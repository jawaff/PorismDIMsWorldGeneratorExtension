// Copyright 2026 Spotted Loaf Studio

#include "Layout/Contracts/LayoutContractModeSelection.h"

#include "Layout/Contracts/LayoutContractPipeline.h"

namespace
{
	ELayoutContractEnvironmentMode SelectEnvironmentMode(const FLayoutRegionSolveRequest& SolveRequest)
	{
		switch (SolveRequest.RootPlacementKind)
		{
		case ELayoutWorldBindingPlacementKind::OrdinaryRoot:
			if (SolveRequest.ProfileSnapshot.bUndergroundPlacement)
			{
				return ELayoutContractEnvironmentMode::UndergroundPocketPlacement;
			}
			return SolveRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve
				? ELayoutContractEnvironmentMode::SteppedSurfacePlacement
				: ELayoutContractEnvironmentMode::NonSteppedWorldPlacement;
		case ELayoutWorldBindingPlacementKind::SurfacePath:
			// Surface continuations use the same stepped adapter as roots when
			// their selected profile supports stepped terrain. This produces the
			// stage shifts and bridge cells required by terrain-aligned roads.
			return SolveRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve
				? ELayoutContractEnvironmentMode::SteppedSurfacePlacement
				: ELayoutContractEnvironmentMode::StandardRegion;
		case ELayoutWorldBindingPlacementKind::BridgeContinuation:
			// Bridge continuations still need shared stage shifting and bridge/deck
			// topology when their selected profile supports stepped terrain.
			return SolveRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve
				? ELayoutContractEnvironmentMode::SteppedSurfacePlacement
				: ELayoutContractEnvironmentMode::BridgeContinuation;
		case ELayoutWorldBindingPlacementKind::TunnelContinuation:
			return ELayoutContractEnvironmentMode::TunnelContinuation;
		case ELayoutWorldBindingPlacementKind::None:
		default:
			return ELayoutContractEnvironmentMode::StandardRegion;
		}
	}

	ELayoutContractRegionScope SelectRegionScope(const FLayoutRegionSolveRequest& SolveRequest)
	{
		if (!SolveRequest.SourceParentRegionDebugPath.IsEmpty())
		{
			return ELayoutContractRegionScope::Child;
		}

		if (SolveRequest.RootContinuationSelection.PlacementKind != ELayoutWorldBindingPlacementKind::None
			|| !SolveRequest.RootContinuationSelection.FamilyId.IsNone())
		{
			return ELayoutContractRegionScope::Continuation;
		}

		return ELayoutContractRegionScope::Root;
	}
}

FLayoutModePlan FLayoutContractModeSelection::SelectModePlan(const FLayoutContractModeSelectionInput& Input)
{
	FLayoutModePlan ModePlan;
	if (Input.SolveRequest == nullptr)
	{
		ModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ModePlan.PlacementShiftCells);
		ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
		return ModePlan;
	}

	const FLayoutRegionSolveRequest& SolveRequest = *Input.SolveRequest;
	ModePlan.Scope = SelectRegionScope(SolveRequest);
	ModePlan.EnvironmentMode = SelectEnvironmentMode(SolveRequest);
	ModePlan.bUsesSteppedTerrainTopology = SolveRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve
		&& (SolveRequest.RootPlacementKind == ELayoutWorldBindingPlacementKind::OrdinaryRoot
			|| SolveRequest.RootPlacementKind == ELayoutWorldBindingPlacementKind::SurfacePath
			|| SolveRequest.RootPlacementKind == ELayoutWorldBindingPlacementKind::BridgeContinuation);
	ModePlan.SiteCenterBlockWorldPos = Input.SiteCenterBlockWorldPos;
	ModePlan.WorldSeed = Input.WorldSeed;
	ModePlan.SolveSeed = SolveRequest.Seed;
	ModePlan.PlacementKind = SolveRequest.RootPlacementKind;
	ModePlan.PlacementPolicy = SolveRequest.WorldBindingPlacementPolicy;
	ModePlan.ContinuationSelection = SolveRequest.RootContinuationSelection;
	ModePlan.PlacementShiftCells = FIntVector::ZeroValue;
	ModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ModePlan.PlacementShiftCells);
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	return ModePlan;
}
