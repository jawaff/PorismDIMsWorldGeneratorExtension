// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionScheduleSolverFacade.h"

namespace
{
	using namespace LayoutRegionScheduleSolverFacade;
	using namespace PorismLayoutTestUtilities;

	FLayoutPlannedCell MakePlannedCell(const FIntVector& Cell, const ELayoutCellIntent Intent)
	{
		FLayoutPlannedCell PlannedCell;
		PlannedCell.Cell = Cell;
		PlannedCell.Intent = Intent;
		return PlannedCell;
	}

	void AddSupportSample(
		FLayoutSteppedTerrainSupportMap& SupportMap,
		const FIntVector& Cell,
		const int32 SupportSurfaceZ)
	{
		const int32 SharedCellHeight = FMath::Max(1, SupportMap.SharedCellHeightInBlocks);
		const int32 SnappedFloorZ = FMath::FloorToInt(static_cast<float>(SupportSurfaceZ) / static_cast<float>(SharedCellHeight)) * SharedCellHeight;
		const int32 SnappedCeilingZ = ((SupportSurfaceZ + SharedCellHeight - 1) / SharedCellHeight) * SharedCellHeight;

		FLayoutSteppedTerrainSupportSample& SupportSample = SupportMap.SupportSamples.AddDefaulted_GetRef();
		SupportSample.LocalCell = Cell;
		SupportSample.SupportSurfaceZ = SupportSurfaceZ;
		SupportSample.SnappedSupportFloorZ = SnappedFloorZ;
		SupportSample.SnappedSupportCeilingZ = SnappedCeilingZ;
	}

	void AddAdjacencyStep(
		FLayoutSteppedTerrainSupportMap& SupportMap,
		const FIntVector& FromCell,
		const FIntVector& ToCell,
		const int32 StepHeightBlocks)
	{
		FLayoutSteppedTerrainAdjacencyStep& AdjacencyStep = SupportMap.AdjacencySteps.AddDefaulted_GetRef();
		AdjacencyStep.FromCell = FromCell;
		AdjacencyStep.ToCell = ToCell;
		AdjacencyStep.StepHeightBlocks = StepHeightBlocks;

		const FLayoutSteppedTerrainSupportSample* FromSample = SupportMap.SupportSamples.FindByPredicate(
			[&FromCell](const FLayoutSteppedTerrainSupportSample& Sample)
			{
				return Sample.LocalCell == FromCell;
			});
		const FLayoutSteppedTerrainSupportSample* ToSample = SupportMap.SupportSamples.FindByPredicate(
			[&ToCell](const FLayoutSteppedTerrainSupportSample& Sample)
			{
				return Sample.LocalCell == ToCell;
			});
		const int32 SharedCellHeight = FMath::Max(1, SupportMap.SharedCellHeightInBlocks);
		if (FromSample != nullptr && ToSample != nullptr)
		{
			AdjacencyStep.SnappedLevelDelta = FMath::Abs(ToSample->SnappedSupportCeilingZ - FromSample->SnappedSupportCeilingZ) / SharedCellHeight;
		}
	}

	/** Builds a three-cell frozen route where only its middle Boundary cell distinguishes stepped from ordinary topology. */
	FLayoutRegionSolveRequest BuildSteppedBoundaryRouteRequest(
		UObject* Outer,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const bool bBridgeCell)
	{
		const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
		const FGameplayTagContainer TraversalTags = MakeTags({LayoutGameplayTags::TraversalPrimary});
		ULayoutModuleAsset* Module = CreateModule(
			Outer,
			TEXT("TerrainSteppedBoundaryRouteModule"),
			CreateTemplate(Outer, TEXT("TerrainSteppedBoundaryRouteTemplate"), FIntVector(16, 16, 16)),
			{ELayoutCellIntent::Entry, ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior},
			{
				MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, OpenTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, TraversalTags),
				MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, OpenTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, TraversalTags),
				MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
				MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
				MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
				MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
			});

		FLayoutRegionSolveRequest Request;
		Request.RegionDebugPath = TEXT("SteppedBoundaryRoute");
		Request.RootPlacementKind = PlacementKind;
		Request.FootprintSize = FIntPoint(3, 1);
		Request.ProfileSnapshot.LevelCount = 2;
		Request.ProfileSnapshot.bSupportsSteppedTerrainSolve = true;
		Request.ProfileSnapshot.bEnableTerrainSeams = false;
		Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;
		Request.bHasFinalizedSteppedTerrainIntents = true;
		Request.ModuleCatalog.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(Module, 1));

		FLayoutPlannedCell& FirstEntry = Request.PlannedCells.AddDefaulted_GetRef();
		FirstEntry.Cell = FIntVector(0, 0, 1);
		FirstEntry.Intent = ELayoutCellIntent::Entry;
		FirstEntry.EntryOrigin = ELayoutEntryOrigin::Continuation;
		FirstEntry.ModuleLevelIndex = 1;
		FLayoutPlannedCell& Intermediate = Request.PlannedCells.AddDefaulted_GetRef();
		Intermediate.Cell = FIntVector(1, 0, 1);
		Intermediate.Intent = ELayoutCellIntent::Boundary;
		Intermediate.ModuleLevelIndex = 1;
		Intermediate.bIsBridgeCell = bBridgeCell;
		FLayoutPlannedCell& SecondEntry = Request.PlannedCells.AddDefaulted_GetRef();
		SecondEntry.Cell = FIntVector(2, 0, 1);
		SecondEntry.Intent = ELayoutCellIntent::Entry;
		SecondEntry.EntryOrigin = ELayoutEntryOrigin::Continuation;
		SecondEntry.ModuleLevelIndex = 1;
		return Request;
	}

	ULayoutModuleAsset* CreateSingleCellJunctionVerticalAccessModule(UObject* Outer, const TCHAR* BaseName);

	FLayoutModuleSolveSnapshot BuildSingleCellVerticalAccessSnapshot(UObject* Outer, const TCHAR* BaseName)
	{
		UChunkStructureTemplate* Template = CreateTemplate(
			Outer,
			*FString::Printf(TEXT("%s_Template"), BaseName),
			FIntVector(16, 16, 16));
		ULayoutModuleAsset* Module = CreateModule(
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
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosY,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegY,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosZ,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegZ,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
			});

		return FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	}

	FLayoutModuleSolveSnapshot BuildSingleCellJunctionVerticalAccessSnapshot(UObject* Outer, const TCHAR* BaseName)
	{
		ULayoutModuleAsset* Module =
			CreateSingleCellJunctionVerticalAccessModule(Outer, BaseName);
		return FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	}

	ULayoutModuleAsset* CreateSingleCellVerticalAccessModule(UObject* Outer, const TCHAR* BaseName)
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
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosY,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegY,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosZ,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegZ,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
			});
	}

	ULayoutModuleAsset* CreateSingleCellJunctionVerticalAccessModule(UObject* Outer, const TCHAR* BaseName)
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
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegY,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosZ,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegZ,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
			});
	}

	ULayoutModuleAsset* CreateSingleCellBranchJunctionVerticalAccessModule(UObject* Outer, const TCHAR* BaseName)
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
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosZ,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegZ,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
			});
	}

	ULayoutModuleAsset* CreateSingleCellNegativeYBranchJunctionVerticalAccessModule(UObject* Outer, const TCHAR* BaseName)
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
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
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
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
			});
	}

	ULayoutModuleAsset* CreateSingleCellDualBranchJunctionVerticalAccessModule(UObject* Outer, const TCHAR* BaseName)
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
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
			});
	}

	ULayoutModuleAsset* CreateSingleCellInteriorModule(UObject* Outer, const TCHAR* BaseName)
	{
		UChunkStructureTemplate* Template = CreateTemplate(
			Outer,
			*FString::Printf(TEXT("%s_Template"), BaseName),
			FIntVector(16, 16, 16));
		return CreateModule(
			Outer,
			*FString::Printf(TEXT("%s_Module"), BaseName),
			Template,
			{ELayoutCellIntent::Interior},
			{
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosX,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegX,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosY,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegY,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosZ,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegZ,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
			});
	}

ULayoutModuleAsset* CreateSingleCellPositiveYBranchCorridorInteriorModule(UObject* Outer, const TCHAR* BaseName)
	{
		UChunkStructureTemplate* Template = CreateTemplate(
			Outer,
			*FString::Printf(TEXT("%s_Template"), BaseName),
			FIntVector(16, 16, 16));
		return CreateModule(
			Outer,
			*FString::Printf(TEXT("%s_Module"), BaseName),
			Template,
			{ELayoutCellIntent::Interior},
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
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegY,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosZ,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegZ,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
			});
	}

	ULayoutModuleAsset* CreateSingleCellNegativeYBranchCorridorInteriorModule(UObject* Outer, const TCHAR* BaseName)
	{
		UChunkStructureTemplate* Template = CreateTemplate(
			Outer,
			*FString::Printf(TEXT("%s_Template"), BaseName),
			FIntVector(16, 16, 16));
		return CreateModule(
			Outer,
			*FString::Printf(TEXT("%s_Module"), BaseName),
			Template,
			{ELayoutCellIntent::Interior},
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
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosZ,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegZ,
					LayoutGameplayTags::FaceSolid,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
			});
	}

FLayoutRegionSolveRequest BuildDenseInterleavedDualBranchAsymmetricCorridorMultiTransitionSteppedCoordinatorRequest(UObject* Outer)
	{
		ULayoutModuleAsset* EdgeVerticalAccessModule = CreateSingleCellVerticalAccessModule(
			Outer,
			TEXT("AATerrainSteppedCoordinatorDenseInterleavedDualBranchEdgeVerticalAccess"));
		ULayoutModuleAsset* DualBranchJunctionVerticalAccessModule = CreateSingleCellDualBranchJunctionVerticalAccessModule(
			Outer,
			TEXT("ZZTerrainSteppedCoordinatorDenseInterleavedDualBranchJunctionVerticalAccess"));
		ULayoutModuleAsset* PositiveBranchJunctionVerticalAccessModule = CreateSingleCellBranchJunctionVerticalAccessModule(
			Outer,
			TEXT("ZZTerrainSteppedCoordinatorDenseInterleavedPositiveJunctionVerticalAccess"));
		ULayoutModuleAsset* NegativeBranchJunctionVerticalAccessModule = CreateSingleCellNegativeYBranchJunctionVerticalAccessModule(
			Outer,
			TEXT("ZZTerrainSteppedCoordinatorDenseInterleavedNegativeJunctionVerticalAccess"));
		ULayoutModuleAsset* HorizontalInteriorModule = CreateSingleCellInteriorModule(
			Outer,
			TEXT("TerrainSteppedCoordinatorDenseInterleavedHorizontalInterior"));
		ULayoutModuleAsset* PositiveYInteriorModule = CreateSingleCellPositiveYBranchCorridorInteriorModule(
			Outer,
			TEXT("TerrainSteppedCoordinatorDenseInterleavedPositiveYInterior"));
		ULayoutModuleAsset* NegativeYInteriorModule = CreateSingleCellNegativeYBranchCorridorInteriorModule(
			Outer,
			TEXT("TerrainSteppedCoordinatorDenseInterleavedNegativeYInterior"));

		FLayoutRegionContentEntry EdgeVerticalAccessEntry;
		EdgeVerticalAccessEntry.EntryId = TEXT("AATerrainSteppedCoordinatorDenseInterleavedEdgeVerticalAccessEntry");
		EdgeVerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
		EdgeVerticalAccessEntry.ModuleSettings.Module = EdgeVerticalAccessModule;
		EdgeVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Edge;

		FLayoutRegionContentEntry DualBranchJunctionVerticalAccessEntry;
		DualBranchJunctionVerticalAccessEntry.EntryId = TEXT("ZZTerrainSteppedCoordinatorDenseInterleavedDualBranchJunctionVerticalAccessEntry");
		DualBranchJunctionVerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
		DualBranchJunctionVerticalAccessEntry.ModuleSettings.Module = DualBranchJunctionVerticalAccessModule;
		DualBranchJunctionVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;

		FLayoutRegionContentEntry PositiveBranchJunctionVerticalAccessEntry;
		PositiveBranchJunctionVerticalAccessEntry.EntryId = TEXT("ZZTerrainSteppedCoordinatorDenseInterleavedPositiveJunctionVerticalAccessEntry");
		PositiveBranchJunctionVerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
		PositiveBranchJunctionVerticalAccessEntry.ModuleSettings.Module = PositiveBranchJunctionVerticalAccessModule;
		PositiveBranchJunctionVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;

		FLayoutRegionContentEntry NegativeBranchJunctionVerticalAccessEntry;
		NegativeBranchJunctionVerticalAccessEntry.EntryId = TEXT("ZZTerrainSteppedCoordinatorDenseInterleavedNegativeJunctionVerticalAccessEntry");
		NegativeBranchJunctionVerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
		NegativeBranchJunctionVerticalAccessEntry.ModuleSettings.Module = NegativeBranchJunctionVerticalAccessModule;
		NegativeBranchJunctionVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;

		FLayoutRegionContentEntry HorizontalInteriorEntry;
		HorizontalInteriorEntry.EntryId = TEXT("TerrainSteppedCoordinatorDenseInterleavedHorizontalInteriorEntry");
		HorizontalInteriorEntry.ContentKind = ELayoutRegionContentKind::Module;
		HorizontalInteriorEntry.ModuleSettings.Module = HorizontalInteriorModule;

		FLayoutRegionContentEntry PositiveYInteriorEntry;
		PositiveYInteriorEntry.EntryId = TEXT("TerrainSteppedCoordinatorDenseInterleavedPositiveYInteriorEntry");
		PositiveYInteriorEntry.ContentKind = ELayoutRegionContentKind::Module;
		PositiveYInteriorEntry.ModuleSettings.Module = PositiveYInteriorModule;

		FLayoutRegionContentEntry NegativeYInteriorEntry;
		NegativeYInteriorEntry.EntryId = TEXT("TerrainSteppedCoordinatorDenseInterleavedNegativeYInteriorEntry");
		NegativeYInteriorEntry.ContentKind = ELayoutRegionContentKind::Module;
		NegativeYInteriorEntry.ModuleSettings.Module = NegativeYInteriorModule;

		ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
			Outer,
			TEXT("TerrainSteppedCoordinatorDenseInterleavedContentSet"),
			{
				EdgeVerticalAccessEntry,
				DualBranchJunctionVerticalAccessEntry,
				PositiveBranchJunctionVerticalAccessEntry,
				NegativeBranchJunctionVerticalAccessEntry,
				HorizontalInteriorEntry,
				PositiveYInteriorEntry,
				NegativeYInteriorEntry
			});
		ULayoutProfileAsset* Profile = CreateProfile(
			Outer,
			TEXT("TerrainSteppedCoordinatorDenseInterleavedProfile"),
			FIntPoint(10, 3),
			FIntPoint(10, 3),
			2,
			0,
			false);
		Profile->ContentSet = ContentSet;
		Profile->bSupportsSteppedTerrainSolve = true;

		FLayoutWorldBindingPlacementPolicy PlacementPolicy;
		PlacementPolicy.HeightIgnoreThreshold = 1;

		FLayoutSteppedTerrainSupportMap SupportMap;
		SupportMap.SharedCellHeightInBlocks = 16;
		AddSupportSample(SupportMap, FIntVector(0, 1, 0), 64);
		AddSupportSample(SupportMap, FIntVector(1, 1, 0), 67);
		AddSupportSample(SupportMap, FIntVector(2, 1, 0), 70);
		AddSupportSample(SupportMap, FIntVector(3, 1, 0), 73);
		AddSupportSample(SupportMap, FIntVector(4, 1, 0), 76);
		AddSupportSample(SupportMap, FIntVector(5, 1, 0), 79);
		AddSupportSample(SupportMap, FIntVector(6, 1, 0), 82);
		AddSupportSample(SupportMap, FIntVector(7, 1, 0), 85);
		AddSupportSample(SupportMap, FIntVector(8, 1, 0), 88);
		AddSupportSample(SupportMap, FIntVector(9, 1, 0), 91);
		AddSupportSample(SupportMap, FIntVector(1, 2, 0), 70);
		AddSupportSample(SupportMap, FIntVector(2, 2, 0), 73);
		AddSupportSample(SupportMap, FIntVector(3, 2, 0), 76);
		AddSupportSample(SupportMap, FIntVector(5, 2, 0), 82);
		AddSupportSample(SupportMap, FIntVector(6, 2, 0), 85);
		AddSupportSample(SupportMap, FIntVector(7, 2, 0), 88);
		AddSupportSample(SupportMap, FIntVector(1, 0, 0), 64);
		AddSupportSample(SupportMap, FIntVector(2, 0, 0), 67);
		AddSupportSample(SupportMap, FIntVector(4, 0, 0), 73);
		AddSupportSample(SupportMap, FIntVector(5, 0, 0), 76);
		AddSupportSample(SupportMap, FIntVector(6, 0, 0), 79);
		AddSupportSample(SupportMap, FIntVector(8, 0, 0), 85);
		AddAdjacencyStep(SupportMap, FIntVector(0, 1, 0), FIntVector(1, 1, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(1, 1, 0), FIntVector(2, 1, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(2, 1, 0), FIntVector(3, 1, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(3, 1, 0), FIntVector(4, 1, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(4, 1, 0), FIntVector(5, 1, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(5, 1, 0), FIntVector(6, 1, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(6, 1, 0), FIntVector(7, 1, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(7, 1, 0), FIntVector(8, 1, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(8, 1, 0), FIntVector(9, 1, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(1, 1, 0), FIntVector(1, 2, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(2, 1, 0), FIntVector(2, 2, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(3, 1, 0), FIntVector(3, 2, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(5, 1, 0), FIntVector(5, 2, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(6, 1, 0), FIntVector(6, 2, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(7, 1, 0), FIntVector(7, 2, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(1, 1, 0), FIntVector(1, 0, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(2, 1, 0), FIntVector(2, 0, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(4, 1, 0), FIntVector(4, 0, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(5, 1, 0), FIntVector(5, 0, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(6, 1, 0), FIntVector(6, 0, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(8, 1, 0), FIntVector(8, 0, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(1, 2, 0), FIntVector(2, 2, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(2, 2, 0), FIntVector(3, 2, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(5, 2, 0), FIntVector(6, 2, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(6, 2, 0), FIntVector(7, 2, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(1, 0, 0), FIntVector(2, 0, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(4, 0, 0), FIntVector(5, 0, 0), 3);
		AddAdjacencyStep(SupportMap, FIntVector(5, 0, 0), FIntVector(6, 0, 0), 3);
		SupportMap.MaximumObservedNeighborHeightDelta = 3;

		FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ContentSet,
			Profile,
			27196,
			TEXT("Root"),
			FLayoutSolverExecutionSettings(),
			NAME_None,
			NAME_None,
			0,
			NAME_None,
			ELayoutWorldBindingPlacementKind::OrdinaryRoot,
			PlacementPolicy,
			&SupportMap);
		/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
		Request.FootprintSize = FIntPoint(10, 3);
		Request.PlannedCells = {
			MakePlannedCell(FIntVector(0, 1, 0), ELayoutCellIntent::VerticalAccess),
			MakePlannedCell(FIntVector(1, 1, 0), ELayoutCellIntent::VerticalAccess),
			MakePlannedCell(FIntVector(2, 1, 0), ELayoutCellIntent::VerticalAccess),
			MakePlannedCell(FIntVector(3, 1, 0), ELayoutCellIntent::VerticalAccess),
			MakePlannedCell(FIntVector(4, 1, 0), ELayoutCellIntent::VerticalAccess),
			MakePlannedCell(FIntVector(5, 1, 0), ELayoutCellIntent::VerticalAccess),
			MakePlannedCell(FIntVector(6, 1, 0), ELayoutCellIntent::VerticalAccess),
			MakePlannedCell(FIntVector(7, 1, 0), ELayoutCellIntent::VerticalAccess),
			MakePlannedCell(FIntVector(8, 1, 0), ELayoutCellIntent::VerticalAccess),
			MakePlannedCell(FIntVector(9, 1, 0), ELayoutCellIntent::Interior),
			MakePlannedCell(FIntVector(1, 2, 0), ELayoutCellIntent::Interior),
			MakePlannedCell(FIntVector(2, 2, 0), ELayoutCellIntent::VerticalAccess),
			MakePlannedCell(FIntVector(3, 2, 0), ELayoutCellIntent::Interior),
			MakePlannedCell(FIntVector(5, 2, 0), ELayoutCellIntent::Interior),
			MakePlannedCell(FIntVector(6, 2, 0), ELayoutCellIntent::VerticalAccess),
			MakePlannedCell(FIntVector(7, 2, 0), ELayoutCellIntent::Interior),
			MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
			MakePlannedCell(FIntVector(2, 0, 0), ELayoutCellIntent::VerticalAccess),
			MakePlannedCell(FIntVector(4, 0, 0), ELayoutCellIntent::Interior),
			MakePlannedCell(FIntVector(5, 0, 0), ELayoutCellIntent::VerticalAccess),
			MakePlannedCell(FIntVector(6, 0, 0), ELayoutCellIntent::Interior),
			MakePlannedCell(FIntVector(8, 0, 0), ELayoutCellIntent::Interior)
		};
		return Request;
	}

FLayoutRegionSolveRequest BuildDenseInterleavedDualBranchAsymmetricCorridorMultiTransitionSteppedCoordinatorRequest(
		UObject* Outer,
		const TCHAR* FamilyLabel,
		const int32 CorridorWidth,
		const int32 RandomSeed,
		std::initializer_list<int32> UpperBranchGapXs,
		std::initializer_list<int32> LowerBranchGapXs)
	{
		ULayoutModuleAsset* EdgeVerticalAccessModule = CreateSingleCellVerticalAccessModule(
			Outer,
			*FString::Printf(TEXT("AATerrainSteppedCoordinator%sDualBranchEdgeVerticalAccess"), FamilyLabel));
		ULayoutModuleAsset* DualBranchJunctionVerticalAccessModule = CreateSingleCellDualBranchJunctionVerticalAccessModule(
			Outer,
			*FString::Printf(TEXT("ZZTerrainSteppedCoordinator%sDualBranchJunctionVerticalAccess"), FamilyLabel));
		ULayoutModuleAsset* PositiveBranchJunctionVerticalAccessModule = CreateSingleCellBranchJunctionVerticalAccessModule(
			Outer,
			*FString::Printf(TEXT("ZZTerrainSteppedCoordinator%sPositiveJunctionVerticalAccess"), FamilyLabel));
		ULayoutModuleAsset* NegativeBranchJunctionVerticalAccessModule = CreateSingleCellNegativeYBranchJunctionVerticalAccessModule(
			Outer,
			*FString::Printf(TEXT("ZZTerrainSteppedCoordinator%sNegativeJunctionVerticalAccess"), FamilyLabel));
		ULayoutModuleAsset* HorizontalInteriorModule = CreateSingleCellInteriorModule(
			Outer,
			*FString::Printf(TEXT("TerrainSteppedCoordinator%sHorizontalInterior"), FamilyLabel));
		ULayoutModuleAsset* PositiveYInteriorModule = CreateSingleCellPositiveYBranchCorridorInteriorModule(
			Outer,
			*FString::Printf(TEXT("TerrainSteppedCoordinator%sPositiveYInterior"), FamilyLabel));
		ULayoutModuleAsset* NegativeYInteriorModule = CreateSingleCellNegativeYBranchCorridorInteriorModule(
			Outer,
			*FString::Printf(TEXT("TerrainSteppedCoordinator%sNegativeYInterior"), FamilyLabel));

		FLayoutRegionContentEntry EdgeVerticalAccessEntry;
		EdgeVerticalAccessEntry.EntryId = *FString::Printf(TEXT("AATerrainSteppedCoordinator%sEdgeVerticalAccessEntry"), FamilyLabel);
		EdgeVerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
		EdgeVerticalAccessEntry.ModuleSettings.Module = EdgeVerticalAccessModule;
		EdgeVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Edge;

		FLayoutRegionContentEntry DualBranchJunctionVerticalAccessEntry;
		DualBranchJunctionVerticalAccessEntry.EntryId =
			*FString::Printf(TEXT("ZZTerrainSteppedCoordinator%sDualBranchJunctionVerticalAccessEntry"), FamilyLabel);
		DualBranchJunctionVerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
		DualBranchJunctionVerticalAccessEntry.ModuleSettings.Module = DualBranchJunctionVerticalAccessModule;
		DualBranchJunctionVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;

		FLayoutRegionContentEntry PositiveBranchJunctionVerticalAccessEntry;
		PositiveBranchJunctionVerticalAccessEntry.EntryId =
			*FString::Printf(TEXT("ZZTerrainSteppedCoordinator%sPositiveJunctionVerticalAccessEntry"), FamilyLabel);
		PositiveBranchJunctionVerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
		PositiveBranchJunctionVerticalAccessEntry.ModuleSettings.Module = PositiveBranchJunctionVerticalAccessModule;
		PositiveBranchJunctionVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;

		FLayoutRegionContentEntry NegativeBranchJunctionVerticalAccessEntry;
		NegativeBranchJunctionVerticalAccessEntry.EntryId =
			*FString::Printf(TEXT("ZZTerrainSteppedCoordinator%sNegativeJunctionVerticalAccessEntry"), FamilyLabel);
		NegativeBranchJunctionVerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
		NegativeBranchJunctionVerticalAccessEntry.ModuleSettings.Module = NegativeBranchJunctionVerticalAccessModule;
		NegativeBranchJunctionVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;

		FLayoutRegionContentEntry HorizontalInteriorEntry;
		HorizontalInteriorEntry.EntryId = *FString::Printf(TEXT("TerrainSteppedCoordinator%sHorizontalInteriorEntry"), FamilyLabel);
		HorizontalInteriorEntry.ContentKind = ELayoutRegionContentKind::Module;
		HorizontalInteriorEntry.ModuleSettings.Module = HorizontalInteriorModule;

		FLayoutRegionContentEntry PositiveYInteriorEntry;
		PositiveYInteriorEntry.EntryId = *FString::Printf(TEXT("TerrainSteppedCoordinator%sPositiveYInteriorEntry"), FamilyLabel);
		PositiveYInteriorEntry.ContentKind = ELayoutRegionContentKind::Module;
		PositiveYInteriorEntry.ModuleSettings.Module = PositiveYInteriorModule;

		FLayoutRegionContentEntry NegativeYInteriorEntry;
		NegativeYInteriorEntry.EntryId = *FString::Printf(TEXT("TerrainSteppedCoordinator%sNegativeYInteriorEntry"), FamilyLabel);
		NegativeYInteriorEntry.ContentKind = ELayoutRegionContentKind::Module;
		NegativeYInteriorEntry.ModuleSettings.Module = NegativeYInteriorModule;

		ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
			Outer,
			*FString::Printf(TEXT("TerrainSteppedCoordinator%sContentSet"), FamilyLabel),
			{
				EdgeVerticalAccessEntry,
				DualBranchJunctionVerticalAccessEntry,
				PositiveBranchJunctionVerticalAccessEntry,
				NegativeBranchJunctionVerticalAccessEntry,
				HorizontalInteriorEntry,
				PositiveYInteriorEntry,
				NegativeYInteriorEntry
			});
		ULayoutProfileAsset* Profile = CreateProfile(
			Outer,
			*FString::Printf(TEXT("TerrainSteppedCoordinator%sProfile"), FamilyLabel),
			FIntPoint(CorridorWidth, 3),
			FIntPoint(CorridorWidth, 3),
			2,
			0,
			false);
		Profile->ContentSet = ContentSet;
		Profile->bSupportsSteppedTerrainSolve = true;

		FLayoutWorldBindingPlacementPolicy PlacementPolicy;
		PlacementPolicy.HeightIgnoreThreshold = 1;

		TSet<int32> UpperBranchGapSet;
		for (const int32 X : UpperBranchGapXs)
		{
			UpperBranchGapSet.Add(X);
		}

		TSet<int32> LowerBranchGapSet;
		for (const int32 X : LowerBranchGapXs)
		{
			LowerBranchGapSet.Add(X);
		}

		TArray<FIntVector> UpperBranchCells;
		TArray<FIntVector> LowerBranchCells;
		for (int32 X = 1; X <= CorridorWidth - 2; ++X)
		{
			if (!UpperBranchGapSet.Contains(X))
			{
				UpperBranchCells.Add(FIntVector(X, 2, 0));
			}
			if (!LowerBranchGapSet.Contains(X))
			{
				LowerBranchCells.Add(FIntVector(X, 0, 0));
			}
		}

		FLayoutSteppedTerrainSupportMap SupportMap;
		SupportMap.SharedCellHeightInBlocks = 16;
		for (int32 X = 0; X <= CorridorWidth - 1; ++X)
		{
			AddSupportSample(SupportMap, FIntVector(X, 1, 0), 64 + (X * 3));
		}
		for (const FIntVector& UpperBranchCell : UpperBranchCells)
		{
			AddSupportSample(SupportMap, UpperBranchCell, 64 + (UpperBranchCell.X * 3) + 3);
		}
		for (const FIntVector& LowerBranchCell : LowerBranchCells)
		{
			AddSupportSample(SupportMap, LowerBranchCell, 64 + ((LowerBranchCell.X - 1) * 3));
		}
		for (int32 X = 0; X <= CorridorWidth - 2; ++X)
		{
			AddAdjacencyStep(SupportMap, FIntVector(X, 1, 0), FIntVector(X + 1, 1, 0), 3);
		}
		for (const FIntVector& UpperBranchCell : UpperBranchCells)
		{
			AddAdjacencyStep(SupportMap, FIntVector(UpperBranchCell.X, 1, 0), UpperBranchCell, 3);
		}
		for (const FIntVector& LowerBranchCell : LowerBranchCells)
		{
			AddAdjacencyStep(SupportMap, FIntVector(LowerBranchCell.X, 1, 0), LowerBranchCell, 3);
		}
		for (int32 Index = 0; Index + 1 < UpperBranchCells.Num(); ++Index)
		{
			AddAdjacencyStep(SupportMap, UpperBranchCells[Index], UpperBranchCells[Index + 1], 3);
		}
		for (int32 Index = 0; Index + 1 < LowerBranchCells.Num(); ++Index)
		{
			AddAdjacencyStep(SupportMap, LowerBranchCells[Index], LowerBranchCells[Index + 1], 3);
		}
		SupportMap.MaximumObservedNeighborHeightDelta = 3;

		FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ContentSet,
			Profile,
			RandomSeed,
			TEXT("Root"),
			FLayoutSolverExecutionSettings(),
			NAME_None,
			NAME_None,
			0,
			NAME_None,
			ELayoutWorldBindingPlacementKind::OrdinaryRoot,
			PlacementPolicy,
			&SupportMap);
		/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
		Request.FootprintSize = FIntPoint(CorridorWidth, 3);
		for (int32 X = 0; X <= CorridorWidth - 2; ++X)
		{
			Request.PlannedCells.Add(MakePlannedCell(FIntVector(X, 1, 0), ELayoutCellIntent::VerticalAccess));
		}
		Request.PlannedCells.Add(MakePlannedCell(FIntVector(CorridorWidth - 1, 1, 0), ELayoutCellIntent::Interior));
		for (const FIntVector& UpperBranchCell : UpperBranchCells)
		{
			Request.PlannedCells.Add(MakePlannedCell(UpperBranchCell, ELayoutCellIntent::Interior));
		}
		for (const FIntVector& LowerBranchCell : LowerBranchCells)
		{
			Request.PlannedCells.Add(MakePlannedCell(LowerBranchCell, ELayoutCellIntent::Interior));
		}
		return Request;
	}

	FLayoutRegionSolveRequest BuildDenseInterleavedDualBranchAsymmetricCorridorMultiTransitionSteppedCoordinatorRequest(
		UObject* Outer,
		const TCHAR* FamilyLabel,
		const int32 CorridorWidth,
		const int32 RandomSeed)
	{
		return BuildDenseInterleavedDualBranchAsymmetricCorridorMultiTransitionSteppedCoordinatorRequest(
			Outer,
			FamilyLabel,
			CorridorWidth,
			RandomSeed,
			{4, 9},
			{3, 8});
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveBuildsTransitionRequirementsTest,
	"PorismExtension.Layout.Solver.TerrainStepped.BuildsTransitionRequirements",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveIgnoresMildNeighborDeltasTest,
	"PorismExtension.Layout.Solver.TerrainStepped.IgnoresMildNeighborDeltas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveSupportsNonOriginVerticalAccessCellTest,
	"PorismExtension.Layout.Solver.TerrainStepped.SupportsNonOriginVerticalAccessCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveSupportsNeighboringVerticalAccessCellTest,
	"PorismExtension.Layout.Solver.TerrainStepped.SupportsNeighboringVerticalAccessCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveUsesPlannedAscentInsteadOfRawSeamTest,
	"PorismExtension.Layout.Solver.TerrainStepped.UsesPlannedAscentInsteadOfRawSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveTracksMissingRootVerticalAccessCandidatesTest,
	"PorismExtension.Layout.Solver.TerrainStepped.TracksMissingRootVerticalAccessCandidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveTracksMissingAdjacentVerticalAccessIntentTest,
	"PorismExtension.Layout.Solver.TerrainStepped.TracksMissingAdjacentVerticalAccessIntent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveBuildsDeterministicRejectionSummaryTest,
	"PorismExtension.Layout.Solver.TerrainStepped.BuildsDeterministicRejectionSummary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveBuildsRouteConstraintsTest,
	"PorismExtension.Layout.Solver.TerrainStepped.BuildsRouteConstraints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveBuildsSupportPlansTest,
	"PorismExtension.Layout.Solver.TerrainStepped.BuildsSupportPlans",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveSelectsDeterministicSupportingVerticalAccessCellTest,
	"PorismExtension.Layout.Solver.TerrainStepped.SelectsDeterministicSupportingVerticalAccessCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveKeepsOriginSupportCellWhenTiePersistsTest,
	"PorismExtension.Layout.Solver.TerrainStepped.KeepsOriginSupportCellWhenTiePersists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveBuildsInsertionPlansTest,
	"PorismExtension.Layout.Solver.TerrainStepped.BuildsInsertionPlans",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveIncludesEveryPlannedAscentInsertionTest,
	"PorismExtension.Layout.Solver.TerrainStepped.IncludesEveryPlannedAscentInsertion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveBuildsPreparedSolveContractTest,
	"PorismExtension.Layout.Solver.TerrainStepped.BuildsPreparedSolveContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveSkipsGeneratedInsertionForFlexibleHostGroupTest,
	"PorismExtension.Layout.Solver.TerrainStepped.SkipsGeneratedInsertionForFlexibleHostGroup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedBoundaryIntermediatePolicyUsesSharedDeckProvenanceTest,
	"PorismExtension.Layout.Solver.TerrainStepped.BoundaryIntermediatePolicyUsesSharedDeckProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedPreparedRouteUsesGeneratedBoundaryIntermediateTest,
	"PorismExtension.Layout.Solver.TerrainStepped.PreparedRouteUsesGeneratedBoundaryIntermediate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveStagesDemandActivationUntilAscentFrontierUnlockTest,
	"PorismExtension.Layout.Solver.TerrainStepped.StagesDemandActivationUntilAscentFrontierUnlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedFinalizedCellViewRejectsInvalidFrozenMappingsTest,
	"PorismExtension.Layout.Solver.TerrainStepped.FinalizedCellViewRejectsInvalidFrozenMappings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedFrozenTopologyRejectsEmptyDomainBeforeWorkerTest,
	"PorismExtension.Layout.Solver.TerrainStepped.FrozenTopologyRejectsEmptyDomainBeforeWorker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedResidualAuditUsesBridgeFlagTest,
	"PorismExtension.Layout.Solver.TerrainStepped.ResidualAuditUsesBridgeFlag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSolveAppliesReservedOpenAfterProjectionTest,
	"PorismExtension.Layout.Solver.TerrainStepped.AppliesReservedOpenAfterProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedGeneratedDeckVerticalAccessDependsOnTraversalRequirementsTest,
	"PorismExtension.Layout.Solver.TerrainStepped.GeneratedDeckVerticalAccessDependsOnTraversalRequirements",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedReservedOpenIgnoresUnrelatedBaselinePairFailureTest,
	"PorismExtension.Layout.Solver.TerrainStepped.ReservedOpenIgnoresUnrelatedBaselinePairFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSteppedFinalizedCellViewRejectsInvalidFrozenMappingsTest::RunTest(const FString& Parameters)
{
	auto BuildFrozenSteppedRequest = []()
	{
		FLayoutRegionSolveRequest Request;
		Request.RegionDebugPath = TEXT("FinalizedCellView");
		Request.FootprintSize = FIntPoint(1, 1);
		Request.ProfileSnapshot.bSupportsSteppedTerrainSolve = true;
		Request.bHasSelectedModePlan = true;
		Request.SelectedModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	Request.SelectedModePlan.bUsesSteppedTerrainTopology = true;
		Request.bHasFinalizedSteppedTerrainIntents = true;
		FLayoutPlannedCell& PlannedCell = Request.PlannedCells.AddDefaulted_GetRef();
		PlannedCell.Cell = FIntVector(0, 0, 0);
		PlannedCell.ModuleLevelIndex = 0;
		Request.PrecomputedFrozenTerrainContract.ActiveCells.AddDefaulted_GetRef().Cell = PlannedCell.Cell;
		return Request;
	};

	FLayoutRegionSolveRequest MissingStageRequest = BuildFrozenSteppedRequest();
	const LayoutProfileSolverInternal::FSolveContext MissingStageContext =
		LayoutProfileSolverInternal::BuildSolveContextFromRequestForTests(MissingStageRequest);
	TestTrue(TEXT("Finalized view rejects frozen Stepped request without stage map"),
		MissingStageContext.Result.FailureReason.Contains(TEXT("requires frozen stage-map")));

	FLayoutRegionSolveRequest DuplicateStageRequest = BuildFrozenSteppedRequest();
	FLayoutFrozenTerrainStageCellRecord& FirstStage =
		DuplicateStageRequest.PrecomputedFrozenTerrainContract.StageMap.AddDefaulted_GetRef();
	FirstStage.FootprintCellXY = FIntPoint(0, 0);
	FLayoutFrozenTerrainStageCellRecord& DuplicateStage =
		DuplicateStageRequest.PrecomputedFrozenTerrainContract.StageMap.AddDefaulted_GetRef();
	DuplicateStage.FootprintCellXY = FIntPoint(0, 0);
	const LayoutProfileSolverInternal::FSolveContext DuplicateStageContext =
		LayoutProfileSolverInternal::BuildSolveContextFromRequestForTests(DuplicateStageRequest);
	TestTrue(TEXT("Finalized view rejects duplicate frozen stage column"),
		DuplicateStageContext.Result.FailureReason.Contains(TEXT("duplicate frozen terrain-stage column")));

	FLayoutRegionSolveRequest AmbiguousSourceRequest = BuildFrozenSteppedRequest();
	FLayoutPlannedCell& ShiftedCell = AmbiguousSourceRequest.PlannedCells.AddDefaulted_GetRef();
	ShiftedCell.Cell = FIntVector(0, 0, 1);
	ShiftedCell.ModuleLevelIndex = 0;
	AmbiguousSourceRequest.PrecomputedFrozenTerrainContract.ActiveCells.AddDefaulted_GetRef().Cell = ShiftedCell.Cell;
	AmbiguousSourceRequest.PrecomputedFrozenTerrainContract.StageMap.AddDefaulted_GetRef().FootprintCellXY = FIntPoint(0, 0);
	const LayoutProfileSolverInternal::FSolveContext AmbiguousSourceContext =
		LayoutProfileSolverInternal::BuildSolveContextFromRequestForTests(AmbiguousSourceRequest);
	TestTrue(TEXT("Finalized view rejects ambiguous authored source cell"),
		AmbiguousSourceContext.Result.FailureReason.Contains(TEXT("ambiguous authored source cell")));
	return true;
}

bool FLayoutTerrainSteppedFrozenTopologyRejectsEmptyDomainBeforeWorkerTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.RegionDebugPath = TEXT("FrozenTopologyPreflight");
	Request.FootprintSize = FIntPoint(1, 1);
	Request.ProfileSnapshot.bSupportsSteppedTerrainSolve = true;
	Request.ProfileSnapshot.EntryCountMode = ELayoutCountConstraintMode::None;
	Request.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::None;
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	Request.SelectedModePlan.bUsesSteppedTerrainTopology = true;
	Request.WorldBindingPlacementPolicy.TerrainTransition.MinimumSteppedTerrainShiftClusterCells = 1;
	FLayoutPlannedCell& ShiftedInterior = Request.PlannedCells.AddDefaulted_GetRef();
	ShiftedInterior.Cell = FIntVector(0, 0, 1);
	ShiftedInterior.Intent = ELayoutCellIntent::Interior;
	ShiftedInterior.ModuleLevelIndex = 0;
	Request.PrecomputedFrozenTerrainContract.ActiveCells.AddDefaulted_GetRef().Cell = ShiftedInterior.Cell;
	Request.PrecomputedFrozenTerrainContract.StageMap.AddDefaulted_GetRef().FootprintCellXY = FIntPoint(0, 0);

	TArray<FLayoutPlannedCell> FinalizedCells = Request.PlannedCells;
	FString FailureReason;
	ELayoutSteppedTerrainFinalizationFailureKind FailureKind =
		ELayoutSteppedTerrainFinalizationFailureKind::None;
	TestFalse(TEXT("Frozen shifted topology rejects an empty static domain before worker solve"),
		FLayoutProfileSolver::FinalizeSteppedTerrainPlan(
			FinalizedCells,
			Request,
			FailureReason,
			&FailureKind));
	TestTrue(TEXT("Frozen topology rejection identifies pre-dispatch static admission"),
		FailureReason.Contains(TEXT("Frozen stepped topology rejected before worker dispatch")));
	TestTrue(TEXT("Frozen topology rejection records first unsupported domain"),
		FailureReason.Contains(TEXT("first unsupported domain=")));
	TestEqual(
		TEXT("Frozen static-domain rejection is classified for configured flat fallback"),
		FailureKind,
		ELayoutSteppedTerrainFinalizationFailureKind::StageTopologyInfeasible);
	return true;
}

bool FLayoutTerrainSteppedResidualAuditUsesBridgeFlagTest::RunTest(const FString& Parameters)
{
	FLayoutSolveResult SolveResult;
	FLayoutPlannedCell& ShiftedAuthoredCell = SolveResult.PlannedCells.AddDefaulted_GetRef();
	ShiftedAuthoredCell.Cell = FIntVector(2, 3, 3);
	ShiftedAuthoredCell.ModuleLevelIndex = 2;
	ShiftedAuthoredCell.bIsBridgeCell = false;
	SolveResult.ResidualUnoccupiedCells.AddDefaulted_GetRef().Cell = ShiftedAuthoredCell.Cell;

	FLayoutPlannedCell UnoccupiedBridgeCell;
	TestFalse(
		TEXT("Shifted authored top-level cell is not a generated bridge"),
		LayoutProfileSolverInternal::TryFindFirstUnoccupiedGeneratedBridgeCell(
			SolveResult,
			UnoccupiedBridgeCell));

	FLayoutPlannedCell& GeneratedBridgeCell = SolveResult.PlannedCells.AddDefaulted_GetRef();
	GeneratedBridgeCell.Cell = FIntVector(4, 3, 3);
	GeneratedBridgeCell.ModuleLevelIndex = 2;
	GeneratedBridgeCell.bIsBridgeCell = true;
	SolveResult.ResidualUnoccupiedCells.AddDefaulted_GetRef().Cell = GeneratedBridgeCell.Cell;
	TestTrue(
		TEXT("Unoccupied generated bridge remains invalid"),
		LayoutProfileSolverInternal::TryFindFirstUnoccupiedGeneratedBridgeCell(
			SolveResult,
			UnoccupiedBridgeCell));
	TestEqual(TEXT("Generated bridge rejection identifies exact cell"),
		UnoccupiedBridgeCell.Cell,
		GeneratedBridgeCell.Cell);
	return true;
}

bool FLayoutTerrainSteppedSolveAppliesReservedOpenAfterProjectionTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_AppliesReservedOpenAfterProjection"));
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		Outer,
		TEXT("TerrainSteppedProjectedReservedOpenProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		1,
		0,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;
	Profile->bEnableTerrainSeams = true;
	FLayoutReservedOpenSpaceRule& Rule = Profile->ReservedOpenSpaceRules.AddDefaulted_GetRef();
	Rule.RuleId = TEXT("ProjectedTopLevelVoid");
	Rule.PlacementZone = ELayoutPlacementZone::Any;
	Rule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::TopLevelOnly;
	Rule.MinReservedCells = 1;
	Rule.MaxReservedCells = 1;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 1337);
	Request.SteppedTerrainSupportMap.MaximumObservedSnappedLevelDelta = 1;
	TArray<FLayoutPlannedCell> ProjectedCells;
	for (int32 Y = 0; Y < 3; ++Y)
	{
		for (int32 X = 0; X < 3; ++X)
		{
			FLayoutPlannedCell& Cell = ProjectedCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, X == 2 ? 1 : 0);
			Cell.Intent = ELayoutCellIntent::Interior;
			Cell.ModuleLevelIndex = 0;
		}
	}
	ProjectedCells[0].TerrainSeamFaceMask = LayoutFaceDirectionMask(ELayoutFaceDirection::PosX);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	Request.SelectedModePlan.bUsesSteppedTerrainTopology = true;
	for (const FLayoutPlannedCell& Cell : ProjectedCells)
	{
		Request.PrecomputedFrozenTerrainContract.ActiveCells.AddDefaulted_GetRef().Cell = Cell.Cell;
	}
	for (int32 Y = 0; Y < 3; ++Y)
	{
		for (int32 X = 0; X < 3; ++X)
		{
			FLayoutFrozenTerrainStageCellRecord& Stage =
				Request.PrecomputedFrozenTerrainContract.StageMap.AddDefaulted_GetRef();
			Stage.FootprintCellXY = FIntPoint(X, Y);
			Stage.TerrainStageIndex = X == 2 ? 1 : 0;
		}
	}

	FString FailureReason;
	const bool bFinalized = FLayoutProfileSolver::FinalizeSteppedTerrainPlan(
		ProjectedCells,
		Request,
		FailureReason);
	TestTrue(FString::Printf(TEXT("Projected stepped plan finalizes: %s"), *FailureReason), bFinalized);
	TestEqual(TEXT("One reserved-open cell is removed after stepped projection"), ProjectedCells.Num(), 8);
	TestTrue(TEXT("Reserved-open selection protects projected terrain-seam carriers"), ProjectedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell == FIntVector(0, 0, 0) && Cell.TerrainSeamFaceMask != 0;
	}));
	TestFalse(TEXT("Every surviving cell retains authored logical level zero"), ProjectedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.ModuleLevelIndex != 0;
	}));
	return true;
}

bool FLayoutTerrainSteppedReservedOpenIgnoresUnrelatedBaselinePairFailureTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_ReservedOpenBaselinePairFailure"));
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		Outer,
		TEXT("TerrainSteppedReservedOpenBaselinePairFailureProfile"),
		FIntPoint(5, 5),
		FIntPoint(5, 5),
		1,
		0,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;
	FLayoutReservedOpenSpaceRule& Rule = Profile->ReservedOpenSpaceRules.AddDefaulted_GetRef();
	Rule.RuleId = TEXT("InteriorVoid");
	Rule.PlacementZone = ELayoutPlacementZone::Interior;
	Rule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::TopLevelOnly;
	Rule.MinReservedCells = 1;
	Rule.MaxReservedCells = 1;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 1339);
	Request.ModuleCatalog.Modules.RemoveAll([](const FLayoutModuleSolveSnapshot& Snapshot)
	{
		return Snapshot.Roles.Contains(ELayoutModuleRole::VerticalAccess);
	});
	TArray<FLayoutPlannedCell> PlannedCells;
	for (int32 Y = 0; Y < 5; ++Y)
	{
		for (int32 X = 0; X < 5; ++X)
		{
			FLayoutPlannedCell& Cell = PlannedCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			Cell.ModuleLevelIndex = 0;
			Cell.Intent = (X == 0 || Y == 0 || X == 4 || Y == 4)
				? ELayoutCellIntent::Boundary
				: ELayoutCellIntent::Interior;
		}
	}
	PlannedCells[0].Intent = ELayoutCellIntent::VerticalAccess;

	FString FailureReason;
	ELayoutSteppedTerrainFinalizationFailureKind FailureKind =
		ELayoutSteppedTerrainFinalizationFailureKind::None;
	TestTrue(
		FString::Printf(TEXT("Unrelated baseline pair failure does not reject reserved-open selection: %s"), *FailureReason),
		FLayoutProfileSolver::FinalizeSteppedTerrainPlan(
			PlannedCells,
			Request,
			FailureReason,
			&FailureKind));
	TestEqual(TEXT("Reservation selection does not classify unrelated baseline failure"),
		FailureKind,
		ELayoutSteppedTerrainFinalizationFailureKind::None);
	TestEqual(TEXT("One interior cell remains reserved open"), PlannedCells.Num(), 24);
	return true;
}

bool FLayoutTerrainSteppedGeneratedDeckVerticalAccessDependsOnTraversalRequirementsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_GeneratedDeckHostClassification"));
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		Outer,
		TEXT("TerrainSteppedGeneratedDeckHostClassificationProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		3,
		0,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;
	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 1338);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	Request.SelectedModePlan.PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	Request.SelectedModePlan.bUsesSteppedTerrainTopology = true;
	Request.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::None;
	Request.ProfileSnapshot.VerticalAccessCount = 0;
	Request.ProfileSnapshot.MinVerticalAccessCount = 0;
	Request.ProfileSnapshot.MaxVerticalAccessCount = 0;
	Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	Request.SteppedTerrainSupportMap.MaximumObservedSnappedLevelDelta = 1;

	TArray<FLayoutPlannedCell> PlannedCells;
	for (int32 Y = 0; Y < 3; ++Y)
	{
		for (int32 X = 0; X < 3; ++X)
		{
			const int32 StageIndex = X == 2 ? 1 : 0;
			FLayoutFrozenTerrainStageCellRecord& Stage =
				Request.PrecomputedFrozenTerrainContract.StageMap.AddDefaulted_GetRef();
			Stage.FootprintCellXY = FIntPoint(X, Y);
			Stage.TerrainStageIndex = StageIndex;
			FLayoutSteppedTerrainSupportSample& Support =
				Request.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
			Support.LocalCell = FIntVector(X, Y, 0);
			Support.SnappedSupportFloorZ = StageIndex * 16;
			if (X == 1)
			{
				FLayoutSteppedTerrainAdjacencyStep& Step =
					Request.SteppedTerrainSupportMap.AdjacencySteps.AddDefaulted_GetRef();
				Step.FromCell = FIntVector(1, Y, 0);
				Step.ToCell = FIntVector(2, Y, 0);
				Step.StepHeightBlocks = 16;
				Step.SnappedLevelDelta = 1;
			}
			for (int32 ModuleLevel = 0; ModuleLevel < 3; ++ModuleLevel)
			{
				FLayoutPlannedCell& Cell = PlannedCells.AddDefaulted_GetRef();
				Cell.Cell = FIntVector(X, Y, ModuleLevel + StageIndex);
				Cell.ModuleLevelIndex = ModuleLevel;
				Cell.Intent = (X == 0 || Y == 0 || X == 2 || Y == 2)
					? ELayoutCellIntent::Boundary
					: ELayoutCellIntent::Interior;
			}
			if (StageIndex > 0)
			{
				FLayoutPlannedCell& Bridge = PlannedCells.AddDefaulted_GetRef();
				Bridge.Cell = FIntVector(X, Y, 0);
				Bridge.ModuleLevelIndex = 0;
				Bridge.Intent = ELayoutCellIntent::Boundary;
				Bridge.bIsBridgeCell = true;
			}
		}
	}
	FLayoutPlannedCell& GeneratedTopBridge = PlannedCells.AddDefaulted_GetRef();
	GeneratedTopBridge.Cell = FIntVector(1, 1, 3);
	GeneratedTopBridge.ModuleLevelIndex = 2;
	GeneratedTopBridge.Intent = ELayoutCellIntent::Boundary;
	GeneratedTopBridge.bIsBridgeCell = true;
	Request.ModuleCatalog.Modules.RemoveAll([](const FLayoutModuleSolveSnapshot& Snapshot)
	{
		return Snapshot.Roles.Contains(ELayoutModuleRole::VerticalAccess);
	});

	const TArray<FLayoutPlannedCell> SourcePlannedCells = PlannedCells;
	FString FailureReason;
	ELayoutSteppedTerrainFinalizationFailureKind FailureKind = ELayoutSteppedTerrainFinalizationFailureKind::None;
	TestTrue(
		TEXT("Generated shifted decks do not require VerticalAccess when traversal reachability and vertical-access counts are disabled"),
		LayoutProfileSolverInternal::EnrichPlannedCellsWithSteppedTerrainIntents(
			PlannedCells,
			Request,
			FailureReason,
			&FailureKind));
	TestTrue(TEXT("Optional traversal produces no VerticalAccess host groups"), Request.VerticalAccessHostGroups.IsEmpty());
	TestFalse(TEXT("Optional traversal produces no VerticalAccess planned cells"), PlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Intent == ELayoutCellIntent::VerticalAccess;
	}));

	Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable = true;
	Request.VerticalAccessHostGroups.Reset();
	PlannedCells = SourcePlannedCells;
	FailureReason.Reset();
	FailureKind = ELayoutSteppedTerrainFinalizationFailureKind::None;
	TestFalse(TEXT("Generated shifted deck rejects when required traversal has no VerticalAccess-role module"),
		LayoutProfileSolverInternal::EnrichPlannedCellsWithSteppedTerrainIntents(
			PlannedCells,
			Request,
			FailureReason,
			&FailureKind));
	TestTrue(
		FString::Printf(TEXT("Required traversal rejection identifies module-compatible host exhaustion: %s"), *FailureReason),
		FailureReason.Contains(TEXT("no module-compatible VerticalAccess host")));
	TestEqual(TEXT("Required traversal host exhaustion is classified for stepped-to-flat fallback"),
		FailureKind,
		ELayoutSteppedTerrainFinalizationFailureKind::StageTopologyInfeasible);
	return true;
}

bool FLayoutTerrainSteppedSolveBuildsTransitionRequirementsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_BuildsTransitionRequirements"));

	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(2, 1);
	RootRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 1;
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Interior)
	};
	RootRequest.ModuleCatalog.Modules.Add(BuildSingleCellVerticalAccessSnapshot(Outer, TEXT("TerrainSteppedSupport")));

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), 67);
	AddAdjacencyStep(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), FIntVector(1, 0, 0), 3);
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 3;

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));

	if (!TestEqual(TEXT("Stepped structural inputs compile one terrain transition requirement above the ignore threshold"), StructuralInputs.RootTerrainSteppedTransitionRequirements.Num(), 1))
	{
		return false;
	}

	const FTerrainSteppedTransitionRequirement& Requirement = StructuralInputs.RootTerrainSteppedTransitionRequirements[0];
	TestEqual(TEXT("Stepped transition requirement preserves the lower support cell"), Requirement.FromCell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Planned ascent requirement preserves the direct upper landing cell"), Requirement.ToCell, FIntVector(0, 0, 1));
	TestEqual(TEXT("Planned ascent requirement preserves shared cell height"), Requirement.StepHeightBlocks, 16);
	TestEqual(TEXT("Stepped transition requirement preserves the single adjacent VA cell when only fromCell has VA intent"), Requirement.AdjacentVerticalAccessCells.Num(), 1);
	TestEqual(TEXT("Stepped transition requirement preserves the adjacent VA candidate cell"), Requirement.AdjacentVerticalAccessCells[0], FIntVector(0, 0, 0));
	TestEqual(TEXT("Stepped transition requirement preserves one root vertical-access support candidate"), Requirement.SupportingRootVerticalAccessCandidateCells.Num(), 1);
	TestEqual(TEXT("Stepped transition requirement preserves the supporting root VA candidate cell"), Requirement.SupportingRootVerticalAccessCandidateCells[0], FIntVector(0, 0, 0));
	TestEqual(TEXT("Stepped transition requirement preserves one supporting root placement bundle id"), Requirement.SupportingRootVerticalAccessBundleIds.Num(), 1);
	TestTrue(TEXT("Stepped transition requirement keeps a non-empty supporting root placement bundle id"), Requirement.SupportingRootVerticalAccessBundleIds[0] != NAME_None);
	TestEqual(TEXT("Stepped transition requirement classifies root-side support as satisfied by a frozen vertical-access candidate"), Requirement.RootSupportStatus, ETerrainSteppedTransitionRootSupportStatus::SupportedByRootVerticalAccessCandidate);
	TestEqual(TEXT("Stepped transition requirement compiles two required traversal cells for the transition"), Requirement.RequiredTraversalCells.Num(), 2);
	TestEqual(TEXT("Stepped transition requirement keeps the lower traversal cell in the required route set"), Requirement.RequiredTraversalCells[0], FIntVector(0, 0, 0));
	TestEqual(TEXT("Planned ascent requirement keeps the upper landing in the protected traversal set"), Requirement.RequiredTraversalCells[1], FIntVector(0, 0, 1));
	TestTrue(TEXT("Stepped structural inputs report that the root can satisfy all compiled terrain-step transitions"), StructuralInputs.bRootCanSatisfyTerrainSteppedTransitions);
	TestEqual(TEXT("Stepped structural inputs preserve no unsupported terrain-step transitions when root support exists"), StructuralInputs.RootUnsupportedTerrainSteppedTransitionRequirements.Num(), 0);
	const FTerrainSteppedRejectionSummary RejectionSummary = BuildRootTerrainSteppedRejectionSummary(StructuralInputs);
	TestFalse(TEXT("Stepped rejection summary reports no unsupported transition when root support exists"), RejectionSummary.bHasUnsupportedTransition);
	TestTrue(TEXT("Stepped rejection summary stays empty when root support exists"), RejectionSummary.FailureReason.IsEmpty());
	const TArray<FTerrainSteppedSupportPlan> SupportPlans = BuildRootTerrainSteppedSupportPlans(StructuralInputs);
	TestEqual(TEXT("Stepped support-plan helper emits one accepted support plan for one supported terrain transition"), SupportPlans.Num(), 1);
	TestEqual(TEXT("Stepped support plan preserves the supported lower cell"), SupportPlans[0].TransitionRequirement.FromCell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Stepped support plan preserves the supported upper landing cell"), SupportPlans[0].TransitionRequirement.ToCell, FIntVector(0, 0, 1));
	TestEqual(TEXT("Stepped support plan selects the highest-scoring supporting root bundle id when only one supporting bundle exists"), SupportPlans[0].SelectedRootVerticalAccessBundleId, Requirement.SupportingRootVerticalAccessBundleIds[0]);
	return true;
}

bool FLayoutTerrainSteppedSolveIgnoresMildNeighborDeltasTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(2, 1);
	RootRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 2;
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::VerticalAccess)
	};

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), 66);
	AddAdjacencyStep(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), FIntVector(1, 0, 0), 2);
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 2;

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));

	TestEqual(TEXT("Stepped structural inputs ignore terrain deltas at or below the ignore threshold"), StructuralInputs.RootTerrainSteppedTransitionRequirements.Num(), 0);
	return true;
}

bool FLayoutTerrainSteppedSolveSupportsNonOriginVerticalAccessCellTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_SupportsNonOriginVerticalAccessCell"));

	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(2, 1);
	RootRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 1;
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(1, 0, 1), ELayoutCellIntent::Interior)
	};
	RootRequest.ModuleCatalog.Modules.Add(BuildSingleCellVerticalAccessSnapshot(Outer, TEXT("TerrainSteppedNonOriginSupport")));

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), 67);
	AddAdjacencyStep(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), FIntVector(1, 0, 0), 3);
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 3;

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));
	if (!TestEqual(TEXT("Stepped structural inputs still compile one terrain transition requirement when the adjacent VerticalAccess cell is not at local origin"), StructuralInputs.RootTerrainSteppedTransitionRequirements.Num(), 1))
	{
		return false;
	}

	const FTerrainSteppedTransitionRequirement& Requirement = StructuralInputs.RootTerrainSteppedTransitionRequirements[0];
	TestEqual(TEXT("Stepped transition requirement preserves the single adjacent VerticalAccess cell when only the non-origin cell has VA intent"), Requirement.AdjacentVerticalAccessCells.Num(), 1);
	TestEqual(TEXT("Stepped transition requirement preserves one supporting candidate cell"), Requirement.SupportingRootVerticalAccessCandidateCells.Num(), 1);
	TestEqual(TEXT("Stepped transition requirement keeps the supporting candidate cell"), Requirement.SupportingRootVerticalAccessCandidateCells[0], FIntVector(1, 0, 0));
	TestEqual(TEXT("Stepped transition requirement finds one supporting root placement bundle id"), Requirement.SupportingRootVerticalAccessBundleIds.Num(), 1);
	TestEqual(TEXT("Stepped transition requirement classifies the non-origin VerticalAccess support as satisfied"), Requirement.RootSupportStatus, ETerrainSteppedTransitionRootSupportStatus::SupportedByRootVerticalAccessCandidate);
	return true;
}

bool FLayoutTerrainSteppedSolveSupportsNeighboringVerticalAccessCellTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_SupportsNeighboringVerticalAccessCell"));

	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(2, 2);
	RootRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 1;
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(0, 1, 1), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(1, 1, 1), ELayoutCellIntent::Boundary),
		MakePlannedCell(FIntVector(0, 1, 2), ELayoutCellIntent::Interior)
	};
	RootRequest.ModuleCatalog.Modules.Add(BuildSingleCellVerticalAccessSnapshot(Outer, TEXT("TerrainSteppedNeighboringSupport")));

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), 67);
	AddAdjacencyStep(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), FIntVector(1, 0, 0), 3);
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 3;

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));
	if (!TestEqual(TEXT("Stepped structural inputs compile one requirement for the shifted planned ascent"), StructuralInputs.RootTerrainSteppedTransitionRequirements.Num(), 1))
	{
		return false;
	}

	const FTerrainSteppedTransitionRequirement& Requirement = StructuralInputs.RootTerrainSteppedTransitionRequirements[0];
	TestEqual(TEXT("Planned ascent requirement preserves its VerticalAccess cell"), Requirement.AdjacentVerticalAccessCells.Num(), 1);
	TestEqual(TEXT("Stepped transition requirement preserves the shifted adjacent cell"), Requirement.AdjacentVerticalAccessCells[0], FIntVector(0, 1, 1));
	TestEqual(TEXT("Stepped transition requirement preserves one supporting root vertical-access candidate"), Requirement.SupportingRootVerticalAccessCandidateCells.Num(), 1);
	TestEqual(TEXT("Stepped transition requirement keeps the shifted supporting candidate cell"), Requirement.SupportingRootVerticalAccessCandidateCells[0], FIntVector(0, 1, 1));
	TestEqual(TEXT("Stepped transition requirement finds one supporting root placement bundle id"), Requirement.SupportingRootVerticalAccessBundleIds.Num(), 1);
	TestEqual(TEXT("Stepped transition requirement classifies support as satisfied"), Requirement.RootSupportStatus, ETerrainSteppedTransitionRootSupportStatus::SupportedByRootVerticalAccessCandidate);
	TestTrue(TEXT("Planned ascent protects the shifted VerticalAccess cell"), Requirement.RequiredTraversalCells.Contains(FIntVector(0, 1, 1)));
	TestTrue(TEXT("Planned ascent protects the directly-above landing cell"), Requirement.RequiredTraversalCells.Contains(FIntVector(0, 1, 2)));
	return true;
}

bool FLayoutTerrainSteppedSolveUsesPlannedAscentInsteadOfRawSeamTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_UsesPlannedAscentInsteadOfRawSeam"));
	for (const int32 RawStepHeight : {1, 2})
	{
		FLayoutRegionSolveRequest RootRequest;
		RootRequest.RegionDebugPath = TEXT("MajorShiftRoot");
		RootRequest.EffectiveSnapshotId = TEXT("MajorShiftSnapshot");
		RootRequest.FootprintSize = FIntPoint(3, 2);
		RootRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 0;
		RootRequest.PlannedCells = {
			MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Interior),
			MakePlannedCell(FIntVector(0, 1, 1), ELayoutCellIntent::Interior),
			MakePlannedCell(FIntVector(2, 1, 1), ELayoutCellIntent::VerticalAccess),
			MakePlannedCell(FIntVector(2, 1, 2), ELayoutCellIntent::Interior)
		};
		RootRequest.ModuleCatalog.Modules.Add(
			BuildSingleCellVerticalAccessSnapshot(Outer, TEXT("TerrainSteppedRemotePlannedAscentSupport")));
		RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
		AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
		AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 1, 0), 64 + RawStepHeight);
		AddAdjacencyStep(
			RootRequest.SteppedTerrainSupportMap,
			FIntVector(0, 0, 0),
			FIntVector(0, 1, 0),
			RawStepHeight);

		const FCompiledStructuralInputs StructuralInputs =
			BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));
		if (!TestEqual(
			FString::Printf(TEXT("Step height %d compiles one actual planned ascent"), RawStepHeight),
			StructuralInputs.RootTerrainSteppedTransitionRequirements.Num(),
			1))
		{
			continue;
		}

		const FTerrainSteppedTransitionRequirement& Requirement =
			StructuralInputs.RootTerrainSteppedTransitionRequirements[0];
		TestEqual(TEXT("Capability requirement starts at planned VA"),
			Requirement.FromCell, FIntVector(2, 1, 1));
		TestEqual(TEXT("Capability requirement ends at direct upper landing"),
			Requirement.ToCell, FIntVector(2, 1, 2));
		TestEqual(TEXT("Remote raw seam does not create unsupported local ascent"),
			StructuralInputs.RootUnsupportedTerrainSteppedTransitionRequirements.Num(), 0);
		TestTrue(TEXT("Frozen root capability accepts actual planned ascent"),
			StructuralInputs.bRootCanSatisfyTerrainSteppedTransitions);
	}
	return true;
}

bool FLayoutTerrainSteppedSolveTracksMissingRootVerticalAccessCandidatesTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(2, 1);
	RootRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 1;
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Interior)
	};

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), 67);
	AddAdjacencyStep(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), FIntVector(1, 0, 0), 3);
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 3;

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));

	if (!TestEqual(TEXT("Stepped structural inputs still compile one terrain transition requirement without root vertical-access support candidates"), StructuralInputs.RootTerrainSteppedTransitionRequirements.Num(), 1))
	{
		return false;
	}

	const FTerrainSteppedTransitionRequirement& Requirement = StructuralInputs.RootTerrainSteppedTransitionRequirements[0];
	TestEqual(TEXT("Planned ascent requirement preserves its single VerticalAccess cell without snapshot support"), Requirement.AdjacentVerticalAccessCells.Num(), 1);
	TestEqual(TEXT("Stepped transition requirement records no supporting root vertical-access candidates when the frozen snapshot set lacks them"), Requirement.SupportingRootVerticalAccessCandidateCells.Num(), 0);
	TestEqual(TEXT("Stepped transition requirement records no supporting root placement bundle ids when frozen root support is missing"), Requirement.SupportingRootVerticalAccessBundleIds.Num(), 0);
	TestEqual(TEXT("Stepped transition requirement classifies root-side support as missing a frozen vertical-access candidate"), Requirement.RootSupportStatus, ETerrainSteppedTransitionRootSupportStatus::MissingRootVerticalAccessCandidate);
	TestEqual(TEXT("Stepped transition requirement still preserves both transition traversal cells when root support is missing"), Requirement.RequiredTraversalCells.Num(), 2);
	TestFalse(TEXT("Stepped structural inputs report that the root cannot satisfy all compiled terrain-step transitions when frozen root support is missing"), StructuralInputs.bRootCanSatisfyTerrainSteppedTransitions);
	TestEqual(TEXT("Stepped structural inputs preserve one unsupported terrain-step transition when frozen root support is missing"), StructuralInputs.RootUnsupportedTerrainSteppedTransitionRequirements.Num(), 1);
	TestEqual(TEXT("Unsupported terrain-step transition preserves the missing-root-candidate status"), StructuralInputs.RootUnsupportedTerrainSteppedTransitionRequirements[0].RootSupportStatus, ETerrainSteppedTransitionRootSupportStatus::MissingRootVerticalAccessCandidate);
	TestEqual(TEXT("Stepped support-plan helper emits no accepted support plans when frozen root support is missing"), BuildRootTerrainSteppedSupportPlans(StructuralInputs).Num(), 0);
	return true;
}

bool FLayoutTerrainSteppedSolveTracksMissingAdjacentVerticalAccessIntentTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(2, 1);
	RootRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 1;
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Entry)
	};

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), 67);
	AddAdjacencyStep(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), FIntVector(1, 0, 0), 3);
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 3;

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));

	if (!TestEqual(TEXT("Stepped structural inputs compile one unsupported planned ascent without an upper landing"), StructuralInputs.RootTerrainSteppedTransitionRequirements.Num(), 1))
	{
		return false;
	}

	const FTerrainSteppedTransitionRequirement& Requirement = StructuralInputs.RootTerrainSteppedTransitionRequirements[0];
	TestEqual(TEXT("Unsupported ascent preserves its planned VerticalAccess cell"), Requirement.AdjacentVerticalAccessCells.Num(), 1);
	TestEqual(TEXT("Unsupported ascent has no supporting root vertical-access candidates without an upper landing"), Requirement.SupportingRootVerticalAccessCandidateCells.Num(), 0);
	TestEqual(TEXT("Unsupported ascent has no supporting root placement bundle ids without an upper landing"), Requirement.SupportingRootVerticalAccessBundleIds.Num(), 0);
	TestEqual(TEXT("Unsupported ascent classifies the missing upper landing contract"), Requirement.RootSupportStatus, ETerrainSteppedTransitionRootSupportStatus::MissingAdjacentVerticalAccessIntent);
	TestEqual(TEXT("Unsupported ascent preserves its lower and expected upper traversal cells"), Requirement.RequiredTraversalCells.Num(), 2);
	TestFalse(TEXT("Structural inputs reject a planned ascent without an upper landing"), StructuralInputs.bRootCanSatisfyTerrainSteppedTransitions);
	TestEqual(TEXT("Structural inputs preserve one unsupported planned ascent"), StructuralInputs.RootUnsupportedTerrainSteppedTransitionRequirements.Num(), 1);
	TestEqual(TEXT("Unsupported terrain-step transition preserves the missing-intent status"), StructuralInputs.RootUnsupportedTerrainSteppedTransitionRequirements[0].RootSupportStatus, ETerrainSteppedTransitionRootSupportStatus::MissingAdjacentVerticalAccessIntent);
	TestEqual(TEXT("Stepped support-plan helper emits no accepted support plans when no adjacent planned vertical-access intent exists"), BuildRootTerrainSteppedSupportPlans(StructuralInputs).Num(), 0);
	return true;
}

bool FLayoutTerrainSteppedSolveBuildsDeterministicRejectionSummaryTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_BuildsDeterministicRejectionSummary"));
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(3, 1);
	RootRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 1;
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(2, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(1, 0, 1), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(2, 0, 1), ELayoutCellIntent::Interior)
	};
	RootRequest.ModuleCatalog.Modules.Add(
		BuildSingleCellVerticalAccessSnapshot(Outer, TEXT("TerrainSteppedDeterministicSupport")));

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), 67);
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(2, 0, 0), 70);
	AddAdjacencyStep(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), FIntVector(1, 0, 0), 3);
	AddAdjacencyStep(RootRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), FIntVector(2, 0, 0), 3);
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 3;

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));
	const FTerrainSteppedRejectionSummary RejectionSummary = BuildRootTerrainSteppedRejectionSummary(StructuralInputs);

	TestFalse(TEXT("Stepped rejection summary reports no unsupported ascent when every planned VA has upper and module support"), RejectionSummary.bHasUnsupportedTransition);
	TestTrue(TEXT("Stepped rejection summary stays empty when all transitions are covered"), RejectionSummary.FailureReason.IsEmpty());
	return true;
}

bool FLayoutTerrainSteppedSolveBuildsRouteConstraintsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_BuildsRouteConstraints"));

	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(2, 1);
	RootRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 1;
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Interior)
	};
	RootRequest.ModuleCatalog.Modules.Add(BuildSingleCellVerticalAccessSnapshot(Outer, TEXT("TerrainSteppedRouteSupport")));

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), 67);
	AddAdjacencyStep(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), FIntVector(1, 0, 0), 3);
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 3;

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));
	const TArray<FLayoutRouteConstraintRecord> RouteConstraints = BuildRootTerrainSteppedRouteConstraints(StructuralInputs);

	TestEqual(TEXT("Planned vertical ascents emit no raw horizontal seam route constraints"), RouteConstraints.Num(), 0);
	return true;
}

bool FLayoutTerrainSteppedSolveBuildsSupportPlansTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_BuildsSupportPlans"));

	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(2, 1);
	RootRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 1;
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Interior)
	};
	RootRequest.ModuleCatalog.Modules.Add(BuildSingleCellVerticalAccessSnapshot(Outer, TEXT("TerrainSteppedPlanSupport")));

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), 67);
	AddAdjacencyStep(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), FIntVector(1, 0, 0), 3);
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 3;

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));
	const TArray<FTerrainSteppedSupportPlan> SupportPlans = BuildRootTerrainSteppedSupportPlans(StructuralInputs);

	if (!TestEqual(TEXT("Stepped support-plan helper emits one plan for the supported terrain step"), SupportPlans.Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("Stepped support plan preserves the supported lower cell"), SupportPlans[0].TransitionRequirement.FromCell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Stepped support plan preserves the supported upper landing"), SupportPlans[0].TransitionRequirement.ToCell, FIntVector(0, 0, 1));
	TestEqual(TEXT("Stepped support plan preserves the selected supporting VerticalAccess cell"), SupportPlans[0].SelectedSupportingRootVerticalAccessCell, FIntVector(0, 0, 0));
	TestTrue(TEXT("Stepped support plan selects a non-empty root bundle id"), SupportPlans[0].SelectedRootVerticalAccessBundleId != NAME_None);
	return true;
}

bool FLayoutTerrainSteppedSolveSelectsDeterministicSupportingVerticalAccessCellTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_SelectsDeterministicSupportingVerticalAccessCell"));

	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(2, 1);
	RootRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 1;
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Interior)
	};
	RootRequest.ModuleCatalog.Modules.Add(BuildSingleCellVerticalAccessSnapshot(Outer, TEXT("TerrainSteppedDeterministicSupportCell")));

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), 67);
	AddAdjacencyStep(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), FIntVector(1, 0, 0), 3);
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 3;

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));
	const TArray<FTerrainSteppedSupportPlan> SupportPlans = BuildRootTerrainSteppedSupportPlans(StructuralInputs);

	if (!TestEqual(TEXT("Stepped support-plan helper emits one deterministic plan for one planned ascent"), SupportPlans.Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("Stepped support plan selects its planned VerticalAccess cell"), SupportPlans[0].SelectedSupportingRootVerticalAccessCell, FIntVector(0, 0, 0));
	return true;
}

bool FLayoutTerrainSteppedSolveKeepsOriginSupportCellWhenTiePersistsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_KeepsOriginSupportCellWhenTiePersists"));

	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(2, 1);
	RootRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 1;
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Interior)
	};
	RootRequest.ModuleCatalog.Modules.Add(
		BuildSingleCellJunctionVerticalAccessSnapshot(
			Outer,
			TEXT("TerrainSteppedOriginTieSupportCell")));

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), 67);
	AddAdjacencyStep(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), FIntVector(1, 0, 0), 3);
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 3;

	const FCompiledStructuralInputs StructuralInputs =
		BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));
	const TArray<FTerrainSteppedSupportPlan> SupportPlans =
		BuildRootTerrainSteppedSupportPlans(StructuralInputs);

	if (!TestEqual(
		TEXT("Stepped support-plan helper emits one plan for one planned junction ascent"),
		SupportPlans.Num(),
		1))
	{
		return false;
	}

	TestEqual(
		TEXT("Planned junction ascent keeps its origin support cell"),
		SupportPlans[0].SelectedSupportingRootVerticalAccessCell,
		FIntVector(0, 0, 0));
	return true;
}

bool FLayoutTerrainSteppedSolveBuildsInsertionPlansTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_BuildsInsertionPlans"));

	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(2, 1);
	RootRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 1;
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Interior)
	};
	RootRequest.ModuleCatalog.Modules.Add(BuildSingleCellVerticalAccessSnapshot(Outer, TEXT("TerrainSteppedInsertionPlanSupport")));

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), 67);
	AddAdjacencyStep(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), FIntVector(1, 0, 0), 3);
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 3;

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));
	const TArray<FTerrainSteppedInsertionPlan> InsertionPlans = BuildRootTerrainSteppedInsertionPlans(StructuralInputs);

	if (!TestEqual(TEXT("Stepped insertion-plan helper emits one anchored insertion for one supported terrain step"), InsertionPlans.Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("Stepped insertion plan preserves the selected proving VerticalAccess cell as its bundle anchor"), InsertionPlans[0].BundleAnchorCell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Stepped insertion plan preserves the selected support-plan bundle id"), InsertionPlans[0].SelectedRootPlacementBundle.BundleId, InsertionPlans[0].SupportPlan.SelectedRootVerticalAccessBundleId);
	TestTrue(TEXT("Stepped insertion plan preserves that the selected bundle supports root VerticalAccess"), InsertionPlans[0].SelectedRootPlacementBundle.bSupportsRootVerticalAccess);
	return true;
}

bool FLayoutTerrainSteppedSolveIncludesEveryPlannedAscentInsertionTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_IncludesEveryPlannedAscentInsertion"));

	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.FootprintSize = FIntPoint(3, 1);
	RootRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 1;
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(2, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(1, 0, 1), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(2, 0, 1), ELayoutCellIntent::Interior)
	};
	RootRequest.ModuleCatalog.Modules.Add(BuildSingleCellVerticalAccessSnapshot(Outer, TEXT("TerrainSteppedCountCapSupport")));

	RootRequest.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	RootRequest.ProfileSnapshot.VerticalAccessCount = 1;

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), 67);
	AddSupportSample(RootRequest.SteppedTerrainSupportMap, FIntVector(2, 0, 0), 70);
	AddAdjacencyStep(RootRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), FIntVector(1, 0, 0), 3);
	AddAdjacencyStep(RootRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), FIntVector(2, 0, 0), 3);
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 3;

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));
	const TArray<FTerrainSteppedInsertionPlan> InsertionPlans = BuildRootTerrainSteppedInsertionPlans(StructuralInputs);

	TestEqual(TEXT("Every planned ascent receives an insertion plan despite lower profile exact count"), InsertionPlans.Num(), 3);
	TestTrue(TEXT("Insertion plans preserve every planned VerticalAccess anchor"),
		InsertionPlans.ContainsByPredicate([](const FTerrainSteppedInsertionPlan& Plan) { return Plan.BundleAnchorCell == FIntVector(0, 0, 0); })
		&& InsertionPlans.ContainsByPredicate([](const FTerrainSteppedInsertionPlan& Plan) { return Plan.BundleAnchorCell == FIntVector(1, 0, 0); })
		&& InsertionPlans.ContainsByPredicate([](const FTerrainSteppedInsertionPlan& Plan) { return Plan.BundleAnchorCell == FIntVector(2, 0, 0); }));
	return true;
}

bool FLayoutTerrainSteppedSolveBuildsPreparedSolveContractTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_BuildsPreparedSolveContract"));

	FLayoutRegionSolveRequest SupportedRequest;
	SupportedRequest.RegionDebugPath = TEXT("Root");
	SupportedRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !SupportedRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	SupportedRequest.FootprintSize = FIntPoint(2, 1);
	SupportedRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 1;
	SupportedRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Interior)
	};
	SupportedRequest.ModuleCatalog.Modules.Add(BuildSingleCellVerticalAccessSnapshot(Outer, TEXT("TerrainSteppedPreparedContractSupport")));
	SupportedRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	AddSupportSample(SupportedRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
	AddSupportSample(SupportedRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), 67);
	AddAdjacencyStep(SupportedRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), FIntVector(1, 0, 0), 3);
	SupportedRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 3;

	const FCompiledStructuralInputs SupportedInputs = BuildCompiledStructuralInputs(BuildSolveContext(SupportedRequest));
	const FTerrainSteppedPreparedSolveContract SupportedContract = BuildRootTerrainSteppedPreparedSolveContract(SupportedInputs);

	TestTrue(TEXT("Prepared stepped contract reports that the root can satisfy all supported terrain transitions"), SupportedContract.bRootCanSatisfyTerrainSteppedTransitions);
	TestFalse(TEXT("Prepared stepped contract keeps the rejection summary empty when every transition is supported"), SupportedContract.RejectionSummary.bHasUnsupportedTransition);
	TestEqual(TEXT("Prepared stepped contract preserves one accepted support plan for one supported terrain step"), SupportedContract.SupportPlans.Num(), 1);
	TestEqual(TEXT("Prepared stepped contract preserves the selected supporting VerticalAccess cell on the accepted support plan"), SupportedContract.SupportPlans[0].SelectedSupportingRootVerticalAccessCell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Prepared stepped contract preserves one anchored insertion plan for one supported terrain step"), SupportedContract.InsertionPlans.Num(), 1);
	TestEqual(TEXT("Prepared stepped contract preserves the insertion-plan anchor cell"), SupportedContract.InsertionPlans[0].BundleAnchorCell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Prepared stepped contract emits no raw horizontal seam route constraints"), SupportedContract.RouteConstraints.Num(), 0);

	FLayoutRegionSolveRequest UnsupportedRequest;
	UnsupportedRequest.RegionDebugPath = TEXT("Root");
	UnsupportedRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !UnsupportedRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	UnsupportedRequest.FootprintSize = FIntPoint(2, 1);
	UnsupportedRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 1;
	UnsupportedRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::VerticalAccess)
	};
	UnsupportedRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	AddSupportSample(UnsupportedRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
	AddSupportSample(UnsupportedRequest.SteppedTerrainSupportMap, FIntVector(1, 0, 0), 67);
	AddAdjacencyStep(UnsupportedRequest.SteppedTerrainSupportMap, FIntVector(0, 0, 0), FIntVector(1, 0, 0), 3);
	UnsupportedRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 3;

	const FCompiledStructuralInputs UnsupportedInputs = BuildCompiledStructuralInputs(BuildSolveContext(UnsupportedRequest));
	const FTerrainSteppedPreparedSolveContract UnsupportedContract = BuildRootTerrainSteppedPreparedSolveContract(UnsupportedInputs);

	TestFalse(TEXT("Prepared stepped contract rejects a planned VA without an upper landing"), UnsupportedContract.bRootCanSatisfyTerrainSteppedTransitions);
	TestTrue(TEXT("Prepared stepped contract preserves rejection summary for missing upper landing"), UnsupportedContract.RejectionSummary.bHasUnsupportedTransition);
	TestEqual(TEXT("Prepared stepped contract emits no support plan for unsupported ascent"), UnsupportedContract.SupportPlans.Num(), 0);
	TestEqual(TEXT("Prepared stepped contract emits no insertion plan for unsupported ascent"), UnsupportedContract.InsertionPlans.Num(), 0);
	TestEqual(TEXT("Prepared stepped contract emits no raw horizontal seam route constraints"), UnsupportedContract.RouteConstraints.Num(), 0);
	return true;
}

bool FLayoutTerrainSteppedSolveSkipsGeneratedInsertionForFlexibleHostGroupTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_SkipsGeneratedInsertionForFlexibleHostGroup"));

	FLayoutRegionSolveRequest Request;
	Request.RegionDebugPath = TEXT("Root");
	Request.EffectiveSnapshotId = TEXT("RootSnapshot");
	Request.FootprintSize = FIntPoint(2, 1);
	Request.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 1;
	Request.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Interior)
	};
	Request.ModuleCatalog.Modules.Add(BuildSingleCellVerticalAccessSnapshot(Outer, TEXT("TerrainSteppedFlexibleHostSupport")));
	Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	AddSupportSample(Request.SteppedTerrainSupportMap, FIntVector(0, 0, 0), 64);
	AddSupportSample(Request.SteppedTerrainSupportMap, FIntVector(1, 0, 0), 67);
	AddAdjacencyStep(Request.SteppedTerrainSupportMap, FIntVector(0, 0, 0), FIntVector(1, 0, 0), 3);
	Request.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 3;

	FLayoutVerticalAccessHostGroup& HostGroup = Request.VerticalAccessHostGroups.AddDefaulted_GetRef();
	HostGroup.GroupId = TEXT("FlexibleHost");
	HostGroup.DeckCell = FIntVector(0, 0, 1);
	FLayoutVerticalAccessHostOption& HostOption = HostGroup.Options.AddDefaulted_GetRef();
	HostOption.LowerCell = FIntVector(0, 0, 0);
	HostOption.UpperCell = FIntVector(0, 0, 1);
	HostOption.Tier = ELayoutVerticalAccessHostTier::Constrained;

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(Request));
	const FTerrainSteppedPreparedSolveContract PreparedContract = BuildRootTerrainSteppedPreparedSolveContract(StructuralInputs);

	TestEqual(TEXT("Flexible host group keeps one structural support plan for diagnostics"), PreparedContract.SupportPlans.Num(), 1);
	TestEqual(TEXT("Flexible host group does not emit a competing exact insertion"), PreparedContract.InsertionPlans.Num(), 0);
	return true;
}

bool FLayoutTerrainSteppedBoundaryIntermediatePolicyUsesSharedDeckProvenanceTest::RunTest(const FString& Parameters)
{
	auto BuildContext = [](const ELayoutWorldBindingPlacementKind PlacementKind, const bool bBridge, const int32 SpatialZ, const int32 ModuleLevel)
	{
		FLayoutRegionSolveRequest Request;
		Request.RegionDebugPath = TEXT("SteppedRoutePolicy");
		Request.RootPlacementKind = PlacementKind;
		Request.FootprintSize = FIntPoint(3, 1);
		Request.ProfileSnapshot.LevelCount = 2;
		Request.ProfileSnapshot.bSupportsSteppedTerrainSolve = true;
		Request.ProfileSnapshot.bEnableTerrainSeams = false;
		Request.bHasSelectedModePlan = true;
		Request.SelectedModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	Request.SelectedModePlan.bUsesSteppedTerrainTopology = true;
		Request.bHasFinalizedSteppedTerrainIntents = true;
		FLayoutPlannedCell& PlannedCell = Request.PlannedCells.AddDefaulted_GetRef();
		PlannedCell.Cell = FIntVector(1, 0, SpatialZ);
		PlannedCell.Intent = ELayoutCellIntent::Boundary;
		PlannedCell.ModuleLevelIndex = ModuleLevel;
		PlannedCell.bIsBridgeCell = bBridge;
		FLayoutFrozenTerrainStageCellRecord& Stage =
			Request.PrecomputedFrozenTerrainContract.StageMap.AddDefaulted_GetRef();
		Stage.FootprintCellXY = FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y);
		Stage.TerrainStageIndex = bBridge ? 1 : 0;
		Request.PrecomputedFrozenTerrainContract.ActiveCells.AddDefaulted_GetRef().Cell = PlannedCell.Cell;
		return LayoutProfileSolverInternal::BuildSolveContextFromRequestForTests(Request);
	};

	const LayoutProfileSolverInternal::FSolveContext RootBridgeContext = BuildContext(
		ELayoutWorldBindingPlacementKind::OrdinaryRoot,
		true,
		1,
		1);
	const LayoutProfileSolverInternal::FSolveContext ContinuationBridgeContext = BuildContext(
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		true,
		1,
		1);
	const LayoutProfileSolverInternal::FSolveContext ShiftedCellContext = BuildContext(
		ELayoutWorldBindingPlacementKind::OrdinaryRoot,
		false,
		1,
		0);
	const LayoutProfileSolverInternal::FSolveContext OrdinaryBoundaryContext = BuildContext(
		ELayoutWorldBindingPlacementKind::OrdinaryRoot,
		false,
		1,
		1);

	const LayoutProfileSolverInternal::FLayoutFinalizedCellViewRecord* const RootBridgeView =
		RootBridgeContext.FinalizedCellsByPhysicalCell.Find(FIntVector(1, 0, 1));
	const LayoutProfileSolverInternal::FLayoutFinalizedCellViewRecord* const ShiftedCellView =
		ShiftedCellContext.FinalizedCellsByPhysicalCell.Find(FIntVector(1, 0, 1));
	TestTrue(TEXT("Finalized view indexes generated bridge physical cell"), RootBridgeView != nullptr);
	TestTrue(TEXT("Generated bridge has no authored source cell"), RootBridgeView != nullptr && !RootBridgeView->bHasAuthoredSource);
	TestTrue(TEXT("Generated bridge retains frozen terrain stage"), RootBridgeView != nullptr && RootBridgeView->bHasTerrainStage);
	TestTrue(TEXT("Finalized view indexes shifted authored physical cell"), ShiftedCellView != nullptr);
	TestTrue(TEXT("Shifted authored cell retains authored source level"),
		ShiftedCellView != nullptr
		&& ShiftedCellView->bHasAuthoredSource
		&& ShiftedCellView->SourceAuthoredCell == FIntVector(1, 0, 0));

	TestTrue(TEXT("Root stepped bridge Boundary cell is eligible as an intermediate route step"),
		LayoutProfileSolverInternal::ShouldAllowBoundaryCellsAsIntermediateRouteStepForTests(
			RootBridgeContext,
			FIntVector(1, 0, 1)));
	TestTrue(TEXT("Continuation uses the same stepped bridge Boundary route policy"),
		LayoutProfileSolverInternal::ShouldAllowBoundaryCellsAsIntermediateRouteStepForTests(
			ContinuationBridgeContext,
			FIntVector(1, 0, 1)));
	TestTrue(TEXT("Spatially shifted authored cell uses the shared stepped Boundary route policy"),
		LayoutProfileSolverInternal::ShouldAllowBoundaryCellsAsIntermediateRouteStepForTests(
			ShiftedCellContext,
			FIntVector(1, 0, 1)));
	TestFalse(TEXT("Ordinary unreserved Boundary shell cell remains blocked"),
		LayoutProfileSolverInternal::ShouldAllowBoundaryCellsAsIntermediateRouteStepForTests(
			OrdinaryBoundaryContext,
			FIntVector(1, 0, 1)));
	return true;
}

bool FLayoutTerrainSteppedPreparedRouteUsesGeneratedBoundaryIntermediateTest::RunTest(const FString& Parameters)
{
	for (const ELayoutWorldBindingPlacementKind PlacementKind : {
		ELayoutWorldBindingPlacementKind::OrdinaryRoot,
		ELayoutWorldBindingPlacementKind::BridgeContinuation })
	{
		UObject* Outer = CreatePackage(*FString::Printf(
			TEXT("/Temp/LayoutTerrainSteppedSolve_PreparedRoute_%d"),
			static_cast<int32>(PlacementKind)));
		FLayoutRegionSolveRequest Request = BuildSteppedBoundaryRouteRequest(Outer, PlacementKind, true);
		LayoutProfileSolverInternal::FSolveContext Context;
		const bool bPrepared = LayoutProfileSolverInternal::TryPrepareRequestSolveContextThroughRouteDomainStage(
			Request,
			Context);
		TestTrue(
			PlacementKind == ELayoutWorldBindingPlacementKind::OrdinaryRoot
				? TEXT("Root stepped route accepts generated Boundary intermediate")
				: TEXT("Continuation stepped route accepts generated Boundary intermediate"),
			bPrepared);
		if (!bPrepared && !Context.Result.FailureReason.IsEmpty())
		{
			AddError(Context.Result.FailureReason);
		}
	}

	UObject* OrdinaryOuter = CreatePackage(TEXT("/Temp/LayoutTerrainSteppedSolve_PreparedRoute_OrdinaryBoundary"));
	FLayoutRegionSolveRequest OrdinaryRequest = BuildSteppedBoundaryRouteRequest(
		OrdinaryOuter,
		ELayoutWorldBindingPlacementKind::OrdinaryRoot,
		false);
	LayoutProfileSolverInternal::FSolveContext OrdinaryContext;
	const bool bOrdinaryPrepared = LayoutProfileSolverInternal::TryPrepareRequestSolveContextThroughRouteDomainStage(
		OrdinaryRequest,
		OrdinaryContext);
	TestTrue(
		TEXT("Ordinary Boundary shell with a domain-proven two-face traversal candidate remains a route fallback"),
		bOrdinaryPrepared);
	if (!bOrdinaryPrepared)
	{
		AddError(OrdinaryContext.Result.FailureReason);
	}
	return true;
}

bool FLayoutTerrainSteppedSolveStagesDemandActivationUntilAscentFrontierUnlockTest::RunTest(const FString& Parameters)
{
	FNegotiationDemandPlan DeferredDemand;
	DeferredDemand.TerrainStageEligibility = ETerrainStageEligibility::Deferred;
	DeferredDemand.bRequiresDeferredActivation = true;
	DeferredDemand.bTerrainStageCanActivateEarlyIfUnlocksAscent = true;
	DeferredDemand.bTerrainStageActivatesOnCurrentFrontier = false;
	DeferredDemand.UnlockingAscentFrontierId.Reset();
	TestFalse(
		TEXT("Deferred demand stays inactive before any ascent frontier unlock is published"),
		IsNegotiationDemandActiveForCurrentTerrainStage(DeferredDemand));

	DeferredDemand.bTerrainStageActivatesOnCurrentFrontier = true;
	DeferredDemand.UnlockingAscentFrontierId = 17;
	TestTrue(
		TEXT("Deferred demand activates once the matching ascent frontier unlock is published"),
		IsNegotiationDemandActiveForCurrentTerrainStage(DeferredDemand));

	FNegotiationDemandPlan AlwaysActiveDemand;
	AlwaysActiveDemand.TerrainStageEligibility = ETerrainStageEligibility::Active;
	AlwaysActiveDemand.bRequiresDeferredActivation = false;
	TestTrue(
		TEXT("Non-gated authored demand stays active without a frontier unlock"),
		IsNegotiationDemandActiveForCurrentTerrainStage(AlwaysActiveDemand));
	return true;
}

// ---- S3: Solver places bridge cells (simple non-stepped path) ----

// ---- N4.93: Multi-level TopBridge solver placements ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedMultiLevelTopBridgeSolverPlacementsTest,
	"PorismExtension.Layout.Solver.TerrainStepped.MultiLevelTopBridgeSolverPlacements",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSteppedMultiLevelTopBridgeSolverPlacementsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(16, 16, 16);

	// Separate ordinary cells from selected VerticalAccess cells so this topology fixture honors authored VA admission.
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("TMultiBridge_Template"), CellSize);
	const TArray<FLayoutFaceRule> FaceRules = {
		MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
	};
	ULayoutModuleAsset* InteriorModule = CreateModule(
		Outer, TEXT("TMultiBridge_InteriorModule"), Template,
		{ELayoutCellIntent::Interior}, FaceRules);
	ULayoutModuleAsset* VerticalAccessModule = CreateModule(
		Outer, TEXT("TMultiBridge_VerticalAccessModule"), Template,
		{ELayoutCellIntent::VerticalAccess}, FaceRules);

	FLayoutModuleCatalog ModuleCatalog;
	ModuleCatalog.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(InteriorModule, 1));
	ModuleCatalog.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(VerticalAccessModule, 1));

	// Multi-level shift-UP with LevelCount=3, 2 columns.
	// Lower column (0,0) stage 0: Z=0,1,2 normal + TopBridge at Z=3.
	// Higher column (1,0) stage 1: Z-shifted to Z=1,2,3 + bridge at Z=0.
	// Total: 8 planned cells.
	FLayoutRegionSolveRequest Request;
	Request.Seed = 99;
	Request.RegionDebugPath = TEXT("MultiBridgeRoot");
	Request.FootprintSize = FIntPoint(2, 1);
	Request.ProfileSnapshot.LevelCount = 3;
	Request.ProfileSnapshot.MinimumFootprintInCells = FIntPoint(2, 1);
	Request.ProfileSnapshot.MaximumFootprintInCells = FIntPoint(2, 1);
	Request.ModuleCatalog = ModuleCatalog;
	Request.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(0, 0, 2), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(0, 0, 3), ELayoutCellIntent::Interior), // TopBridge
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::VerticalAccess), // bridge
		MakePlannedCell(FIntVector(1, 0, 1), ELayoutCellIntent::Interior), // shifted Z=0
		MakePlannedCell(FIntVector(1, 0, 2), ELayoutCellIntent::Interior), // shifted Z=1
		MakePlannedCell(FIntVector(1, 0, 3), ELayoutCellIntent::Interior), // shifted Z=2
	};

	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionTree(Request);
	const FLayoutSolveResult& SolveResult = ScheduleResult.MergedSolveResult;

	TestTrue(TEXT("Multi-level solver succeeds with bridge cells"), SolveResult.bSucceeded);
	if (!SolveResult.bSucceeded)
	{
		AddError(FString::Printf(TEXT("Solve failed: %s"), *SolveResult.FailureReason));
		return false;
	}

	const int32 PlacementCount = SolveResult.Placements.Num();
	TestTrue(FString::Printf(TEXT("Solver places modules at all 8 cells (got %d)"), PlacementCount), PlacementCount >= 8);

	// Verify specific bridge cell positions.
	bool bFoundTopBridgeZ3 = false;
	bool bFoundShiftedBridgeZ0 = false;
	for (const FLayoutPlacedModule& P : SolveResult.Placements)
	{
		if (P.Cell == FIntVector(0, 0, 3)) bFoundTopBridgeZ3 = true;
		if (P.Cell == FIntVector(1, 0, 0)) bFoundShiftedBridgeZ0 = true;
	}
	TestTrue(TEXT("TopBridge cell at Z=3 on lower column has a placement"), bFoundTopBridgeZ3);
	TestTrue(TEXT("Bridge cell at Z=0 on shifted higher column has a placement"), bFoundShiftedBridgeZ0);

	// Verify no residuals at bridge cell Z levels.
	for (const FLayoutResidualCellRecord& R : SolveResult.ResidualUnoccupiedCells)
	{
		if (R.Cell.Z >= 3)
		{
			AddError(FString::Printf(TEXT("Unexpected residual at bridge level Z=%d cell=%s"), R.Cell.Z, *R.Cell.ToString()));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedVerticalRegionPreservesTopBridgeZoneTest,
	"PorismExtension.Layout.Solver.TerrainStepped.VerticalRegionPreservesTopBridgeZone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSteppedVerticalRegionPreservesTopBridgeZoneTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(16, 16, 16);
	const TArray<FLayoutFaceRule> SolidFaceRules = {
		MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
	};
	ULayoutModuleAsset* InteriorModule = CreateModule(
		Outer,
		TEXT("TTopBridgeInterior_Module"),
		CreateTemplate(Outer, TEXT("TTopBridgeInterior_Template"), CellSize),
		{ELayoutCellIntent::Interior},
		SolidFaceRules);
	ULayoutModuleAsset* CornerBoundaryModule = CreateModule(
		Outer,
		TEXT("TTopBridgeCornerBoundary_Module"),
		CreateTemplate(Outer, TEXT("TTopBridgeCornerBoundary_Template"), CellSize),
		{ELayoutCellIntent::Boundary},
		SolidFaceRules);

	FLayoutRegionSolveRequest Request;
	Request.Seed = 179;
	Request.RegionDebugPath = TEXT("TopBridgeLocalZone");
	Request.FootprintSize = FIntPoint(3, 3);
	Request.ProfileSnapshot.LevelCount = 4;
	Request.ProfileSnapshot.MinimumFootprintInCells = FIntPoint(3, 3);
	Request.ProfileSnapshot.MaximumFootprintInCells = FIntPoint(3, 3);
	Request.ModuleCatalog.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(InteriorModule, 1));
	Request.ModuleCatalog.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(
		CornerBoundaryModule,
		1,
		ELayoutPlacementZone::Corner));
	for (int32 Z = 0; Z < 3; ++Z)
	{
		Request.PlannedCells.Add(MakePlannedCell(FIntVector(1, 1, Z), ELayoutCellIntent::Interior));
	}

	FLayoutPlannedCell& TopBridge = Request.PlannedCells.AddDefaulted_GetRef();
	TopBridge.Cell = FIntVector(1, 1, 3);
	TopBridge.Intent = ELayoutCellIntent::Boundary;
	TopBridge.bIsBridgeCell = true;
	TopBridge.PlacementZone = ELayoutPlacementZone::Corner;

	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionTree(Request);
	const FLayoutSolveResult& SolveResult = ScheduleResult.MergedSolveResult;
	TestTrue(TEXT("Vertical region solve preserves the local TopBridge placement zone"), SolveResult.bSucceeded);
	if (!SolveResult.bSucceeded)
	{
		AddError(FString::Printf(TEXT("Solve failed: %s"), *SolveResult.FailureReason));
		return false;
	}

	const bool bPlacedTopBridge = SolveResult.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell == FIntVector(1, 1, 3);
	});
	TestTrue(TEXT("Corner TopBridge at globally interior XY has a placement"), bPlacedTopBridge);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedBottomBridgeDomainsMatchFlatZonesTest,
	"PorismExtension.Layout.Solver.TerrainStepped.BottomBridgeDomainsMatchFlatZones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSteppedBottomBridgeDomainsMatchFlatZonesTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FGameplayTagContainer SolidTags = MakeTags({LayoutGameplayTags::FaceSolid});
	auto MakeSolidFace = [&SolidTags](const ELayoutFaceDirection Direction)
	{
		return MakeConnectionFaceRule(
			Direction,
			LayoutGameplayTags::FaceSolid,
			SolidTags,
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor);
	};
	const TArray<FLayoutFaceRule> FaceRules = {
		MakeSolidFace(ELayoutFaceDirection::PosX),
		MakeSolidFace(ELayoutFaceDirection::NegX),
		MakeSolidFace(ELayoutFaceDirection::PosY),
		MakeSolidFace(ELayoutFaceDirection::NegY),
		MakeSolidFace(ELayoutFaceDirection::PosZ),
		MakeSolidFace(ELayoutFaceDirection::NegZ)
	};

	FLayoutModuleCatalog Catalog;
	for (const ELayoutPlacementZone Zone : {ELayoutPlacementZone::Interior, ELayoutPlacementZone::Edge, ELayoutPlacementZone::Corner})
	{
		ULayoutModuleAsset* Module = CreateModule(
			Outer,
			*FString::Printf(TEXT("TBridgeZone_%d_Module"), static_cast<int32>(Zone)),
			CreateTemplate(Outer, *FString::Printf(TEXT("TBridgeZone_%d_Template"), static_cast<int32>(Zone)), FIntVector(16, 16, 16)),
			{ELayoutCellIntent::Boundary},
			FaceRules);
		FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
		Snapshot.PlacementZone = Zone;
		Catalog.Modules.Add(MoveTemp(Snapshot));
	}

	auto BuildSnapshot = [&Catalog](const FLayoutPlannedCell& PlannedCell)
	{
		FLayoutRegionSolveRequest Request;
		Request.Seed = 92;
		Request.FootprintSize = FIntPoint(5, 3);
		Request.ProfileSnapshot.LevelCount = 1;
		Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;
		Request.ModuleCatalog = Catalog;
		Request.PlannedCells = {PlannedCell};
		return FLayoutProfileSolver::BuildIndexedDomainSnapshot(Request);
	};
	auto CollectIds = [](const FLayoutIndexedDomainSnapshot& Snapshot)
	{
		TSet<FLayoutId> Ids;
		if (!Snapshot.CellDomains.IsEmpty())
		{
			for (const int32 CandidateIndex : Snapshot.CellDomains[0].OrderedCandidateIndices)
			{
				if (Snapshot.Candidates.IsValidIndex(CandidateIndex) && !Snapshot.Candidates[CandidateIndex].bEmpty)
				{
					Ids.Add(Snapshot.Candidates[CandidateIndex].ModuleSnapshotId);
				}
			}
		}
		return Ids;
	};

	struct FZoneCase
	{
		ELayoutPlacementZone Zone;
		FIntVector BridgeCell;
		FIntVector FlatCell;
	};
	const FZoneCase Cases[] = {
		{ELayoutPlacementZone::Interior, FIntVector(2, 1, 0), FIntVector(2, 1, 0)},
		{ELayoutPlacementZone::Edge, FIntVector(3, 1, 0), FIntVector(3, 0, 0)},
		{ELayoutPlacementZone::Corner, FIntVector(4, 1, 0), FIntVector(4, 0, 0)}
	};
	for (const FZoneCase& TestCase : Cases)
	{
		FLayoutPlannedCell Bridge;
		Bridge.Cell = TestCase.BridgeCell;
		Bridge.Intent = ELayoutCellIntent::Boundary;
		Bridge.ModuleLevelIndex = 0;
		Bridge.bIsBridgeCell = true;
		Bridge.PlacementZone = TestCase.Zone;
		FLayoutPlannedCell Flat = Bridge;
		Flat.Cell = TestCase.FlatCell;
		Flat.bIsBridgeCell = false;

		const FLayoutIndexedDomainSnapshot BridgeSnapshot = BuildSnapshot(Bridge);
		const FLayoutIndexedDomainSnapshot FlatSnapshot = BuildSnapshot(Flat);
		TestTrue(TEXT("Bridge indexed domain builds"), BridgeSnapshot.bSucceeded);
		TestTrue(TEXT("Flat indexed domain builds"), FlatSnapshot.bSucceeded);
		const TSet<FLayoutId> BridgeIds = CollectIds(BridgeSnapshot);
		const TSet<FLayoutId> FlatIds = CollectIds(FlatSnapshot);
		TestTrue(
			TEXT("Bridge local zone admits the same module domain as equivalent flat zone"),
			BridgeIds.Num() == FlatIds.Num() && BridgeIds.Includes(FlatIds));
		bool bRowsMatch = BridgeSnapshot.CompatibilityRows.Num() == FlatSnapshot.CompatibilityRows.Num();
		for (int32 RowIndex = 0; bRowsMatch && RowIndex < BridgeSnapshot.CompatibilityRows.Num(); ++RowIndex)
		{
			const FLayoutIndexedCandidateCompatibility& BridgeRow = BridgeSnapshot.CompatibilityRows[RowIndex];
			const FLayoutIndexedCandidateCompatibility& FlatRow = FlatSnapshot.CompatibilityRows[RowIndex];
			bRowsMatch = BridgeRow.SourceCandidateIndex == FlatRow.SourceCandidateIndex
				&& BridgeRow.Direction == FlatRow.Direction
				&& BridgeRow.CompatibleCandidateBits == FlatRow.CompatibleCandidateBits;
		}
		TestTrue(TEXT("Bridge and flat compatibility rows match"), bRowsMatch);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedStrictPosZRejectsOccupiedUpperTest,
	"PorismExtension.Layout.Solver.TerrainStepped.StrictPosZRejectsOccupiedUpper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSteppedStrictPosZRejectsOccupiedUpperTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
	const FGameplayTagContainer SolidTags = MakeTags({LayoutGameplayTags::FaceSolid});
	TArray<FLayoutFaceRule> FaceRules = {
		MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, SolidTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, SolidTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, SolidTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, SolidTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceOpen, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
	};
	FLayoutFaceRule* const PosZRule = FaceRules.FindByPredicate([](const FLayoutFaceRule& Rule)
	{
		return Rule.Direction == ELayoutFaceDirection::PosZ;
	});
	if (!TestNotNull(TEXT("Stacked-shell test authors positive-Z face"), PosZRule))
	{
		return false;
	}
	PosZRule->BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("TStrictPosZRejectsOccupiedUpper_Module"),
		CreateTemplate(Outer, TEXT("TStrictPosZRejectsOccupiedUpper_Template"), FIntVector(16, 16, 16)),
		{ELayoutCellIntent::Boundary},
		FaceRules);

	FLayoutRegionSolveRequest Request;
	Request.Seed = 203;
	Request.RegionDebugPath = TEXT("StrictPosZRejectsOccupiedUpper");
	Request.FootprintSize = FIntPoint(2, 2);
	Request.ProfileSnapshot.LevelCount = 2;
	Request.ProfileSnapshot.bSupportsSteppedTerrainSolve = true;
	Request.ProfileSnapshot.bEnableTerrainSeams = false;
	Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;
	Request.ModuleCatalog.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(
		Module,
		1,
		ELayoutPlacementZone::Corner));

	FLayoutPlannedCell& LowerBridge = Request.PlannedCells.AddDefaulted_GetRef();
	LowerBridge.Cell = FIntVector(0, 0, 0);
	LowerBridge.Intent = ELayoutCellIntent::Boundary;
	LowerBridge.bIsBridgeCell = true;
	FLayoutPlannedCell& ShiftedAuthoredUpper = Request.PlannedCells.AddDefaulted_GetRef();
	ShiftedAuthoredUpper.Cell = FIntVector(0, 0, 1);
	ShiftedAuthoredUpper.Intent = ELayoutCellIntent::Boundary;

	const FLayoutIndexedDomainSnapshot Domains = FLayoutProfileSolver::BuildIndexedDomainSnapshot(Request);
	TestFalse(TEXT("Occupied upper rejects authored lower PosZ RequiresEmptyNeighbor"), Domains.bSucceeded);
	TestTrue(TEXT("Indexed domain reports strict vertical occupancy rejection"),
		Domains.FailureReason.Contains(TEXT("Prepared solve has no initial candidates")));

	const FLayoutRegionSolveResult Result = FLayoutProfileSolver::SolveRegion(Request);
	TestFalse(TEXT("Strict vertical occupancy rejects the stacked bridge solve"), Result.SolveResult.bSucceeded);
	TestTrue(TEXT("Solve preserves the strict vertical occupancy rejection"),
		Result.SolveResult.FailureReason.Contains(TEXT("Prepared solve has no initial candidates")));
	TestEqual(
		TEXT("Generated bridge empty domain is classified for authorized flat fallback"),
		Result.SolveResult.PreparationFailureKind,
		ELayoutSolvePreparationFailureKind::SteppedTerrainModuleDomainInfeasible);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedSpatialLevelUsesPreservedModuleLevelTest,
	"PorismExtension.Layout.Solver.TerrainStepped.SpatialLevelUsesPreservedModuleLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSteppedSpatialLevelUsesPreservedModuleLevelTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(16, 16, 16);
	const FGameplayTagContainer SolidTags = MakeTags({LayoutGameplayTags::FaceSolid});
	auto MakeSolidFace = [&SolidTags](const ELayoutFaceDirection Direction)
	{
		return MakeConnectionFaceRule(
			Direction,
			LayoutGameplayTags::FaceSolid,
			SolidTags,
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor);
	};
	const TArray<FLayoutFaceRule> FaceRules = {
		MakeSolidFace(ELayoutFaceDirection::PosX),
		MakeSolidFace(ELayoutFaceDirection::NegX),
		MakeSolidFace(ELayoutFaceDirection::PosY),
		MakeSolidFace(ELayoutFaceDirection::NegY),
		MakeSolidFace(ELayoutFaceDirection::PosZ),
		MakeSolidFace(ELayoutFaceDirection::NegZ)
	};

	ULayoutModuleAsset* GroundModule = CreateModule(
		Outer,
		TEXT("TPreservedGround_Module"),
		CreateTemplate(Outer, TEXT("TPreservedGround_Template"), CellSize),
		{ELayoutCellIntent::Interior},
		FaceRules);
	ULayoutModuleAsset* UpperModule = CreateModule(
		Outer,
		TEXT("TPreservedUpper_Module"),
		CreateTemplate(Outer, TEXT("TPreservedUpper_Template"), CellSize),
		{ELayoutCellIntent::Interior},
		FaceRules);
	FLayoutModuleSolveSnapshot GroundSnapshot = FLayoutProfileSolver::BuildModuleSnapshot(GroundModule, 1);
	GroundSnapshot.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	FLayoutModuleSolveSnapshot UpperSnapshot = FLayoutProfileSolver::BuildModuleSnapshot(UpperModule, 1);
	UpperSnapshot.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::SpecificLevel;
	UpperSnapshot.SpecificLevel = 1;

	FLayoutRegionSolveRequest Request;
	Request.Seed = 91;
	Request.RegionDebugPath = TEXT("PreservedModuleLevel");
	Request.FootprintSize = FIntPoint(2, 1);
	Request.ProfileSnapshot.LevelCount = 2;
	Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;
	Request.ModuleCatalog.Modules = {GroundSnapshot, UpperSnapshot};
	FLayoutPlannedCell& UnshiftedUpper = Request.PlannedCells.AddDefaulted_GetRef();
	UnshiftedUpper.Cell = FIntVector(0, 0, 1);
	UnshiftedUpper.Intent = ELayoutCellIntent::Interior;
	UnshiftedUpper.ModuleLevelIndex = 1;
	FLayoutPlannedCell& ShiftedGround = Request.PlannedCells.AddDefaulted_GetRef();
	ShiftedGround.Cell = FIntVector(1, 0, 1);
	ShiftedGround.Intent = ELayoutCellIntent::Interior;
	ShiftedGround.ModuleLevelIndex = 0;

	FLayoutRegionSolveRequest FlatRequest = Request;
	FlatRequest.RegionDebugPath = TEXT("FlatModuleLevelBaseline");
	FlatRequest.FootprintSize = FIntPoint(1, 1);
	FlatRequest.PlannedCells.Reset();
	FLayoutPlannedCell& FlatGround = FlatRequest.PlannedCells.AddDefaulted_GetRef();
	FlatGround.Cell = FIntVector(0, 0, 0);
	FlatGround.Intent = ELayoutCellIntent::Interior;
	FlatGround.ModuleLevelIndex = 0;
	const FLayoutIndexedDomainSnapshot FlatDomains = FLayoutProfileSolver::BuildIndexedDomainSnapshot(FlatRequest);
	const FLayoutIndexedDomainSnapshot ShiftedDomains = FLayoutProfileSolver::BuildIndexedDomainSnapshot(Request);
	TestTrue(TEXT("Flat baseline indexed domains build"), FlatDomains.bSucceeded);
	TestTrue(TEXT("Shifted indexed domains build"), ShiftedDomains.bSucceeded);
	auto CollectModuleIds = [](const FLayoutIndexedDomainSnapshot& Snapshot, const FIntVector& Cell)
	{
		TSet<FLayoutId> ResultIds;
		const FLayoutIndexedCellDomain* Domain = Snapshot.CellDomains.FindByPredicate([&Cell](const FLayoutIndexedCellDomain& CandidateDomain)
		{
			return CandidateDomain.Cell == Cell;
		});
		if (Domain != nullptr)
		{
			for (const int32 CandidateIndex : Domain->OrderedCandidateIndices)
			{
				if (Snapshot.Candidates.IsValidIndex(CandidateIndex) && !Snapshot.Candidates[CandidateIndex].bEmpty)
				{
					ResultIds.Add(Snapshot.Candidates[CandidateIndex].ModuleSnapshotId);
				}
			}
		}
		return ResultIds;
	};
	const TSet<FLayoutId> ShiftedGroundModuleIds = CollectModuleIds(ShiftedDomains, FIntVector(1, 0, 1));
	const TSet<FLayoutId> FlatGroundModuleIds = CollectModuleIds(FlatDomains, FIntVector(0, 0, 0));
	TestTrue(
		TEXT("Shifted authored ground cell retains flat ground candidate domain"),
		ShiftedGroundModuleIds.Num() == FlatGroundModuleIds.Num()
			&& ShiftedGroundModuleIds.Includes(FlatGroundModuleIds));
	bool bCompatibilityRowsMatch = ShiftedDomains.CompatibilityRows.Num() == FlatDomains.CompatibilityRows.Num();
	for (int32 RowIndex = 0; bCompatibilityRowsMatch && RowIndex < FlatDomains.CompatibilityRows.Num(); ++RowIndex)
	{
		const FLayoutIndexedCandidateCompatibility& FlatRow = FlatDomains.CompatibilityRows[RowIndex];
		const FLayoutIndexedCandidateCompatibility& ShiftedRow = ShiftedDomains.CompatibilityRows[RowIndex];
		bCompatibilityRowsMatch = FlatRow.SourceCandidateIndex == ShiftedRow.SourceCandidateIndex
			&& FlatRow.Direction == ShiftedRow.Direction
			&& FlatRow.CompatibleCandidateBits == ShiftedRow.CompatibleCandidateBits;
	}
	TestTrue(TEXT("Spatial shifting does not change candidate compatibility rows"), bCompatibilityRowsMatch);

	const FLayoutSolveResult& Result = FLayoutProfileSolver::SolveRegionTree(Request).MergedSolveResult;
	TestTrue(TEXT("Same spatial Z solves across different preserved module levels"), Result.bSucceeded);
	if (!Result.bSucceeded)
	{
		AddError(Result.FailureReason);
		return false;
	}

	const FLayoutPlacedModule* UpperPlacement = Result.Placements.FindByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell == FIntVector(0, 0, 1);
	});
	const FLayoutPlacedModule* GroundPlacement = Result.Placements.FindByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell == FIntVector(1, 0, 1);
	});
	TestTrue(TEXT("Unshifted cell uses level-one module"), UpperPlacement != nullptr && UpperPlacement->ModuleSnapshotId == UpperSnapshot.SnapshotId);
	TestTrue(TEXT("Shifted cell at same spatial Z uses ground module"), GroundPlacement != nullptr && GroundPlacement->ModuleSnapshotId == GroundSnapshot.SnapshotId);

	LayoutProfileSolverInternal::FSolveContext FillRuleContext;
	const FIntVector ShiftedTopCell(1, 0, 3);
	FillRuleContext.ProfileSnapshot.LevelCount = 4;
	FLayoutLevelFillRule& TopFillRule = FillRuleContext.ProfileSnapshot.LevelFillRules.AddDefaulted_GetRef();
	TopFillRule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::TopLevelOnly;
	TopFillRule.FillMode = ELayoutLevelFillMode::BoundaryOnly;
	FillRuleContext.ModuleLevelByCell.Add(ShiftedTopCell, 2);
	FillRuleContext.TopPlannedLevelByXY.Add(FIntPoint(1, 0), 2);
	TestEqual(
		TEXT("Top-level fill rule uses preserved per-column module level instead of spatial profile height"),
		LayoutProfileSolverInternal::GetLevelFillModeForCellForTests(FillRuleContext, ShiftedTopCell),
		ELayoutLevelFillMode::BoundaryOnly);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedVerticalChainCoordinatesStairLandingYawTest,
	"PorismExtension.Layout.Solver.TerrainStepped.VerticalChainCoordinatesStairLandingYaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSteppedVerticalChainCoordinatesStairLandingYawTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(16, 16, 16);
	const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
	const FGameplayTagContainer StairTags = MakeTags({LayoutGameplayTags::FaceStair});
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	auto MakeMatchingStairFace = [&StairTags, &PrimaryTraversal](
		const ELayoutFaceDirection Direction,
		const ELayoutFaceOccupancyPolicy Occupancy)
	{
		FLayoutFaceRule Rule = MakeConnectionFaceRule(
			Direction,
			LayoutGameplayTags::FaceStair,
			StairTags,
			Occupancy,
			PrimaryTraversal);
		Rule.bRequireMatchingYawWithFilledNeighbor = true;
		return Rule;
	};

	ULayoutModuleAsset* Stair = CreateModule(
		Outer,
		TEXT("TUnifiedVerticalStair_Module"),
		CreateTemplate(Outer, TEXT("TUnifiedVerticalStair_Template"), CellSize),
		{ELayoutCellIntent::VerticalAccess},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, OpenTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, PrimaryTraversal),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, OpenTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, PrimaryTraversal),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceOpen, OpenTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, PrimaryTraversal),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceOpen, OpenTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, PrimaryTraversal),
			MakeMatchingStairFace(ELayoutFaceDirection::PosZ, ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
		});

	ULayoutModuleAsset* Landing = CreateModule(
		Outer,
		TEXT("TUnifiedVerticalLanding_Module"),
		CreateTemplate(Outer, TEXT("TUnifiedVerticalLanding_Template"), CellSize),
		{ELayoutCellIntent::Interior},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, OpenTags, ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor, PrimaryTraversal),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeMatchingStairFace(ELayoutFaceDirection::NegZ, ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor)
		});

	ULayoutModuleAsset* Fill = CreateModule(
		Outer,
		TEXT("TUnifiedVerticalFill_Module"),
		CreateTemplate(Outer, TEXT("TUnifiedVerticalFill_Template"), CellSize),
		{ELayoutCellIntent::Interior},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, OpenTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, PrimaryTraversal),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, OpenTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, PrimaryTraversal),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
		});

	const FLayoutModuleSolveSnapshot StairSnapshot = FLayoutProfileSolver::BuildModuleSnapshot(Stair, 1);
	const FLayoutModuleSolveSnapshot LandingSnapshot = FLayoutProfileSolver::BuildModuleSnapshot(Landing, 1);
	const FLayoutModuleSolveSnapshot FillSnapshot = FLayoutProfileSolver::BuildModuleSnapshot(Fill, 1);

	for (int32 Seed = 0; Seed < 16; ++Seed)
	{
		FLayoutRegionSolveRequest Request;
		Request.Seed = Seed;
		Request.RegionDebugPath = FString::Printf(TEXT("UnifiedVerticalChain/%d"), Seed);
		Request.FootprintSize = FIntPoint(2, 1);
		Request.ProfileSnapshot.LevelCount = 2;
		Request.ProfileSnapshot.MinimumFootprintInCells = Request.FootprintSize;
		Request.ProfileSnapshot.MaximumFootprintInCells = Request.FootprintSize;
		Request.ModuleCatalog.Modules.Add(StairSnapshot);
		Request.ModuleCatalog.Modules.Add(LandingSnapshot);
		Request.ModuleCatalog.Modules.Add(FillSnapshot);
		Request.PlannedCells = {
			MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::VerticalAccess),
			MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
			MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Boundary),
			MakePlannedCell(FIntVector(1, 0, 1), ELayoutCellIntent::Interior)
		};

		const FLayoutSolveResult& Result = FLayoutProfileSolver::SolveRegionTree(Request).MergedSolveResult;
		TestTrue(FString::Printf(TEXT("Unified vertical chain solves seed %d"), Seed), Result.bSucceeded);
		if (!Result.bSucceeded)
		{
			AddError(FString::Printf(TEXT("Seed %d failed: %s"), Seed, *Result.FailureReason));
			continue;
		}

		const bool bPinnedVerticalEndpointFace = Result.RouteConstraints.ContainsByPredicate([](const FLayoutRouteConstraintRecord& Constraint)
		{
			return (Constraint.Cell == FIntVector(0, 0, 0) || Constraint.Cell == FIntVector(0, 0, 1))
				&& !Constraint.FaceRequirements.IsEmpty();
		});
		TestFalse(TEXT("Vertical pair route endpoints remain face-disjunctive for unified CSP"), bPinnedVerticalEndpointFace);

		const FLayoutPlacedModule* Lower = Result.Placements.FindByPredicate([](const FLayoutPlacedModule& Placement)
		{
			return Placement.Cell == FIntVector(0, 0, 0);
		});
		const FLayoutPlacedModule* Upper = Result.Placements.FindByPredicate([](const FLayoutPlacedModule& Placement)
		{
			return Placement.Cell == FIntVector(0, 0, 1);
		});
		TestNotNull(TEXT("Vertical chain has lower stair"), Lower);
		TestNotNull(TEXT("Vertical chain has upper landing"), Upper);
		if (Lower != nullptr && Upper != nullptr)
		{
			TestEqual(TEXT("VerticalAccess cell selects the stair module"), Lower->ModuleSnapshotId, StairSnapshot.SnapshotId);
			TestEqual(TEXT("Cell directly above the stair selects the landing module"), Upper->ModuleSnapshotId, LandingSnapshot.SnapshotId);
			TestEqual(TEXT("Vertical chain commits matching stair and landing yaw"), Lower->YawRotationSteps, Upper->YawRotationSteps);
		}

		int32 LandingCount = 0;
		for (const FLayoutPlacedModule& Placement : Result.Placements)
		{
			if (Placement.ModuleSnapshotId != LandingSnapshot.SnapshotId)
			{
				continue;
			}
			++LandingCount;
			const FLayoutPlacedModule* PlacementBelow = Result.Placements.FindByPredicate(
				[&Placement](const FLayoutPlacedModule& Candidate)
				{
					return Candidate.Cell == Placement.Cell - FIntVector(0, 0, 1);
				});
			TestTrue(
				FString::Printf(TEXT("Stair landing %s is directly above the stair module; below=%s"),
					*Placement.Cell.ToString(),
					PlacementBelow != nullptr ? *PlacementBelow->ModuleSnapshotId.ToString() : TEXT("<none>")),
				PlacementBelow != nullptr && PlacementBelow->ModuleSnapshotId == StairSnapshot.SnapshotId);
		}
		TestEqual(TEXT("Only the supported upper cell selects the stair landing"), LandingCount, 1);
	}

	return true;
}

// ---- N4.92: Multi-level shift-DOWN solver placements ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedMultiLevelShiftDownSolverPlacementsTest,
	"PorismExtension.Layout.Solver.TerrainStepped.MultiLevelShiftDownSolverPlacements",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSteppedMultiLevelShiftDownSolverPlacementsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(16, 16, 16);

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("TShiftDown_Template"), CellSize);
	const TArray<FLayoutFaceRule> FaceRules = {
		MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
	};
	ULayoutModuleAsset* InteriorModule = CreateModule(
		Outer, TEXT("TShiftDown_InteriorModule"), Template,
		{ELayoutCellIntent::Interior}, FaceRules);
	ULayoutModuleAsset* VerticalAccessModule = CreateModule(
		Outer, TEXT("TShiftDown_VerticalAccessModule"), Template,
		{ELayoutCellIntent::VerticalAccess}, FaceRules);

	FLayoutModuleCatalog ModuleCatalog;
	ModuleCatalog.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(InteriorModule, 1));
	ModuleCatalog.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(VerticalAccessModule, 1));

	// Shift-DOWN: higher-stage column (0,0) at stage 1, lower-stage column (1,0) at stage 0.
	// Higher column Z-shifts down: cells originally Z=0,1,2 move to Z=1,2,3. Bridge at Z=0.
	// Lower column unchanged: Z=0,1,2. TopBridge at Z=3 on lower column.
	// Total: 8 planned cells.
	FLayoutRegionSolveRequest Request;
	Request.Seed = 117;
	Request.RegionDebugPath = TEXT("ShiftDownRoot");
	Request.FootprintSize = FIntPoint(2, 1);
	Request.ProfileSnapshot.LevelCount = 3;
	Request.ProfileSnapshot.MinimumFootprintInCells = FIntPoint(2, 1);
	Request.ProfileSnapshot.MaximumFootprintInCells = FIntPoint(2, 1);
	Request.ModuleCatalog = ModuleCatalog;
	Request.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Interior), // bridge on higher column
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Interior), // shifted Z=0
		MakePlannedCell(FIntVector(0, 0, 2), ELayoutCellIntent::Interior), // shifted Z=1
		MakePlannedCell(FIntVector(0, 0, 3), ELayoutCellIntent::Interior), // shifted Z=2
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(1, 0, 1), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(1, 0, 2), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(1, 0, 3), ELayoutCellIntent::Interior), // TopBridge on lower column
	};

	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionTree(Request);
	const FLayoutSolveResult& SolveResult = ScheduleResult.MergedSolveResult;

	TestTrue(TEXT("Shift-DOWN multi-level solver succeeds"), SolveResult.bSucceeded);
	if (!SolveResult.bSucceeded)
	{
		AddError(FString::Printf(TEXT("Solve failed: %s"), *SolveResult.FailureReason));
		return false;
	}

	const int32 PlacementCount = SolveResult.Placements.Num();
	TestTrue(FString::Printf(TEXT("Solver places all 8 cells (got %d)"), PlacementCount), PlacementCount >= 8);

	bool bFoundHigherBridgeZ0 = false;
	bool bFoundLowerTopBridgeZ3 = false;
	for (const FLayoutPlacedModule& P : SolveResult.Placements)
	{
		if (P.Cell == FIntVector(0, 0, 0)) bFoundHigherBridgeZ0 = true;
		if (P.Cell == FIntVector(1, 0, 3)) bFoundLowerTopBridgeZ3 = true;
	}
	TestTrue(TEXT("Bridge at Z=0 on shifted higher column"), bFoundHigherBridgeZ0);
	TestTrue(TEXT("TopBridge at Z=3 on lower column"), bFoundLowerTopBridgeZ3);

	return true;
}


// ---- N4.89: Bridge cells with explicit CanFaceRegionBoundary ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedBridgeCellsWithBoundaryFacesTest,
	"PorismExtension.Layout.Solver.TerrainStepped.BridgeCellsWithBoundaryFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSteppedBridgeCellsWithBoundaryFacesTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(16, 16, 16);

	// Separate ordinary and VerticalAccess roles so selected-interface validation remains covered by this bridge topology fixture.
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("TBridgeBoundary_Template"), CellSize);
	const TArray<FLayoutFaceRule> FaceRules = {
		MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
	};
	ULayoutModuleAsset* InteriorModule = CreateModule(
		Outer, TEXT("TBridgeBoundary_InteriorModule"), Template,
		{ELayoutCellIntent::Interior}, FaceRules);
	ULayoutModuleAsset* VerticalAccessModule = CreateModule(
		Outer, TEXT("TBridgeBoundary_VerticalAccessModule"), Template,
		{ELayoutCellIntent::VerticalAccess}, FaceRules);

	FLayoutModuleCatalog ModuleCatalog;
	ModuleCatalog.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(InteriorModule, 1));
	ModuleCatalog.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(VerticalAccessModule, 1));

	// Bridge layout: lower (0,0) Z=0, TopBridge Z=1. Higher (1,0) Z=0 bridge, Z=1 shifted.
	FLayoutRegionSolveRequest Request;
	Request.Seed = 99;
	Request.RegionDebugPath = TEXT("Root");
	Request.FootprintSize = FIntPoint(2, 1);
	Request.ProfileSnapshot.LevelCount = 1;
	Request.ProfileSnapshot.MinimumFootprintInCells = FIntPoint(2, 1);
	Request.ProfileSnapshot.MaximumFootprintInCells = FIntPoint(2, 1);
	Request.ModuleCatalog = ModuleCatalog;
	/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	Request.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(1, 0, 1), ELayoutCellIntent::Interior)
	};

	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionTree(Request);
	const FLayoutSolveResult& SolveResult = ScheduleResult.MergedSolveResult;

	TestTrue(TEXT("Solver succeeds with boundary-aware module"), SolveResult.bSucceeded);
	if (!SolveResult.bSucceeded)
	{
		AddError(FString::Printf(TEXT("Solve failed: %s"), *SolveResult.FailureReason));
		return false;
	}

	// Bridge cell at (1,0,0) has NegX=Interior to (0,0,0), PosX=OuterBoundary (edge of footprint).
	// Module has BoundaryRequirement on PosX, so it should be placed.
	bool bFoundBridge = false;
	for (const FLayoutPlacedModule& P : SolveResult.Placements)
	{
		if (P.Cell == FIntVector(1, 0, 0)) bFoundBridge = true;
	}
	TestTrue(TEXT("Bridge cell at Z=0 placed with boundary-aware module"), bFoundBridge);

	// Verify no residuals at bridge cell positions
	for (const FLayoutResidualCellRecord& R : SolveResult.ResidualUnoccupiedCells)
	{
		if (R.Cell == FIntVector(1, 0, 0) || R.Cell == FIntVector(0, 0, 1))
		{
			AddError(FString::Printf(TEXT("Unexpected residual at bridge cell %s"), *R.Cell.ToString()));
		}
	}

	return true;
}

// ---- N4.90: PosZ region boundary on top cells with bridge cells ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedBridgeCellsPosZBoundaryTest,
	"PorismExtension.Layout.Solver.TerrainStepped.BridgeCellsPosZBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSteppedBridgeCellsPosZBoundaryTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(16, 16, 16);

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("TPosZBoundary_Tmpl"), CellSize);
	const TArray<FLayoutFaceRule> FaceRules = {
		MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
		MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
	};
	ULayoutModuleAsset* InteriorModule = CreateModule(
		Outer, TEXT("TPosZBoundary_InteriorModule"), Template,
		{ELayoutCellIntent::Interior}, FaceRules);
	ULayoutModuleAsset* VerticalAccessModule = CreateModule(
		Outer, TEXT("TPosZBoundary_VerticalAccessModule"), Template,
		{ELayoutCellIntent::VerticalAccess}, FaceRules);

	FLayoutModuleCatalog ModuleCatalog;
	ModuleCatalog.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(InteriorModule, 1));
	ModuleCatalog.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(VerticalAccessModule, 1));

	FLayoutRegionSolveRequest Request;
	Request.Seed = 101;
	Request.RegionDebugPath = TEXT("Root");
	Request.FootprintSize = FIntPoint(2, 1);
	Request.ProfileSnapshot.LevelCount = 1;
	Request.ProfileSnapshot.MinimumFootprintInCells = FIntPoint(2, 1);
	Request.ProfileSnapshot.MaximumFootprintInCells = FIntPoint(2, 1);
	Request.ModuleCatalog = ModuleCatalog;
	Request.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(1, 0, 1), ELayoutCellIntent::Interior)
	};

	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionTree(Request);
	const FLayoutSolveResult& SolveResult = ScheduleResult.MergedSolveResult;

	TestTrue(TEXT("PosZ-boundary bridge-cell solve succeeds"), SolveResult.bSucceeded);
	if (!SolveResult.bSucceeded)
	{
		AddError(FString::Printf(TEXT("Solve failed: %s"), *SolveResult.FailureReason));
		return false;
	}

	bool bFoundTopBridge = false;
	bool bFoundShiftedTop = false;
	for (const FLayoutPlacedModule& P : SolveResult.Placements)
	{
		if (P.Cell == FIntVector(0, 0, 1)) bFoundTopBridge = true;
		if (P.Cell == FIntVector(1, 0, 1)) bFoundShiftedTop = true;
	}
	TestTrue(TEXT("TopBridge at Z=1 placed"), bFoundTopBridge);
	TestTrue(TEXT("Shifted top at Z=1 placed"), bFoundShiftedTop);

	return true;
}


// ---- N5.1: BoundaryRequirement enforcement (horizontal) ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedMustFaceHorizontalTest,
	"PorismExtension.Layout.Solver.TerrainStepped.MustFaceHorizontal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSteppedMustFaceHorizontalTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(16, 16, 16);

	// Module with MustFace on PosX — PosX requires boundary placement.
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("TMustH_Tmpl"), CellSize);
	FLayoutFaceRule PosXFace = MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
	PosXFace.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

	ULayoutModuleAsset* MustModule = CreateModule(
		Outer, TEXT("TMustH_Mod"), Template,
		{ELayoutCellIntent::Interior},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			PosXFace,
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
		});

	// Filler module — no MustFace flag, can go anywhere.
	UChunkStructureTemplate* FillTemplate = CreateTemplate(Outer, TEXT("TFillH_Tmpl"), CellSize);
	ULayoutModuleAsset* FillModule = CreateModule(
		Outer, TEXT("TFillH_Mod"), FillTemplate,
		{ELayoutCellIntent::Interior},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
		});

	FLayoutModuleCatalog Snapshot;
	Snapshot.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(MustModule, 1));
	Snapshot.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(FillModule, 1));

	// 1x1 — all faces boundary → MustFace PosX satisfied.
	{
		FLayoutRegionSolveRequest Request;
		Request.Seed = 201;
		Request.RegionDebugPath = TEXT("Root");
		Request.FootprintSize = FIntPoint(1, 1);
		Request.ProfileSnapshot.LevelCount = 1;
		Request.ProfileSnapshot.MinimumFootprintInCells = FIntPoint(1, 1);
		Request.ProfileSnapshot.MaximumFootprintInCells = FIntPoint(1, 1);
		Request.ModuleCatalog = Snapshot;
		Request.PlannedCells = { MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Interior) };

		const FLayoutRegionSolveScheduleResult R = FLayoutProfileSolver::SolveRegionTree(Request);
		TestTrue(TEXT("MustPosX at 1x1 cell passes"), R.MergedSolveResult.bSucceeded);
	}

	// 2x1 — MustFace module goes to edge cell (PosX=OuterBoundary).
	// Filler module fills interior cell (0,0,0) where PosX faces neighbor.
	{
		FLayoutRegionSolveRequest Request;
		Request.Seed = 202;
		Request.RegionDebugPath = TEXT("Root");
		Request.FootprintSize = FIntPoint(2, 1);
		Request.ProfileSnapshot.LevelCount = 1;
		Request.ProfileSnapshot.MinimumFootprintInCells = FIntPoint(2, 1);
		Request.ProfileSnapshot.MaximumFootprintInCells = FIntPoint(2, 1);
		Request.ModuleCatalog = Snapshot;
		Request.PlannedCells = {
			MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Interior),
			MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior)
		};

		const FLayoutRegionSolveScheduleResult R = FLayoutProfileSolver::SolveRegionTree(Request);
		TestTrue(TEXT("MustPosX in 2x1 layout succeeds"), R.MergedSolveResult.bSucceeded);

		// The MustFace module must NOT be at (0,0,0) — PosX faces interior there.
		for (const FLayoutPlacedModule& P : R.MergedSolveResult.Placements)
		{
			if (P.Module != nullptr && GetNameSafe(P.Module) == FString(TEXT("TMustH_Mod")))
			{
				TestNotEqual(TEXT("MustPosX module NOT at interior cell (0,0,0)"),
					P.Cell, FIntVector(0, 0, 0));
			}
		}
	}

	return true;
}


// ---- N5.2: BoundaryRequirement on PosZ (vertical) ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSteppedMustFacePosZTest,
	"PorismExtension.Layout.Solver.TerrainStepped.MustFacePosZ",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSteppedMustFacePosZTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(16, 16, 16);

	// Module with MustFace on PosZ — PosZ requires boundary placement.
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("TMustZ_Tmpl"), CellSize);
	FLayoutFaceRule PosZFace = MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor);
	PosZFace.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

	ULayoutModuleAsset* MustModule = CreateModule(
		Outer, TEXT("TMustZ_Mod"), Template,
		{ELayoutCellIntent::Interior},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			PosZFace,
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
		});

	// Filler module — no MustFace flag, can go anywhere.
	UChunkStructureTemplate* FillTemplate = CreateTemplate(Outer, TEXT("TFillZ_Tmpl"), CellSize);
	ULayoutModuleAsset* FillModule = CreateModule(
		Outer, TEXT("TFillZ_Mod"), FillTemplate,
		{ELayoutCellIntent::Interior},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
		});

	FLayoutModuleCatalog Snapshot;
	Snapshot.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(MustModule, 1));
	Snapshot.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(FillModule, 1));

	// Single Z=0 — PosZ is OuterBoundary → MustFace satisfied.
	{
		FLayoutRegionSolveRequest Request;
		Request.Seed = 301;
		Request.RegionDebugPath = TEXT("Root");
		Request.FootprintSize = FIntPoint(1, 1);
		Request.ProfileSnapshot.LevelCount = 1;
		Request.ProfileSnapshot.MinimumFootprintInCells = FIntPoint(1, 1);
		Request.ProfileSnapshot.MaximumFootprintInCells = FIntPoint(1, 1);
		Request.ModuleCatalog = Snapshot;
		Request.PlannedCells = { MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Interior) };

		const FLayoutRegionSolveScheduleResult R = FLayoutProfileSolver::SolveRegionTree(Request);
		TestTrue(TEXT("MustPosZ at sole Z=0 cell passes"), R.MergedSolveResult.bSucceeded);
	}

	// 2-level: MustPosZ goes to Z=1 (PosZ=OuterBoundary). Filler fills Z=0.
	{
		FLayoutRegionSolveRequest Request;
		Request.Seed = 302;
		Request.RegionDebugPath = TEXT("Root");
		Request.FootprintSize = FIntPoint(1, 1);
		Request.ProfileSnapshot.LevelCount = 1;
		Request.ProfileSnapshot.MinimumFootprintInCells = FIntPoint(1, 1);
		Request.ProfileSnapshot.MaximumFootprintInCells = FIntPoint(1, 1);
		Request.ModuleCatalog = Snapshot;
		Request.PlannedCells = {
			MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Interior),
			MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::Interior)
		};

		const FLayoutRegionSolveScheduleResult R = FLayoutProfileSolver::SolveRegionTree(Request);
		TestTrue(TEXT("MustPosZ in 2-level layout succeeds"), R.MergedSolveResult.bSucceeded);

		bool bMustAtZ0 = false;
		for (const FLayoutPlacedModule& P : R.MergedSolveResult.Placements)
		{
			if (P.Module != nullptr && GetNameSafe(P.Module) == FString(TEXT("TMustZ_Mod")) && P.Cell.Z == 0)
			{
				bMustAtZ0 = true;
			}
		}
		TestFalse(TEXT("MustPosZ NOT at Z=0 (faces interior neighbor)"), bMustAtZ0);
	}

	return true;
}
