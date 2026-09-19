// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Contracts/LayoutContractPipeline.h"

#include "ChunkWorld/ChunkWorldCore.h"
#include "Layout/Contracts/LayoutContractModeAdapter.h"
#include "Layout/Contracts/LayoutContractModeSelection.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"
#include "Layout/Terrain/LayoutTerrainOperationBuilder.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Terrain/LayoutWorldBindingTerrainFit.h"

namespace
{
	/** Private stable diagnostic categories mirrored from the recursive-negotiation design terminal failure taxonomy. */
	enum class ELayoutContractDiagnosticCategory : uint8
	{
		None = 0,
		ManifestInvalid,
		ModeUnsupported,
		TerrainSampleUnavailable,
		EntryTerrainSealed,
		ClearanceExcavationConflict,
		CoarsePlacementShiftsExhausted,
		InvalidRequiredTerrainOverlap,
		DisconnectedStage,
		MissingVerticalAccessFrontier,
		ExactVerticalAccessCountImpossible,
		FeatureProviderCountImpossible,
		ChildPlacementImpossible,
		SeamHandoffImpossible,
		BoundaryClosureImpossible,
		FoundationRampBudgetImpossible,
		BridgeSpanInvalid,
		BridgeApproachLandingUnreachable,
		TunnelExcavationConflict,
		InnerProofFailed,
		PlanningWindowRootPresolveMissedReadyRadius,
		BackgroundSolveWaitingForDispatch,
		BackgroundSolveCanceledByGroup,
		BackgroundSolveSupersededByGeneration,
		BackgroundSolveExpiredOutsidePlanningWindow,
		ContinuationEndpointOutsidePlanningWindow,
		ContinuationGraphHasNoAcceptedTargetRoots,
		ChunkRealizationWaitingForRequiredChunkOrigins
	};

	constexpr uint64 LayoutContractFnvOffsetBasis = 14695981039346656037ull;
	constexpr uint64 LayoutContractFnvPrime = 1099511628211ull;

	void AppendStableString(uint64& InOutHash, const FString& Value)
	{
		for (const TCHAR Character : Value)
		{
			InOutHash ^= static_cast<uint64>(Character);
			InOutHash *= LayoutContractFnvPrime;
		}
		InOutHash ^= static_cast<uint64>('|');
		InOutHash *= LayoutContractFnvPrime;
	}

	void AppendStableInt(uint64& InOutHash, const int64 Value)
	{
		AppendStableString(InOutHash, LexToString(Value));
	}

	void AppendStableName(uint64& InOutHash, const FLayoutId Value)
	{
		AppendStableString(InOutHash, Value.ToString());
	}

	void AppendStableVector(uint64& InOutHash, const FIntVector& Value)
	{
		AppendStableInt(InOutHash, Value.X);
		AppendStableInt(InOutHash, Value.Y);
		AppendStableInt(InOutHash, Value.Z);
	}

	void AppendStablePoint(uint64& InOutHash, const FIntPoint& Value)
	{
		AppendStableInt(InOutHash, Value.X);
		AppendStableInt(InOutHash, Value.Y);
	}

	FLayoutId MakeStableId(const FString& Prefix, const uint64 Hash)
	{
		return FLayoutId(*FString::Printf(TEXT("%s.%016llX"), *Prefix, static_cast<unsigned long long>(Hash)));
	}

	TArray<FIntVector> MakeSortedCells(const TArray<FIntVector>& Cells)
	{
		TArray<FIntVector> SortedCells = Cells;
		SortedCells.Sort([](const FIntVector& Left, const FIntVector& Right)
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
		return SortedCells;
	}

	FName GetDiagnosticCategoryName(const ELayoutContractDiagnosticCategory Category)
	{
		switch (Category)
		{
		case ELayoutContractDiagnosticCategory::None:
			return TEXT("None");
		case ELayoutContractDiagnosticCategory::ManifestInvalid:
			return TEXT("ManifestInvalid");
		case ELayoutContractDiagnosticCategory::ModeUnsupported:
			return TEXT("ModeUnsupported");
		case ELayoutContractDiagnosticCategory::TerrainSampleUnavailable:
			return TEXT("TerrainSampleUnavailable");
		case ELayoutContractDiagnosticCategory::EntryTerrainSealed:
			return TEXT("EntryTerrainSealed");
		case ELayoutContractDiagnosticCategory::ClearanceExcavationConflict:
			return TEXT("ClearanceExcavationConflict");
		case ELayoutContractDiagnosticCategory::CoarsePlacementShiftsExhausted:
			return TEXT("CoarsePlacementShiftsExhausted");
		case ELayoutContractDiagnosticCategory::InvalidRequiredTerrainOverlap:
			return TEXT("InvalidRequiredTerrainOverlap");
		case ELayoutContractDiagnosticCategory::DisconnectedStage:
			return TEXT("DisconnectedStage");
		case ELayoutContractDiagnosticCategory::MissingVerticalAccessFrontier:
			return TEXT("MissingVerticalAccessFrontier");
		case ELayoutContractDiagnosticCategory::ExactVerticalAccessCountImpossible:
			return TEXT("ExactVerticalAccessCountImpossible");
		case ELayoutContractDiagnosticCategory::FeatureProviderCountImpossible:
			return TEXT("FeatureProviderCountImpossible");
		case ELayoutContractDiagnosticCategory::ChildPlacementImpossible:
			return TEXT("ChildPlacementImpossible");
		case ELayoutContractDiagnosticCategory::SeamHandoffImpossible:
			return TEXT("SeamHandoffImpossible");
		case ELayoutContractDiagnosticCategory::BoundaryClosureImpossible:
			return TEXT("BoundaryClosureImpossible");
		case ELayoutContractDiagnosticCategory::FoundationRampBudgetImpossible:
			return TEXT("FoundationRampBudgetImpossible");
		case ELayoutContractDiagnosticCategory::BridgeSpanInvalid:
			return TEXT("BridgeSpanInvalid");
		case ELayoutContractDiagnosticCategory::BridgeApproachLandingUnreachable:
			return TEXT("BridgeApproachLandingUnreachable");
		case ELayoutContractDiagnosticCategory::TunnelExcavationConflict:
			return TEXT("TunnelExcavationConflict");
		case ELayoutContractDiagnosticCategory::InnerProofFailed:
			return TEXT("InnerProofFailed");
		case ELayoutContractDiagnosticCategory::PlanningWindowRootPresolveMissedReadyRadius:
			return TEXT("PlanningWindowRootPresolveMissedReadyRadius");
		case ELayoutContractDiagnosticCategory::BackgroundSolveWaitingForDispatch:
			return TEXT("BackgroundSolveWaitingForDispatch");
		case ELayoutContractDiagnosticCategory::BackgroundSolveCanceledByGroup:
			return TEXT("BackgroundSolveCanceledByGroup");
		case ELayoutContractDiagnosticCategory::BackgroundSolveSupersededByGeneration:
			return TEXT("BackgroundSolveSupersededByGeneration");
		case ELayoutContractDiagnosticCategory::BackgroundSolveExpiredOutsidePlanningWindow:
			return TEXT("BackgroundSolveExpiredOutsidePlanningWindow");
		case ELayoutContractDiagnosticCategory::ContinuationEndpointOutsidePlanningWindow:
			return TEXT("ContinuationEndpointOutsidePlanningWindow");
		case ELayoutContractDiagnosticCategory::ContinuationGraphHasNoAcceptedTargetRoots:
			return TEXT("ContinuationGraphHasNoAcceptedTargetRoots");
		case ELayoutContractDiagnosticCategory::ChunkRealizationWaitingForRequiredChunkOrigins:
			return TEXT("ChunkRealizationWaitingForRequiredChunkOrigins");
		default:
			return TEXT("Unknown");
		}
	}

	FIntVector ResolveSharedCellSizeInBlocks(const FLayoutRegionSolveRequest& SolveRequest)
	{
		if (SolveRequest.ContentSetSnapshot.SharedCellSizeInBlocks != FIntVector::ZeroValue)
		{
			return SolveRequest.ContentSetSnapshot.SharedCellSizeInBlocks;
		}

		return SolveRequest.ModuleCatalog.SharedCellSizeInBlocks;
	}

	int32 ResolveExactVerticalAccessCount(const FLayoutProfileSolveSnapshot& ProfileSnapshot)
	{
		return ProfileSnapshot.VerticalAccessCountMode == ELayoutCountConstraintMode::Exact
			? ProfileSnapshot.VerticalAccessCount
			: INDEX_NONE;
	}

	void AppendUniqueAssertionSummary(
		TArray<FLayoutContractManifestValidationAssertion>& InOutSummaries,
		const FLayoutValidationAssertionRecord& AssertionRecord)
	{
		for (const FLayoutContractManifestValidationAssertion& ExistingSummary : InOutSummaries)
		{
			if (ExistingSummary.AssertionId == AssertionRecord.AssertionId)
			{
				return;
			}
		}

		FLayoutContractManifestValidationAssertion& Summary = InOutSummaries.AddDefaulted_GetRef();
		Summary.AssertionId = AssertionRecord.AssertionId;
		Summary.AssertionKind = AssertionRecord.AssertionKind;
		Summary.bPassed = AssertionRecord.bPassed;
		Summary.RelatedIds = AssertionRecord.RelatedIds;
	}

	TArray<FLayoutContractManifestValidationAssertion> BuildManifestValidationAssertions(const FLayoutRegionSolveRequest& SolveRequest)
	{
		TArray<FLayoutContractManifestValidationAssertion> Summaries;
		Summaries.Reserve(
			SolveRequest.ValidationAssertions.Num()
			+ SolveRequest.ProfileSnapshot.ValidationAssertions.Num()
			+ SolveRequest.ContentSetSnapshot.ValidationAssertions.Num()
			+ SolveRequest.ModuleCatalog.ValidationAssertions.Num());

		auto AppendAssertions = [&Summaries](const TArray<FLayoutValidationAssertionRecord>& AssertionRecords)
		{
			for (const FLayoutValidationAssertionRecord& AssertionRecord : AssertionRecords)
			{
				AppendUniqueAssertionSummary(Summaries, AssertionRecord);
			}
		};

		AppendAssertions(SolveRequest.ValidationAssertions);
		AppendAssertions(SolveRequest.ProfileSnapshot.ValidationAssertions);
		AppendAssertions(SolveRequest.ContentSetSnapshot.ValidationAssertions);
		AppendAssertions(SolveRequest.ModuleCatalog.ValidationAssertions);
		for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : SolveRequest.ContentSetSnapshot.Entries)
		{
			if (EntrySnapshot.CompiledChildRequestTemplate.IsValid())
			{
				AppendAssertions(EntrySnapshot.CompiledChildRequestTemplate->ValidationAssertions);
			}
		}
		for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : SolveRequest.ModuleCatalog.Modules)
		{
			AppendAssertions(ModuleSnapshot.ValidationAssertions);
		}

		return Summaries;
	}

	TArray<FLayoutContractManifestContentEntrySummary> BuildManifestContentEntrySummaries(const FLayoutRegionSolveRequest& SolveRequest)
	{
		TArray<FLayoutContractManifestContentEntrySummary> Summaries;
		Summaries.Reserve(SolveRequest.ContentSetSnapshot.Entries.Num());
		for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : SolveRequest.ContentSetSnapshot.Entries)
		{
			FLayoutContractManifestContentEntrySummary& Summary = Summaries.AddDefaulted_GetRef();
			Summary.EntryId = EntrySnapshot.EntryId;
			Summary.ContentKind = EntrySnapshot.ContentKind;
			Summary.Weight = EntrySnapshot.Weight;
			Summary.ProvidedZoneFeatures = EntrySnapshot.ProvidedZoneFeatures;
			Summary.ClosureProviderIntents = EntrySnapshot.ClosureProviderIntents;
			Summary.SeamProviderIntents = EntrySnapshot.SeamProviderIntents;
			Summary.ModuleSnapshotIndex = EntrySnapshot.ModuleSnapshotIndex;
			Summary.ModulePlacementZone = EntrySnapshot.ModulePlacementZone;
			Summary.ModuleLevelPlacementPolicy = EntrySnapshot.ModuleLevelPlacementPolicy;
			Summary.ModuleSpecificLevel = EntrySnapshot.ModuleSpecificLevel;
			Summary.bModuleOptional = EntrySnapshot.bModuleOptional;
			Summary.ChildProfileSnapshotId = EntrySnapshot.ChildProfileSnapshotId;
			Summary.ChildProfilePath = EntrySnapshot.ChildProfilePath;
			Summary.ChildContentSetSnapshotId = EntrySnapshot.ChildContentSetSnapshotId;
			Summary.ChildPlacementZone = EntrySnapshot.ChildPlacementZone;
			Summary.ChildLevelPlacementPolicy = EntrySnapshot.ChildLevelPlacementPolicy;
			Summary.ChildSpecificLevel = EntrySnapshot.ChildSpecificLevel;
			Summary.bChildOptional = EntrySnapshot.bChildOptional;
			Summary.bChildContributesHostVerticalAccess = EntrySnapshot.bChildContributesHostVerticalAccess;
		}
		return Summaries;
	}

	TArray<FLayoutContractManifestModuleSummary> BuildManifestModuleSummaries(const FLayoutRegionSolveRequest& SolveRequest)
	{
		TArray<FLayoutContractManifestModuleSummary> Summaries;
		Summaries.Reserve(SolveRequest.ModuleCatalog.Modules.Num());
		for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : SolveRequest.ModuleCatalog.Modules)
		{
			FLayoutContractManifestModuleSummary& Summary = Summaries.AddDefaulted_GetRef();
			Summary.SnapshotId = ModuleSnapshot.SnapshotId;
			Summary.DebugName = ModuleSnapshot.DebugName;
			Summary.TemplatePath = ModuleSnapshot.Template.ToSoftObjectPath();
			Summary.BoundsCells = ModuleSnapshot.BoundsCells;
			Summary.OccupiedLocalCells = ModuleSnapshot.OccupiedLocalCells;
			Summary.Roles = ModuleSnapshot.Roles;
			Summary.TraversalChannels = ModuleSnapshot.TraversalChannels;
			Summary.SourceContentEntryId = ModuleSnapshot.SourceContentEntryId;
			Summary.PlacementZone = ModuleSnapshot.PlacementZone;
			Summary.LevelPlacementPolicy = ModuleSnapshot.LevelPlacementPolicy;
			Summary.SpecificLevel = ModuleSnapshot.SpecificLevel;
			Summary.bOptional = ModuleSnapshot.bOptional;
			Summary.bCompositeSnapshot = ModuleSnapshot.SourceCompositeModule != nullptr;
		}
		return Summaries;
	}

	TArray<FLayoutId> BuildManifestClosureRequirementIds(const FLayoutProfileSolveSnapshot& ProfileSnapshot)
	{
		TArray<FLayoutId> RequirementIds;
		RequirementIds.Reserve(ProfileSnapshot.ClosureRequirements.Num());
		for (const FLayoutClosureRequirement& Requirement : ProfileSnapshot.ClosureRequirements)
		{
			RequirementIds.Add(Requirement.ClosureId);
		}
		return RequirementIds;
	}

	TArray<FLayoutId> BuildManifestZoneFeatureRequirementIds(const FLayoutProfileSolveSnapshot& ProfileSnapshot)
	{
		TArray<FLayoutId> RequirementIds;
		RequirementIds.Reserve(ProfileSnapshot.ZoneFeatureRequirements.Num());
		for (const FLayoutZoneFeatureRequirement& Requirement : ProfileSnapshot.ZoneFeatureRequirements)
		{
			RequirementIds.Add(Requirement.RequirementId);
		}
		return RequirementIds;
	}

	ELayoutContractRegionScope ResolveRegionScope(const FLayoutRegionSolveRequest& SolveRequest)
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

TArray<FName> FLayoutContractPipeline::GetStableDiagnosticCategoryNames()
{
	return {
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::None),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::ManifestInvalid),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::ModeUnsupported),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::TerrainSampleUnavailable),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::EntryTerrainSealed),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::ClearanceExcavationConflict),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::CoarsePlacementShiftsExhausted),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::InvalidRequiredTerrainOverlap),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::DisconnectedStage),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::MissingVerticalAccessFrontier),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::ExactVerticalAccessCountImpossible),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::FeatureProviderCountImpossible),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::ChildPlacementImpossible),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::SeamHandoffImpossible),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::BoundaryClosureImpossible),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::FoundationRampBudgetImpossible),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::BridgeSpanInvalid),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::BridgeApproachLandingUnreachable),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::TunnelExcavationConflict),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::InnerProofFailed),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::PlanningWindowRootPresolveMissedReadyRadius),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::BackgroundSolveWaitingForDispatch),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::BackgroundSolveCanceledByGroup),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::BackgroundSolveSupersededByGeneration),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::BackgroundSolveExpiredOutsidePlanningWindow),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::ContinuationEndpointOutsidePlanningWindow),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::ContinuationGraphHasNoAcceptedTargetRoots),
		GetDiagnosticCategoryName(ELayoutContractDiagnosticCategory::ChunkRealizationWaitingForRequiredChunkOrigins)
	};
}

FLayoutId FLayoutContractPipeline::BuildModePlanId(const FLayoutModePlan& ModePlan)
{
	uint64 Hash = LayoutContractFnvOffsetBasis;
	AppendStableString(Hash, TEXT("ModePlan"));
	AppendStableInt(Hash, static_cast<int64>(ModePlan.Scope));
	AppendStableInt(Hash, static_cast<int64>(ModePlan.EnvironmentMode));
	AppendStableInt(Hash, ModePlan.bUsesSteppedTerrainTopology ? 1 : 0);
	AppendStableVector(Hash, ModePlan.SiteCenterBlockWorldPos);
	AppendStableVector(Hash, ModePlan.PlacementShiftCells);
	AppendStableName(Hash, ModePlan.PlacementShiftId);
	AppendStableInt(Hash, ModePlan.WorldSeed);
	AppendStableInt(Hash, ModePlan.SolveSeed);
	AppendStableInt(Hash, static_cast<int64>(ModePlan.PlacementKind));
	AppendStableInt(Hash, ModePlan.PlacementPolicy.TerrainSampleGridSpacing);
	AppendStableInt(Hash, ModePlan.PlacementPolicy.HeightIgnoreThreshold);
	AppendStableInt(Hash, ModePlan.PlacementPolicy.TerrainTransition.bAllowFoundationFill ? 1 : 0);
	AppendStableInt(Hash, ModePlan.PlacementPolicy.TerrainTransition.MaxFoundationDepth);
	AppendStableInt(Hash, ModePlan.PlacementPolicy.TerrainTransition.bAllowPerimeterRampTransition ? 1 : 0);
	AppendStableInt(Hash, ModePlan.PlacementPolicy.TerrainTransition.MinimumSteppedTerrainShiftClusterCells);
	AppendStableInt(Hash, ModePlan.PlacementPolicy.TerrainTransition.bFallbackToFlatTerrainWhenSteppedReservedOpenIsInfeasible ? 1 : 0);
	AppendStableName(Hash, ModePlan.ContinuationSelection.FamilyId);
	AppendStableInt(Hash, static_cast<int64>(ModePlan.ContinuationSelection.PlacementKind));
	AppendStableInt(Hash, ModePlan.ContinuationSelection.ResolvedEntryLevel);
	return MakeStableId(TEXT("ModePlan"), Hash);
}

FLayoutId FLayoutContractPipeline::BuildPlacementShiftId(const FIntVector& PlacementShiftCells)
{
	uint64 Hash = LayoutContractFnvOffsetBasis;
	AppendStableString(Hash, TEXT("PlacementShift"));
	AppendStableVector(Hash, PlacementShiftCells);
	return MakeStableId(TEXT("PlacementShift"), Hash);
}

FLayoutId FLayoutContractPipeline::BuildCellMaskId(const FString& Prefix, const TArray<FIntVector>& Cells)
{
	uint64 Hash = LayoutContractFnvOffsetBasis;
	AppendStableString(Hash, Prefix);
	for (const FIntVector& Cell : MakeSortedCells(Cells))
	{
		AppendStableVector(Hash, Cell);
	}
	return MakeStableId(Prefix.IsEmpty() ? FString(TEXT("Mask")) : Prefix, Hash);
}

FLayoutId FLayoutContractPipeline::BuildActiveCellMaskId(const FString& Prefix, const TArray<FLayoutContractActiveCellRecord>& ActiveCells)
{
	TArray<FLayoutContractActiveCellRecord> SortedCells = ActiveCells;
	SortedCells.Sort([](const FLayoutContractActiveCellRecord& Left, const FLayoutContractActiveCellRecord& Right)
	{
		if (Left.Cell.Z != Right.Cell.Z)
		{
			return Left.Cell.Z < Right.Cell.Z;
		}
		if (Left.Cell.Y != Right.Cell.Y)
		{
			return Left.Cell.Y < Right.Cell.Y;
		}
		if (Left.Cell.X != Right.Cell.X)
		{
			return Left.Cell.X < Right.Cell.X;
		}
		return false;
	});

	uint64 Hash = LayoutContractFnvOffsetBasis;
	AppendStableString(Hash, Prefix);
	for (const FLayoutContractActiveCellRecord& ActiveCell : SortedCells)
	{
		AppendStableVector(Hash, ActiveCell.Cell);
	}
	return MakeStableId(Prefix.IsEmpty() ? FString(TEXT("ActiveMask")) : Prefix, Hash);
}

bool FLayoutContractPipeline::ValidateActiveCellRecords(
	const TArray<FLayoutContractActiveCellRecord>& ActiveCells,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	TSet<FIntVector> SeenActiveCellCoordinates;
	for (const FLayoutContractActiveCellRecord& ActiveCell : ActiveCells)
	{
		if (SeenActiveCellCoordinates.Contains(ActiveCell.Cell))
		{
			OutFailureReason = TEXT("Region contract active-cell collection contains duplicate active-cell records for one local cell.");
			return false;
		}
		SeenActiveCellCoordinates.Add(ActiveCell.Cell);
	}
	return true;
}

FLayoutId FLayoutContractPipeline::BuildStageMapId(const TArray<FLayoutPlannedCell>& PlannedCells)
{
	TArray<FLayoutPlannedCell> SortedCells = PlannedCells;
	SortedCells.Sort([](const FLayoutPlannedCell& Left, const FLayoutPlannedCell& Right)
	{
		if (Left.Cell.Z != Right.Cell.Z)
		{
			return Left.Cell.Z < Right.Cell.Z;
		}
		if (Left.Cell.Y != Right.Cell.Y)
		{
			return Left.Cell.Y < Right.Cell.Y;
		}
		if (Left.Cell.X != Right.Cell.X)
		{
			return Left.Cell.X < Right.Cell.X;
		}
		return static_cast<uint8>(Left.Intent) < static_cast<uint8>(Right.Intent);
	});

	uint64 Hash = LayoutContractFnvOffsetBasis;
	AppendStableString(Hash, TEXT("StageMap"));
	for (const FLayoutPlannedCell& PlannedCell : SortedCells)
	{
		AppendStableVector(Hash, PlannedCell.Cell);
		AppendStableInt(Hash, static_cast<int64>(PlannedCell.Intent));
		AppendStableInt(Hash, static_cast<int64>(PlannedCell.EntryOrigin));
		AppendStableInt(Hash, PlannedCell.ModuleLevelIndex);
		AppendStableInt(Hash, PlannedCell.bIsBridgeCell ? 1 : 0);
		if (PlannedCell.bIsTopBridgeOffer)
		{
			AppendStableString(Hash, TEXT("TopBridgeOffer"));
		}
		AppendStableInt(Hash, static_cast<int64>(PlannedCell.PlacementZone));
		AppendStableInt(Hash, PlannedCell.TerrainSeamFaceMask);
		AppendStableInt(Hash, PlannedCell.VerticalAccessLandingContactMask);
	}
	return MakeStableId(TEXT("StageMap"), Hash);
}

FLayoutId FLayoutContractPipeline::BuildTerrainWriteArtifactId(const FLayoutFrozenTerrainContract& FrozenTerrainContract)
{
	uint64 Hash = LayoutContractFnvOffsetBasis;
	AppendStableString(Hash, TEXT("TerrainWriteArtifact"));
	AppendStableVector(Hash, FrozenTerrainContract.SiteCenterBlockWorldPos);
	AppendStableVector(Hash, FrozenTerrainContract.FootprintMinBlockWorldPos);
	AppendStableVector(Hash, FrozenTerrainContract.SharedCellSizeInBlocks);
	AppendStablePoint(Hash, FrozenTerrainContract.FootprintSizeInCells);
	AppendStableInt(Hash, static_cast<int64>(FrozenTerrainContract.DiagnosticKind));

	TArray<FLayoutTerrainCellContractRecord> SortedCellContracts = FrozenTerrainContract.CellContracts;
	SortedCellContracts.Sort([](const FLayoutTerrainCellContractRecord& Left, const FLayoutTerrainCellContractRecord& Right)
	{
		if (Left.Cell.Z != Right.Cell.Z)
		{
			return Left.Cell.Z < Right.Cell.Z;
		}
		if (Left.Cell.Y != Right.Cell.Y)
		{
			return Left.Cell.Y < Right.Cell.Y;
		}
		if (Left.Cell.X != Right.Cell.X)
		{
			return Left.Cell.X < Right.Cell.X;
		}
		return static_cast<uint8>(Left.Contract) < static_cast<uint8>(Right.Contract);
	});
	for (const FLayoutTerrainCellContractRecord& CellContract : SortedCellContracts)
	{
		AppendStableVector(Hash, CellContract.Cell);
		AppendStableInt(Hash, static_cast<int64>(CellContract.Contract));
		AppendStableInt(Hash, CellContract.bHasFoundationFillEvidence ? 1 : 0);
		if (CellContract.bHasFoundationFillEvidence)
		{
			AppendStableInt(Hash, CellContract.RequiredFoundationDepth);
			AppendStableInt(Hash, CellContract.FoundationMaterial);
		}
		AppendStableInt(Hash, CellContract.bHasClearanceEvidence ? 1 : 0);
		AppendStableInt(Hash, CellContract.bHasRampTransitionEvidence ? 1 : 0);
		AppendStableInt(Hash, CellContract.bHasBridgeSupportEvidence ? 1 : 0);
		AppendStableInt(Hash, CellContract.bHasExcavationOverlapZ ? 1 : 0);
		if (CellContract.bHasExcavationOverlapZ)
		{
			AppendStableInt(Hash, CellContract.ExcavationOverlapMinLocalZ.Value);
			AppendStableInt(Hash, CellContract.ExcavationOverlapMaxLocalZ.Value);
		}
	}

	TArray<FLayoutTerrainBackedNeighborFaceRecord> SortedTerrainBackedFaces = FrozenTerrainContract.TerrainBackedNeighborFaces;
	SortedTerrainBackedFaces.Sort([](const FLayoutTerrainBackedNeighborFaceRecord& Left, const FLayoutTerrainBackedNeighborFaceRecord& Right)
	{
		if (Left.Cell.Z != Right.Cell.Z) return Left.Cell.Z < Right.Cell.Z;
		if (Left.Cell.Y != Right.Cell.Y) return Left.Cell.Y < Right.Cell.Y;
		if (Left.Cell.X != Right.Cell.X) return Left.Cell.X < Right.Cell.X;
		if (Left.FaceDirection != Right.FaceDirection) return static_cast<uint8>(Left.FaceDirection) < static_cast<uint8>(Right.FaceDirection);
		if (Left.NeighborCell.Z != Right.NeighborCell.Z) return Left.NeighborCell.Z < Right.NeighborCell.Z;
		if (Left.NeighborCell.Y != Right.NeighborCell.Y) return Left.NeighborCell.Y < Right.NeighborCell.Y;
		if (Left.NeighborCell.X != Right.NeighborCell.X) return Left.NeighborCell.X < Right.NeighborCell.X;
		return Left.SourceEvidenceId.LexicalLess(Right.SourceEvidenceId);
	});
	AppendStableString(Hash, TEXT("TerrainBackedNeighborFaces"));
	for (const FLayoutTerrainBackedNeighborFaceRecord& Face : SortedTerrainBackedFaces)
	{
		AppendStableVector(Hash, Face.Cell);
		AppendStableInt(Hash, static_cast<uint8>(Face.FaceDirection));
		AppendStableVector(Hash, Face.NeighborCell);
		AppendStableName(Hash, Face.SourceEvidenceId);
	}

	TArray<FLayoutContractActiveCellRecord> SortedActiveCells = FrozenTerrainContract.ActiveCells;
	SortedActiveCells.Sort([](const FLayoutContractActiveCellRecord& Left, const FLayoutContractActiveCellRecord& Right)
	{
		if (Left.Cell.Z != Right.Cell.Z)
		{
			return Left.Cell.Z < Right.Cell.Z;
		}
		if (Left.Cell.Y != Right.Cell.Y)
		{
			return Left.Cell.Y < Right.Cell.Y;
		}
		if (Left.Cell.X != Right.Cell.X)
		{
			return Left.Cell.X < Right.Cell.X;
		}
		return false;
	});
	for (const FLayoutContractActiveCellRecord& ActiveCell : SortedActiveCells)
	{
		AppendStableVector(Hash, ActiveCell.Cell);
	}

	TArray<FLayoutCellReservationRecord> SortedReservedOpenReservations = FrozenTerrainContract.ReservedOpenTerrainReservations;
	SortedReservedOpenReservations.Sort([](const FLayoutCellReservationRecord& Left, const FLayoutCellReservationRecord& Right)
	{
		if (Left.ReservationId != Right.ReservationId) return Left.ReservationId.LexicalLess(Right.ReservationId);
		if (Left.Cell.Z != Right.Cell.Z) return Left.Cell.Z < Right.Cell.Z;
		if (Left.Cell.Y != Right.Cell.Y) return Left.Cell.Y < Right.Cell.Y;
		return Left.Cell.X < Right.Cell.X;
	});
	AppendStableString(Hash, TEXT("ReservedOpenTerrainReservations"));
	for (const FLayoutCellReservationRecord& Reservation : SortedReservedOpenReservations)
	{
		AppendStableName(Hash, Reservation.ReservationId);
		AppendStableVector(Hash, Reservation.Cell);
		AppendStableInt(Hash, static_cast<int64>(Reservation.Intent));
		AppendStableInt(Hash, static_cast<int64>(Reservation.ReservationKind));
		AppendStableInt(Hash, static_cast<int64>(Reservation.TerrainBehavior));
	}

	TArray<FLayoutFrozenTerrainWriteRecord> SortedWrites = FrozenTerrainContract.TerrainWrites;
	LayoutTerrainOperationBuilder::SortWrites(SortedWrites);
	for (const FLayoutFrozenTerrainWriteRecord& WriteRecord : SortedWrites)
	{
		AppendStableVector(Hash, WriteRecord.BlockWorldPos);
		AppendStableInt(Hash, WriteRecord.Material);
		AppendStableInt(Hash, static_cast<int64>(WriteRecord.SourceContract));
		if (WriteRecord.bResolveMaterialFromTerrain)
		{
			AppendStableInt(Hash, 0x46494C4C); // Deferred fill material is part of frozen authority.
			AppendStableVector(Hash, WriteRecord.MaterialSourceBlockWorldPos);
			AppendStableInt(Hash, WriteRecord.MaterialSourceSearchDepthBlocks);
		}
	}
	for (const FIntVector& RemovedCell : MakeSortedCells(FrozenTerrainContract.RemovedCells))
	{
		AppendStableVector(Hash, RemovedCell);
	}
	for (const FIntVector& ProtectedCell : MakeSortedCells(FrozenTerrainContract.ProtectedOccupiedCells))
	{
		AppendStableVector(Hash, ProtectedCell);
	}
	return MakeStableId(TEXT("TerrainWriteArtifact"), Hash);
}

FLayoutContractManifest FLayoutContractPipeline::BuildManifestFromSolveRequest(const FLayoutRegionSolveRequest& SolveRequest)
{
	FLayoutContractManifest Manifest;
	Manifest.ManifestId = !SolveRequest.EffectiveSnapshotId.IsNone()
		? SolveRequest.EffectiveSnapshotId
		: SolveRequest.ProfileSnapshot.SnapshotId;
	Manifest.ProfileSnapshotId = SolveRequest.ProfileSnapshot.SnapshotId;
	Manifest.ProfileSourcePath = SolveRequest.ProfileSnapshot.SourceProfilePath;
	Manifest.ContentSetSnapshotId = SolveRequest.ContentSetSnapshot.SnapshotId;
	Manifest.ModuleCatalogId = SolveRequest.ModuleCatalog.SnapshotId;
	Manifest.WorldBindingId = SolveRequest.WorldBindingId;
	Manifest.SharedCellSizeInBlocks = ResolveSharedCellSizeInBlocks(SolveRequest);
	Manifest.MinimumFootprintInCells = SolveRequest.ProfileSnapshot.MinimumFootprintInCells;
	Manifest.MaximumFootprintInCells = SolveRequest.ProfileSnapshot.MaximumFootprintInCells;
	Manifest.LevelCount = SolveRequest.ProfileSnapshot.LevelCount;
	Manifest.bSupportsSteppedTerrainSolve = SolveRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve;
	Manifest.bEnableTerrainSeams = SolveRequest.ProfileSnapshot.bEnableTerrainSeams;
	Manifest.RequiredVerticalAccessCount = ResolveExactVerticalAccessCount(SolveRequest.ProfileSnapshot);
	Manifest.ClosureRequirementIds = BuildManifestClosureRequirementIds(SolveRequest.ProfileSnapshot);
	Manifest.ZoneFeatureRequirementIds = BuildManifestZoneFeatureRequirementIds(SolveRequest.ProfileSnapshot);
	Manifest.ValidationAssertions = BuildManifestValidationAssertions(SolveRequest);
	Manifest.ContentEntries = BuildManifestContentEntrySummaries(SolveRequest);
	Manifest.Modules = BuildManifestModuleSummaries(SolveRequest);
	return Manifest;
}

FLayoutModePlan FLayoutContractPipeline::BuildModePlanFromSolveRequest(
	const FLayoutRegionSolveRequest& SolveRequest,
	const FIntVector& SiteCenterBlockWorldPos,
	const int32 WorldSeed)
{
	FLayoutContractModeSelectionInput Input;
	Input.SolveRequest = &SolveRequest;
	Input.SiteCenterBlockWorldPos = SiteCenterBlockWorldPos;
	Input.WorldSeed = WorldSeed;
	return FLayoutContractModeSelection::SelectModePlan(Input);
}

bool FLayoutContractPipeline::TryBuildPreparedRegionContract(
	const FLayoutContractManifest& Manifest,
	const FIntPoint& FootprintSize,
	const FLayoutAdapterOutput& AdapterOutput,
	FLayoutRegionContract& OutContract,
	FString& OutFailureReason)
{
	OutContract = FLayoutRegionContract();
	OutFailureReason.Reset();

	OutContract.Manifest = Manifest;
	if (OutContract.Manifest.SharedCellSizeInBlocks == FIntVector::ZeroValue)
	{
		OutFailureReason = TEXT("Layout contract requires a non-zero shared cell size.");
		return false;
	}

	if (!AdapterOutput.bSucceeded || AdapterOutput.PlannedCells.IsEmpty())
	{
		OutFailureReason = TEXT("Region contract assembly requires successful prepared adapter output.");
		return false;
	}
	OutContract.ModePlan = AdapterOutput.ModePlan;
	OutContract.FootprintSize = FootprintSize;
	OutContract.PlannedCells = AdapterOutput.PlannedCells;
	OutContract.bHasFinalizedSteppedTerrainIntents = AdapterOutput.bHasFinalizedSteppedTerrainIntents;
	OutContract.VerticalAccessHostGroups = AdapterOutput.VerticalAccessHostGroups;
	OutContract.ActiveCells = AdapterOutput.ActiveCells;
	if (OutContract.ActiveCells.IsEmpty())
	{
		OutFailureReason = TEXT("Layout contract adapter must emit active-cell records before proof can continue.");
		return false;
	}
	if (!ValidateActiveCellRecords(OutContract.ActiveCells, OutFailureReason))
	{
		return false;
	}
	OutContract.ActiveMaskId = BuildActiveCellMaskId(TEXT("ActiveMask"), OutContract.ActiveCells);
	OutContract.RemovedMaskId = BuildCellMaskId(TEXT("RemovedMask"), OutContract.RemovedCells);
	OutContract.VerticalAccessMaskId = BuildCellMaskId(TEXT("VerticalAccessMask"), OutContract.VerticalAccessCells);
	OutContract.ProtectedTraversalMaskId = BuildCellMaskId(TEXT("ProtectedTraversalMask"), OutContract.ProtectedTraversalCells);
	OutContract.StageMapId = BuildStageMapId(OutContract.PlannedCells);
	OutContract.SteppedTerrainSupportMap = AdapterOutput.SteppedTerrainSupportMap;
	OutContract.FrozenTerrainContract = AdapterOutput.FrozenTerrainContract;
	OutContract.FrozenTerrainContract.SiteCenterBlockWorldPos = OutContract.ModePlan.SiteCenterBlockWorldPos;
	OutContract.FrozenTerrainContract.SharedCellSizeInBlocks = OutContract.Manifest.SharedCellSizeInBlocks;
	OutContract.FrozenTerrainContract.FootprintSizeInCells = FootprintSize;
	OutContract.FrozenTerrainContract.ActiveCells = OutContract.ActiveCells;
	OutContract.FrozenTerrainContract.ContractId = BuildTerrainWriteArtifactId(OutContract.FrozenTerrainContract);
	return true;
}

bool FLayoutContractPipeline::TryPrecomputeAdapterOutput(
	FLayoutRegionSolveRequest& InOutSolveRequest,
	FString& OutFailureReason,
	FLayoutAdapterOutput* const OutRejectedAdapterPreview)
{
	PORISM_LAYOUT_PROFILE_SCOPE(Layout_Prewarm_AdapterPrecompute, STAT_PorismLayout_AdapterPrecompute);
	OutFailureReason.Reset();
	if (OutRejectedAdapterPreview != nullptr)
	{
		*OutRejectedAdapterPreview = FLayoutAdapterOutput();
	}

	const FLayoutContractManifest PreSolveManifest = BuildManifestFromSolveRequest(InOutSolveRequest);
	if (PreSolveManifest.SharedCellSizeInBlocks == FIntVector::ZeroValue)
	{
		OutFailureReason = TEXT("Precompute requires a non-zero shared cell size.");
		return false;
	}

	FLayoutModePlan PreSolveModePlan;
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Prewarm_ModePlan, STAT_PorismLayout_PrewarmModePlan);
		PreSolveModePlan = InOutSolveRequest.bHasSelectedModePlan
			? InOutSolveRequest.SelectedModePlan
			: BuildModePlanFromSolveRequest(
				InOutSolveRequest,
				InOutSolveRequest.SelectedModePlan.SiteCenterBlockWorldPos,
				InOutSolveRequest.Seed);
	}

	FLayoutContractModeAdapterInput AdapterInput;
	AdapterInput.ModePlan = PreSolveModePlan;
	AdapterInput.SolveRequest = &InOutSolveRequest;
	AdapterInput.Manifest = &PreSolveManifest;

	/** Removes stale adapter evidence after final planning removes logical cells. */
	const auto ReconcileAdapterOutputToPlan = [](FLayoutAdapterOutput& InOutAdapterOutput)
	{
		TSet<FIntVector> FinalPlannedCells;
		FinalPlannedCells.Reserve(InOutAdapterOutput.PlannedCells.Num());
		for (const FLayoutPlannedCell& PlannedCell : InOutAdapterOutput.PlannedCells)
		{
			FinalPlannedCells.Add(PlannedCell.Cell);
		}
		InOutAdapterOutput.ActiveCells.RemoveAll([&FinalPlannedCells](const FLayoutContractActiveCellRecord& ActiveCell)
		{
			return !FinalPlannedCells.Contains(ActiveCell.Cell);
		});
		InOutAdapterOutput.FrozenTerrainContract.ActiveCells = InOutAdapterOutput.ActiveCells;
		InOutAdapterOutput.FrozenTerrainContract.CellContracts.RemoveAll(
			[&FinalPlannedCells](const FLayoutTerrainCellContractRecord& CellContract)
			{
				return !FinalPlannedCells.Contains(CellContract.Cell);
			});
		InOutAdapterOutput.FrozenTerrainContract.TerrainBackedNeighborFaces.RemoveAll(
			[&FinalPlannedCells](const FLayoutTerrainBackedNeighborFaceRecord& FaceRecord)
			{
				return !FinalPlannedCells.Contains(FaceRecord.Cell);
			});
	};

	/** Rebuilds one fresh flat contract from original frozen evidence after classified stepped prewarm insufficiency. */
	const auto TryBuildFlatFallback = [&AdapterInput, &InOutSolveRequest, &ReconcileAdapterOutputToPlan](
		const FString& SteppedFailureReason,
		FLayoutAdapterOutput& OutFlatAdapterOutput,
		FLayoutRegionSolveRequest& OutFlatPlanningRequest,
		FString& OutFallbackFailureReason)
	{
		FLayoutContractModeAdapterInput FlatAdapterInput = AdapterInput;
		const bool bPreserveUndergroundEnvironment =
			AdapterInput.ModePlan.EnvironmentMode == ELayoutContractEnvironmentMode::UndergroundPocketPlacement;
		FlatAdapterInput.ModePlan.EnvironmentMode = bPreserveUndergroundEnvironment
			? ELayoutContractEnvironmentMode::UndergroundPocketPlacement
			: ELayoutContractEnvironmentMode::NonSteppedWorldPlacement;
		FlatAdapterInput.ModePlan.bUsesSteppedTerrainTopology = false;
		FlatAdapterInput.ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(FlatAdapterInput.ModePlan);
		FLayoutRegionSolveRequest FlatAdapterRequest = InOutSolveRequest;
		FlatAdapterRequest.PrecomputedPlannedCells.Reset();
		FlatAdapterRequest.bHasFinalizedSteppedTerrainIntents = false;
		FlatAdapterRequest.SteppedTerrainSupportMap = FLayoutSteppedTerrainSupportMap();
		FlatAdapterRequest.PrecomputedSteppedTerrainSupportMap = FLayoutSteppedTerrainSupportMap();
		FlatAdapterRequest.FrozenTerrainBiomeAdapterInput.bHasSteppedSupportEvidence = false;
		FlatAdapterRequest.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples.Reset();
		FlatAdapterRequest.FrozenTerrainBiomeAdapterInput.SteppedNeighborHaloSamples.Reset();
		const ELayoutWorldBindingPlacementKind EffectivePlacementKind =
			FlatAdapterRequest.RootPlacementKind != ELayoutWorldBindingPlacementKind::None
				? FlatAdapterRequest.RootPlacementKind
				: FlatAdapterInput.ModePlan.PlacementKind;
		const bool bContinuationPlacement =
			EffectivePlacementKind == ELayoutWorldBindingPlacementKind::SurfacePath
			|| EffectivePlacementKind == ELayoutWorldBindingPlacementKind::BridgeContinuation
			|| EffectivePlacementKind == ELayoutWorldBindingPlacementKind::TunnelContinuation;
		if (bContinuationPlacement)
		{
			// Flat fallback keeps sparse continuation authority instead of expanding its bounding rectangle.
			TArray<FLayoutPlannedCell> FlatRouteCells;
			TSet<FIntVector> FlatRouteCellIds;
			FlatRouteCells.Reserve(FlatAdapterRequest.PlannedCells.Num());
			for (const FLayoutPlannedCell& SourceCell : FlatAdapterRequest.PlannedCells)
			{
				if (SourceCell.bIsBridgeCell)
				{
					continue;
				}
				FLayoutPlannedCell FlatCell = SourceCell;
				if (FlatCell.ModuleLevelIndex != INDEX_NONE)
				{
					FlatCell.Cell.Z = FlatCell.ModuleLevelIndex;
				}
				FlatCell.TerrainSeamFaceMask = 0;
				FlatCell.VerticalAccessLandingContactMask = 0;
				if (FlatRouteCellIds.Contains(FlatCell.Cell))
				{
					OutFallbackFailureReason = FString::Printf(
						TEXT("Flat continuation normalization produced duplicate route cell %s."),
						*FlatCell.Cell.ToString());
					return false;
				}
				FlatRouteCellIds.Add(FlatCell.Cell);
				FlatRouteCells.Add(MoveTemp(FlatCell));
			}
			FlatAdapterRequest.PlannedCells = MoveTemp(FlatRouteCells);
		}
		else
		{
			FLayoutProfileSolveSnapshot FlatPlanProfile = FlatAdapterRequest.ProfileSnapshot;
			FlatPlanProfile.ReservedOpenSpaceRules.Reset();
			if (!LayoutProfileSolverInternal::BuildAuthoredPlan(
					FlatPlanProfile,
					FlatAdapterRequest.ModuleCatalog,
					FlatAdapterRequest.Seed,
					FlatAdapterRequest.FootprintSize,
					FlatAdapterRequest.PlannedCells,
					OutFallbackFailureReason))
			{
				return false;
			}
		}
		FlatAdapterInput.SolveRequest = &FlatAdapterRequest;
		if (!FLayoutContractModeAdapter::TryRunAdapter(
				FlatAdapterInput,
				OutFlatAdapterOutput,
				OutFallbackFailureReason))
		{
			return false;
		}
		OutFlatAdapterOutput.SteppedTerrainSupportMap = FLayoutSteppedTerrainSupportMap();
		OutFlatAdapterOutput.VerticalAccessHostGroups.Reset();
		for (FLayoutPlannedCell& PlannedCell : OutFlatAdapterOutput.PlannedCells)
		{
			PlannedCell.bIsBridgeCell = false;
			PlannedCell.bIsTopBridgeOffer = false;
			PlannedCell.TerrainSeamFaceMask = 0;
			PlannedCell.VerticalAccessLandingContactMask = 0;
		}
		OutFlatAdapterOutput.FrozenTerrainContract.StageMap.Reset();
		OutFlatAdapterOutput.FrozenTerrainContract.TerrainBackedNeighborFaces.Reset();
		OutFlatPlanningRequest = FlatAdapterRequest;
		OutFlatPlanningRequest.SelectedModePlan = OutFlatAdapterOutput.ModePlan;
		OutFlatPlanningRequest.bHasSelectedModePlan = true;
		OutFlatPlanningRequest.SteppedTerrainSupportMap = FLayoutSteppedTerrainSupportMap();
		OutFlatPlanningRequest.PrecomputedFrozenTerrainContract = OutFlatAdapterOutput.FrozenTerrainContract;
		OutFlatPlanningRequest.QualifiedEntryCells = OutFlatAdapterOutput.QualifiedEntryCells;
		OutFlatPlanningRequest.bHasQualifiedEntryCells = !OutFlatPlanningRequest.QualifiedEntryCells.IsEmpty();
		if (!FLayoutProfileSolver::FinalizeFlatTerrainPlan(
				OutFlatAdapterOutput.PlannedCells,
				OutFlatPlanningRequest,
				OutFallbackFailureReason))
		{
			return false;
		}
		OutFlatAdapterOutput.FrozenTerrainContract.ReservedOpenTerrainReservations =
			OutFlatPlanningRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations;
		ReconcileAdapterOutputToPlan(OutFlatAdapterOutput);
		OutFlatPlanningRequest.PrecomputedFrozenTerrainContract = OutFlatAdapterOutput.FrozenTerrainContract;
		OutFlatAdapterOutput.Diagnostics.AddDefaulted_GetRef().Detail = FString::Printf(
			TEXT("Stepped fallback to flat: insufficient usable shifted cells for classified prewarm. Original stepped rejection: %s"),
			*SteppedFailureReason);
		return true;
	};

	FLayoutAdapterOutput AdapterOutput;
	FLayoutRegionSolveRequest IntentPlanningRequest;
	TArray<FLayoutCellReservationRecord> FinalizedReservedOpenTerrainReservations;
	bool bUsedFlatFallback = false;
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Prewarm_ContractAdapter, STAT_PorismLayout_ContractAdapter);
		ELayoutSteppedTerrainFinalizationFailureKind AdapterFailureKind =
			ELayoutSteppedTerrainFinalizationFailureKind::None;
		if (!FLayoutContractModeAdapter::TryRunAdapter(AdapterInput, AdapterOutput, OutFailureReason, &AdapterFailureKind))
		{
			const bool bCanFallbackToFlatTerrain =
				PreSolveModePlan.bUsesSteppedTerrainTopology
				&& InOutSolveRequest.WorldBindingPlacementPolicy.TerrainTransition.bFallbackToFlatTerrainWhenSteppedReservedOpenIsInfeasible
				&& (AdapterFailureKind == ELayoutSteppedTerrainFinalizationFailureKind::EntryTerrainQualificationInfeasible
					|| AdapterFailureKind == ELayoutSteppedTerrainFinalizationFailureKind::StageTopologyInfeasible);
			if (!bCanFallbackToFlatTerrain)
			{
				// Adapter-stage rejection can already contain raw corridor cells and a
				// cell/edge diagnostic. Preserve it for preview only; never form a contract.
				if (OutRejectedAdapterPreview != nullptr && !AdapterOutput.PlannedCells.IsEmpty())
				{
					AdapterOutput.bSucceeded = false;
					*OutRejectedAdapterPreview = MoveTemp(AdapterOutput);
				}
				return false;
			}
			const FString SteppedFailureReason = OutFailureReason;
			FString FlatFailureReason;
			if (!TryBuildFlatFallback(
					SteppedFailureReason,
					AdapterOutput,
					IntentPlanningRequest,
					FlatFailureReason))
			{
				OutFailureReason = FString::Printf(
					TEXT("%s Flat terrain fallback could not rebuild a fresh contract: %s"),
					*SteppedFailureReason,
					*FlatFailureReason);
				return false;
			}
			bUsedFlatFallback = true;
			OutFailureReason.Reset();
		}
	}
	const auto PublishRejectedAdapterPreview = [&AdapterOutput, OutRejectedAdapterPreview]()
	{
		if (OutRejectedAdapterPreview != nullptr)
		{
			AdapterOutput.bSucceeded = false;
			*OutRejectedAdapterPreview = AdapterOutput;
		}
	};
	for (const FLayoutContractActiveCellRecord& ActiveCell : AdapterOutput.ActiveCells)
	{
		if (!AdapterOutput.PlannedCells.ContainsByPredicate([&ActiveCell](const FLayoutPlannedCell& PlannedCell)
			{
				return PlannedCell.Cell == ActiveCell.Cell;
			}))
		{
			OutFailureReason = FString::Printf(
				TEXT("Adapter active cell %s has no matching planned-cell metadata."),
				*ActiveCell.Cell.ToString());
			PublishRejectedAdapterPreview();
			return false;
		}
	}

	if (!bUsedFlatFallback)
	{
		IntentPlanningRequest = InOutSolveRequest;
		IntentPlanningRequest.SelectedModePlan = AdapterOutput.ModePlan;
		IntentPlanningRequest.bHasSelectedModePlan = true;
		IntentPlanningRequest.SteppedTerrainSupportMap = AdapterOutput.SteppedTerrainSupportMap;
		// VerticalAccess host admission runs during finalization and needs the adapter's
		// frozen terrain-backed face proof before the precomputed request is published.
		// Local project fix: adapter terrain output does not own an already-compiled
		// removed-cell reservation, so retain request authority across contract replacement.
		TArray<FLayoutCellReservationRecord> ExistingReservedOpenTerrainReservations =
			MoveTemp(IntentPlanningRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations);
		IntentPlanningRequest.PrecomputedFrozenTerrainContract = AdapterOutput.FrozenTerrainContract;
		if (IntentPlanningRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations.IsEmpty())
		{
			IntentPlanningRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations =
				MoveTemp(ExistingReservedOpenTerrainReservations);
		}
		IntentPlanningRequest.QualifiedEntryCells = AdapterOutput.QualifiedEntryCells;
		IntentPlanningRequest.bHasQualifiedEntryCells = !IntentPlanningRequest.QualifiedEntryCells.IsEmpty();
		ELayoutSteppedTerrainFinalizationFailureKind FinalizationFailureKind =
			ELayoutSteppedTerrainFinalizationFailureKind::None;
		if (!FLayoutProfileSolver::FinalizeSteppedTerrainPlan(
			AdapterOutput.PlannedCells,
			IntentPlanningRequest,
			OutFailureReason,
			&FinalizationFailureKind,
			&FinalizedReservedOpenTerrainReservations))
	{
		const FString SteppedFailureReason = OutFailureReason;
		const bool bCanFallbackToFlatTerrain =
			PreSolveModePlan.bUsesSteppedTerrainTopology
			&& InOutSolveRequest.WorldBindingPlacementPolicy.TerrainTransition.bFallbackToFlatTerrainWhenSteppedReservedOpenIsInfeasible
			&& (FinalizationFailureKind == ELayoutSteppedTerrainFinalizationFailureKind::ReservedOpenSteppedTopologyInfeasible
				|| FinalizationFailureKind == ELayoutSteppedTerrainFinalizationFailureKind::EntryTerrainQualificationInfeasible
				|| FinalizationFailureKind == ELayoutSteppedTerrainFinalizationFailureKind::StageTopologyInfeasible);
		if (!bCanFallbackToFlatTerrain)
		{
			// Finalization can reject after identifying host alternatives and their first
			// module/face rejection. Keep that authoring evidence preview-only.
			AdapterOutput.VerticalAccessHostGroups = MoveTemp(IntentPlanningRequest.VerticalAccessHostGroups);
			PublishRejectedAdapterPreview();
			return false;
		}

		FLayoutAdapterOutput FlatAdapterOutput;
		FLayoutRegionSolveRequest FlatPlanningRequest;
		FString FlatFailureReason;
		if (!TryBuildFlatFallback(
				SteppedFailureReason,
				FlatAdapterOutput,
				FlatPlanningRequest,
				FlatFailureReason))
		{
			OutFailureReason = FString::Printf(
				TEXT("%s Flat terrain fallback could not rebuild a fresh contract: %s"),
				*SteppedFailureReason,
				*FlatFailureReason);
			AdapterOutput.VerticalAccessHostGroups = MoveTemp(IntentPlanningRequest.VerticalAccessHostGroups);
			PublishRejectedAdapterPreview();
			return false;
		}
		AdapterOutput = MoveTemp(FlatAdapterOutput);
		IntentPlanningRequest = MoveTemp(FlatPlanningRequest);
		OutFailureReason.Reset();
	}
	}
	AdapterOutput.bHasFinalizedSteppedTerrainIntents = IntentPlanningRequest.bHasFinalizedSteppedTerrainIntents;
	AdapterOutput.VerticalAccessHostGroups = IntentPlanningRequest.VerticalAccessHostGroups;
	// Local project fix: tiny stepped clusters can normalize to flat before adapter
	// finalization, leaving reservation selection already compiled into request authority.
	AdapterOutput.FrozenTerrainContract.ReservedOpenTerrainReservations =
		!FinalizedReservedOpenTerrainReservations.IsEmpty()
			? MoveTemp(FinalizedReservedOpenTerrainReservations)
			: IntentPlanningRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations;
	ReconcileAdapterOutputToPlan(AdapterOutput);
	for (FLayoutTerrainCellContractRecord& CellContract : AdapterOutput.FrozenTerrainContract.CellContracts)
	{
		// Final planning may remove mixed support flags. Recompute pure clearance
		// ownership before publication while retaining perimeter ramp authority.
		if (CellContract.bHasClearanceEvidence
			&& CellContract.Contract == ELayoutFrozenTerrainCellContract::Active
			&& !CellContract.bHasFoundationFillEvidence
			&& !CellContract.bHasRampTransitionEvidence
			&& !CellContract.bHasBridgeSupportEvidence)
		{
			CellContract.Contract = ELayoutFrozenTerrainCellContract::FlatClearance;
		}
		if (PreSolveModePlan.PlacementKind == ELayoutWorldBindingPlacementKind::OrdinaryRoot)
		{
			// Ordinary-root templates own footprint replacement; overlap bounds are pre-solve
			// admission evidence and must not authorize a second excavation replay.
			CellContract.bHasExcavationOverlapZ = false;
			CellContract.ExcavationOverlapMinLocalZ = FLayoutLocalBlockCoord8();
			CellContract.ExcavationOverlapMaxLocalZ = FLayoutLocalBlockCoord8();
		}
	}
	if (PreSolveModePlan.PlacementKind == ELayoutWorldBindingPlacementKind::OrdinaryRoot)
	{
		LayoutTerrainOperationBuilder::BuildOrdinaryRootWrites(
			AdapterOutput.FrozenTerrainContract,
			InOutSolveRequest.FrozenTerrainBiomeAdapterInput.PocketVoidIntervals,
			InOutSolveRequest.FrozenTerrainBiomeAdapterInput.PerimeterSurfaceSamples,
			0,
			InOutSolveRequest.TemplatePlacementZOffsetBlocks,
			PreSolveModePlan.PlacementPolicy.TerrainTransition,
			AdapterOutput.FrozenTerrainContract.TerrainWrites);
	}
	AdapterOutput.FrozenTerrainContract.ContractId =
		BuildTerrainWriteArtifactId(AdapterOutput.FrozenTerrainContract);

	InOutSolveRequest.PrecomputedPlannedCells = AdapterOutput.PlannedCells;
	InOutSolveRequest.bHasFinalizedSteppedTerrainIntents = AdapterOutput.bHasFinalizedSteppedTerrainIntents;
	InOutSolveRequest.VerticalAccessHostGroups = MoveTemp(AdapterOutput.VerticalAccessHostGroups);
	InOutSolveRequest.PlannedCells = MoveTemp(AdapterOutput.PlannedCells);
	InOutSolveRequest.PrecomputedActiveCells = MoveTemp(AdapterOutput.ActiveCells);
	InOutSolveRequest.PrecomputedAdapterDiagnostics = AdapterOutput.Diagnostics;
	InOutSolveRequest.PrecomputedFrozenTerrainContract = MoveTemp(AdapterOutput.FrozenTerrainContract);
	InOutSolveRequest.SteppedTerrainSupportMap = AdapterOutput.SteppedTerrainSupportMap;
	InOutSolveRequest.PrecomputedSteppedTerrainSupportMap = MoveTemp(AdapterOutput.SteppedTerrainSupportMap);
	InOutSolveRequest.QualifiedEntryCells = MoveTemp(AdapterOutput.QualifiedEntryCells);
	InOutSolveRequest.bHasQualifiedEntryCells = !InOutSolveRequest.QualifiedEntryCells.IsEmpty();
	InOutSolveRequest.SelectedModePlan = AdapterOutput.ModePlan;
	InOutSolveRequest.bHasSelectedModePlan = true;
	return true;
}

bool FLayoutContractPipeline::CanTryChildLocalFlatFallback(const FLayoutRegionSolveRequest& Request)
{
	return !Request.bUseChildLocalFlatFallback
		&& Request.bHasSelectedModePlan
		&& Request.SelectedModePlan.bUsesSteppedTerrainTopology
		&& !Request.PrecomputedFrozenTerrainContract.StageMap.IsEmpty()
		&& Request.WorldBindingPlacementPolicy.TerrainTransition
			.bFallbackToFlatTerrainWhenSteppedReservedOpenIsInfeasible
		&& Request.ContentSetSnapshot.Entries.ContainsByPredicate(
			[](const FLayoutRegionContentEntrySolveSnapshot& Entry)
			{
				return Entry.ContentKind == ELayoutRegionContentKind::ChildRegion
					&& Entry.ChildLevelPlacementPolicy == ELayoutLevelPlacementPolicy::GroundOnly;
			});
}

bool FLayoutContractPipeline::TryBuildChildLocalFlatFallbackRequest(
	const FLayoutRegionSolveRequest& SteppedRequest,
	FLayoutRegionSolveRequest& OutRequest)
{
	if (!CanTryChildLocalFlatFallback(SteppedRequest)) return false;
	OutRequest = SteppedRequest;
	OutRequest.bUseChildLocalFlatFallback = true;
	OutRequest.PreparedChildPlacementHints.Reset();
	return true;
}

bool FLayoutContractPipeline::TryBuildFlatFallbackRequestAfterSteppedChildPreparationFailure(
	const FLayoutRegionSolveRequest& SteppedRequest,
	const FString& SteppedFailureReason,
	FLayoutRegionSolveRequest& OutFlatRequest,
	FString& OutFailureReason)
{
	if (!SteppedRequest.WorldBindingPlacementPolicy.TerrainTransition.bFallbackToFlatTerrainWhenSteppedReservedOpenIsInfeasible)
	{
		OutFailureReason = TEXT("Flat terrain fallback is disabled by the root placement policy.");
		return false;
	}
	OutFlatRequest = SteppedRequest;
	OutFlatRequest.bUseChildLocalFlatFallback = false;
	const bool bPreserveUndergroundEnvironment =
		SteppedRequest.SelectedModePlan.EnvironmentMode
			== ELayoutContractEnvironmentMode::UndergroundPocketPlacement;
	OutFlatRequest.SelectedModePlan.EnvironmentMode = bPreserveUndergroundEnvironment
		? ELayoutContractEnvironmentMode::UndergroundPocketPlacement
		: ELayoutContractEnvironmentMode::NonSteppedWorldPlacement;
	OutFlatRequest.SelectedModePlan.bUsesSteppedTerrainTopology = false;
	OutFlatRequest.SelectedModePlan.ModePlanId =
		BuildModePlanId(OutFlatRequest.SelectedModePlan);
	OutFlatRequest.bHasSelectedModePlan = true;
	OutFlatRequest.PrecomputedPlannedCells.Reset();
	OutFlatRequest.PlannedCells.Reset();
	OutFlatRequest.PrecomputedActiveCells.Reset();
	OutFlatRequest.VerticalAccessHostGroups.Reset();
	OutFlatRequest.PreparedChildPlacementHints.Reset();
	OutFlatRequest.bHasFinalizedSteppedTerrainIntents = false;
	OutFlatRequest.SteppedTerrainSupportMap = FLayoutSteppedTerrainSupportMap();
	OutFlatRequest.PrecomputedSteppedTerrainSupportMap = FLayoutSteppedTerrainSupportMap();
	OutFlatRequest.FrozenTerrainBiomeAdapterInput.bHasSteppedSupportEvidence = false;
	OutFlatRequest.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples.Reset();
	OutFlatRequest.FrozenTerrainBiomeAdapterInput.SteppedNeighborHaloSamples.Reset();

	FLayoutProfileSolveSnapshot FlatPlanProfile = OutFlatRequest.ProfileSnapshot;
	FlatPlanProfile.ReservedOpenSpaceRules.Reset();
	if (!LayoutProfileSolverInternal::BuildAuthoredPlan(
			FlatPlanProfile,
			OutFlatRequest.ModuleCatalog,
			OutFlatRequest.Seed,
			OutFlatRequest.FootprintSize,
			OutFlatRequest.PlannedCells,
			OutFailureReason)
		|| !TryPrecomputeAdapterOutput(OutFlatRequest, OutFailureReason))
	{
		return false;
	}

	FLayoutAdapterDiagnostic& Diagnostic =
		OutFlatRequest.PrecomputedAdapterDiagnostics.AddDefaulted_GetRef();
	Diagnostic.Detail = FString::Printf(
		TEXT("Stepped required-child preparation fallback to flat. Original rejection: %s"),
		*SteppedFailureReason);
	return true;
}

bool FLayoutContractPipeline::TryApplyRegionContractToSolveRequest(
	const FLayoutRegionContract& Contract,
	FLayoutRegionSolveRequest& InOutSolveRequest,
	FString& OutFailureReason)
{
	if (!ValidateActiveCellRecords(Contract.ActiveCells, OutFailureReason))
	{
		return false;
	}

	TSet<FIntVector> RealActiveCells;
	for (const FLayoutContractActiveCellRecord& ActiveCell : Contract.ActiveCells)
	{
		if (true)
		{
			RealActiveCells.Add(ActiveCell.Cell);
		}
	}
	for (const FLayoutPlannedCell& PlannedCell : Contract.PlannedCells)
	{
		if (!RealActiveCells.Contains(PlannedCell.Cell))
		{
			OutFailureReason = TEXT("Region contract planned cells must be present as real active cells before request finalization.");
			return false;
		}
	}

	InOutSolveRequest.RootPlacementKind = Contract.ModePlan.PlacementKind;
	InOutSolveRequest.WorldBindingPlacementPolicy = Contract.ModePlan.PlacementPolicy;
	InOutSolveRequest.RootContinuationSelection = Contract.ModePlan.ContinuationSelection;
	InOutSolveRequest.FootprintSize = Contract.FootprintSize;
	InOutSolveRequest.PlannedCells = Contract.PlannedCells;
	InOutSolveRequest.PrecomputedPlannedCells = Contract.PlannedCells;
	InOutSolveRequest.bHasFinalizedSteppedTerrainIntents = Contract.bHasFinalizedSteppedTerrainIntents;
	InOutSolveRequest.VerticalAccessHostGroups = Contract.VerticalAccessHostGroups;
	InOutSolveRequest.PrecomputedActiveCells = Contract.ActiveCells;
	InOutSolveRequest.SteppedTerrainSupportMap = Contract.SteppedTerrainSupportMap;
	InOutSolveRequest.PrecomputedSteppedTerrainSupportMap = Contract.SteppedTerrainSupportMap;
	InOutSolveRequest.PrecomputedFrozenTerrainContract = Contract.FrozenTerrainContract;
	return true;
}

void FLayoutContractPipeline::ApplyRegionContractToSolveRequest(
	const FLayoutRegionContract& Contract,
	FLayoutRegionSolveRequest& InOutSolveRequest)
{
	FString IgnoredFailureReason;
	ensureMsgf(
		TryApplyRegionContractToSolveRequest(Contract, InOutSolveRequest, IgnoredFailureReason),
		TEXT("Invalid region contract active-cell carrier applied to solve request: %s"),
		*IgnoredFailureReason);
}

bool FLayoutContractPipeline::ApplyFrozenTerrainContract(
	AChunkWorldCore* const ChunkWorld,
	const FLayoutFrozenTerrainContract& FrozenTerrainContract,
	FString& OutFailureReason,
	const TFunction<TArray<int32>(const TArray<FIntVector>&)>& MaterialReader)
{
	OutFailureReason.Reset();
	if (ChunkWorld == nullptr)
	{
		OutFailureReason = TEXT("Frozen terrain contract realization requires a chunk world.");
		return false;
	}

	if (!FrozenTerrainContract.TerrainWrites.IsEmpty())
	{
		if (FrozenTerrainContract.CellContracts.IsEmpty())
		{
			OutFailureReason = TEXT("Frozen terrain contract terrain writes require terrain cell contract authority.");
			return false;
		}

		FString ActiveCellFailureReason;
		if (FrozenTerrainContract.ActiveCells.IsEmpty()
			|| !ValidateActiveCellRecords(FrozenTerrainContract.ActiveCells, ActiveCellFailureReason))
		{
			OutFailureReason = ActiveCellFailureReason.IsEmpty()
				? TEXT("Frozen terrain contract with terrain writes requires valid active-cell records before realization.")
				: ActiveCellFailureReason;
			return false;
		}
		TSet<FIntVector> RealActiveCells;
		for (const FLayoutContractActiveCellRecord& ActiveCell : FrozenTerrainContract.ActiveCells)
		{
			if (true)
			{
				RealActiveCells.Add(ActiveCell.Cell);
			}
		}
		if (RealActiveCells.IsEmpty())
		{
			OutFailureReason = TEXT("Frozen terrain contract terrain writes require at least one real active-cell authority.");
			return false;
		}
		for (const FLayoutTerrainCellContractRecord& CellContract : FrozenTerrainContract.CellContracts)
		{
			if (!RealActiveCells.Contains(CellContract.Cell))
			{
				OutFailureReason = TEXT("Frozen terrain contract terrain writes reference a cell without real active-cell authority.");
				return false;
			}
		}
	}
	if (FrozenTerrainContract.TerrainWrites.IsEmpty())
	{
		return true;
	}

	// Snapshot every support material before any terrain write, including excavation.
	// Fresh-chunk admission belongs to the realization caller; no edit reconciliation here.
	TArray<FIntVector> MaterialSources;
	TMap<FIntVector, int32> MaterialSourceIndices;
	for (const FLayoutFrozenTerrainWriteRecord& WriteRecord : FrozenTerrainContract.TerrainWrites)
	{
		if (!WriteRecord.bResolveMaterialFromTerrain) continue;
		const FIntVector& Source = WriteRecord.MaterialSourceBlockWorldPos;
		if (Source.X != WriteRecord.BlockWorldPos.X || Source.Y != WriteRecord.BlockWorldPos.Y
			|| Source.Z >= WriteRecord.BlockWorldPos.Z || WriteRecord.Material == EmptyMaterial
			|| WriteRecord.MaterialSourceSearchDepthBlocks < 0
			|| static_cast<int64>(Source.Z) - WriteRecord.MaterialSourceSearchDepthBlocks < MIN_int32)
		{
			OutFailureReason = TEXT("Deferred fill material requires support beneath its own fill column.");
			return false;
		}
		for (int64 Depth = 0; Depth <= WriteRecord.MaterialSourceSearchDepthBlocks; ++Depth)
		{
			const FIntVector Probe(Source.X, Source.Y, static_cast<int32>(static_cast<int64>(Source.Z) - Depth));
			if (!MaterialSourceIndices.Contains(Probe))
			{
				MaterialSourceIndices.Add(Probe, MaterialSources.Add(Probe));
			}
		}
	}
	TArray<int32> SupportMaterials;
	if (!MaterialSources.IsEmpty())
	{
		SupportMaterials = MaterialReader ? MaterialReader(MaterialSources)
			: ChunkWorld->GetBlockValuesByBlockWorldPos(MaterialSources, ERessourceType::MaterialIndex, 0);
		if (SupportMaterials.Num() != MaterialSources.Num())
		{
			OutFailureReason = TEXT("Deferred fill material requires available support chunks before realization.");
			return false;
		}
	}

	TArray<FIntVector> Positions;
	TArray<int32> Materials;
	Positions.Reserve(FrozenTerrainContract.TerrainWrites.Num());
	Materials.Reserve(FrozenTerrainContract.TerrainWrites.Num());
	for (const FLayoutFrozenTerrainWriteRecord& WriteRecord : FrozenTerrainContract.TerrainWrites)
	{
		Positions.Add(WriteRecord.BlockWorldPos);
		int32 Material = WriteRecord.Material;
		if (WriteRecord.bResolveMaterialFromTerrain)
		{
			Material = EmptyMaterial;
			const FIntVector& Source = WriteRecord.MaterialSourceBlockWorldPos;
			for (int64 Depth = 0; Depth <= WriteRecord.MaterialSourceSearchDepthBlocks; ++Depth)
			{
				const FIntVector Probe(Source.X, Source.Y, static_cast<int32>(static_cast<int64>(Source.Z) - Depth));
				const int32 Candidate = SupportMaterials[MaterialSourceIndices.FindChecked(Probe)];
				if (Candidate >= 0 && Candidate < EmptyMaterial)
				{
					Material = Candidate;
					break;
				}
			}
			if (Material == EmptyMaterial)
			{
				OutFailureReason = FString::Printf(TEXT("Fill support material unavailable at %s within %d blocks below."),
					*Source.ToString(), WriteRecord.MaterialSourceSearchDepthBlocks);
				return false;
			}
		}
		Materials.Add(Material);
	}

	ChunkWorld->SetBlockValuesByBlockWorldPos(Positions, Materials, false);
	return true;
}
