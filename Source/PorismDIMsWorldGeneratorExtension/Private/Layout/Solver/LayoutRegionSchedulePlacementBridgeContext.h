// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "LayoutRegionSchedulePlacementBridgeTypes.h"

/**
 * Shared runtime carriers for the thinner placement-bridge orchestration layer.
 *
 * These types intentionally describe the future cross-file interface without
 * carrying the old giant bridge implementation details with them.
 */
namespace LayoutRegionScheduleSolverPrivate
{
	/** Minimal child-interface summary needed by parent-proof and deferred-validation stages. */
	struct FPlacementBridgeChildInterfaceSummary
	{
		/** One exposed boundary face on the child planning variant. */
		struct FBoundaryFace
		{
			FIntVector LocalCell = FIntVector::ZeroValue;
			ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;
			ELayoutCellIntent Intent = ELayoutCellIntent::Interior;
			FGameplayTag ConnectionTag;
			FGameplayTagContainer AllowedConnectionTags;
			FGameplayTagContainer TraversalChannels;
		};

		TArray<FIntVector> FootprintCells;
		TArray<FBoundaryFace> BoundaryFaces;
		TArray<FIntVector> EntryCells;
		TArray<FLayoutChildCapabilityEndpoint> EntryCapabilities;
		TArray<FLayoutChildCapabilityEndpoint> ParentShareableEntryOffers;
		TArray<FLayoutChildCapabilitySpan> BoundarySpanCapabilities;
		TArray<FLayoutChildCapabilitySpan> ParentShareableSpanOffers;
		TArray<FLayoutChildCapabilitySeam> SeamCapabilities;
		TArray<FLayoutChildCapabilitySeam> ParentShareableSeamOffers;
		TArray<FLayoutChildCapabilitySeam> SiblingShareableSeamOffers;
		bool bHasExplicitEntryCapabilities = false;
		bool bSupportsVerticalAccess = false;
		TArray<FIntVector> VerticalAccessCells;
	};

	/** Reduced planning-variant view passed from bridge orchestration into hot-path helper units. */
	struct FPlacementBridgePlanningVariantView
	{
		FString ChildRegionDebugPath;
		FIntPoint FootprintSize = FIntPoint::ZeroValue;
		int32 MaxLocalLevel = 0;
		TArray<FLayoutPlannedCell> PlannedCells;
		/** Child-local filled cells that can structurally support surviving parent cells above them. */
		TArray<FIntVector> StructuralSupportLocalCells;
		FPlacementBridgeChildInterfaceSummary InterfaceSummary;
		bool bChildContributesHostVerticalAccess = false;
	};

	/** Immutable parent-side context compiled once per bridge solve. */
	struct FPlacementBridgeSolveContext
	{
		const FLayoutRegionSolveRequest* RootRequest = nullptr;
		const TArray<FLayoutPlannedCell>* ParentPlannedCells = nullptr;
		FIntPoint ParentFootprintSize = FIntPoint::ZeroValue;
		const TSet<FIntVector>* ParentPlannedCellSet = nullptr;
		const TMap<FIntVector, ELayoutCellIntent>* ParentPlannedCellIntents = nullptr;
		const TArray<FIntVector>* ParentPriorityTargets = nullptr;
		const TSet<FIntVector>* ParentProtectedTraversalCells = nullptr;
		const TSet<ELayoutFaceDirection>* ParentVerticalAccessIngressDirections = nullptr;
	};

	/** Mutable schedule state exposed to extracted helper stages without leaking the full legacy scheduler. */
	struct FPlacementBridgeMutableStateView
	{
		const TArray<FAutomaticChildPlacement>* CurrentPlacements = nullptr;
		const TSet<FIntVector>* CurrentReservedParentCells = nullptr;
	};
}
