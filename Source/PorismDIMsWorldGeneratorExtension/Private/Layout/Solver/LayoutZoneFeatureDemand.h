// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutProfileSolver.h"

/** Shared pointer-free matching rules for counted zone-feature providers. */
namespace LayoutZoneFeatureDemand
{
	/** Pointer-free hard demand compiled from one immutable profile requirement. */
	struct FHardDemand
	{
		FName RequirementId;
		ELayoutPlacementZone Zone = ELayoutPlacementZone::Any;
		FGameplayTagContainer RequiredFeatures;
		ELayoutZoneFeatureMatchMode MatchMode = ELayoutZoneFeatureMatchMode::Any;
		int32 MinCount = 0;
		int32 MaxCount = 0;
	};

	/** Pointer-free authored provider choice exposed before module or child placement. */
	struct FProviderChoiceSummary
	{
		FLayoutId ProviderSummaryId;
		FName RequirementId;
		FName SourceContentEntryId;
		ELayoutRegionContentKind ContentKind = ELayoutRegionContentKind::Module;
		ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Any;
		ELayoutLevelPlacementPolicy LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;
		int32 SpecificLevel = 0;
		int32 Weight = 1;
		bool bOptional = false;
	};

	/** Matches one content entry's exact provided tags against one authored requirement. */
	inline bool DoesProvidedFeatureSetMatchRequirement(
		const FGameplayTagContainer& ProvidedFeatures,
		const FLayoutZoneFeatureRequirement& Requirement)
	{
		if (Requirement.RequiredFeatures.IsEmpty())
		{
			return false;
		}

		return Requirement.MatchMode == ELayoutZoneFeatureMatchMode::All
			? ProvidedFeatures.HasAllExact(Requirement.RequiredFeatures)
			: ProvidedFeatures.HasAnyExact(Requirement.RequiredFeatures);
	}

	/** Matches one provider feature set against one compiled hard demand. */
	inline bool DoesProvidedFeatureSetMatchDemand(
		const FGameplayTagContainer& ProvidedFeatures,
		const FHardDemand& Demand)
	{
		if (Demand.RequiredFeatures.IsEmpty())
		{
			return false;
		}

		return Demand.MatchMode == ELayoutZoneFeatureMatchMode::All
			? ProvidedFeatures.HasAllExact(Demand.RequiredFeatures)
			: ProvidedFeatures.HasAnyExact(Demand.RequiredFeatures);
	}

	/** Compiles required counted features once in stable id order. */
	inline void CompileHardDemands(
		const TArray<FLayoutZoneFeatureRequirement>& Requirements,
		TArray<FHardDemand>& OutDemands)
	{
		OutDemands.Reset();
		for (const FLayoutZoneFeatureRequirement& Requirement : Requirements)
		{
			if (Requirement.RequiredFeatures.IsEmpty())
			{
				continue;
			}

			FHardDemand& Demand = OutDemands.AddDefaulted_GetRef();
			Demand.RequirementId = Requirement.RequirementId;
			Demand.Zone = Requirement.Zone;
			Demand.RequiredFeatures = Requirement.RequiredFeatures;
			Demand.MatchMode = Requirement.MatchMode;
			Demand.MinCount = FMath::Max(0, Requirement.MinCount);
			Demand.MaxCount = FMath::Max(0, Requirement.MaxCount);
		}
		OutDemands.Sort([](const FHardDemand& Left, const FHardDemand& Right)
		{
			return Left.RequirementId.LexicalLess(Right.RequirementId);
		});
	}

	/** Tests whether one authored provider zone can overlap a requirement's finalized zone. */
	inline bool DoPotentialPlacementZonesOverlap(
		const ELayoutPlacementZone ProviderZone,
		const ELayoutPlacementZone RequirementZone)
	{
		if (RequirementZone == ELayoutPlacementZone::Any
			|| ProviderZone == ELayoutPlacementZone::Any
			|| RequirementZone == ProviderZone)
		{
			return true;
		}

		switch (RequirementZone)
		{
		case ELayoutPlacementZone::Perimeter:
			return ProviderZone == ELayoutPlacementZone::Edge
				|| ProviderZone == ELayoutPlacementZone::Corner;
		case ELayoutPlacementZone::Edge:
		case ELayoutPlacementZone::Corner:
			return ProviderZone == ELayoutPlacementZone::Perimeter;
		case ELayoutPlacementZone::Interior:
			return ProviderZone == ELayoutPlacementZone::Core;
		case ELayoutPlacementZone::Core:
			return ProviderZone == ELayoutPlacementZone::Interior;
		case ELayoutPlacementZone::Any:
		default:
			return false;
		}
	}

	/** Compiles matching module/composite and direct-child source choices without placing them. */
	inline void CompileProviderChoiceSummaries(
		const FLayoutId EffectiveSnapshotId,
		const TArray<FHardDemand>& Demands,
		const TArray<FLayoutRegionContentEntrySolveSnapshot>& Entries,
		TArray<FProviderChoiceSummary>& OutSummaries)
	{
		OutSummaries.Reset();
		for (const FHardDemand& Demand : Demands)
		{
			for (const FLayoutRegionContentEntrySolveSnapshot& Entry : Entries)
			{
				if (Entry.EntryId.IsNone()
					|| !DoesProvidedFeatureSetMatchDemand(
						Entry.ProvidedZoneFeatures,
						Demand))
				{
					continue;
				}

				const ELayoutPlacementZone ProviderZone =
					Entry.ContentKind == ELayoutRegionContentKind::ChildRegion
						? Entry.ChildPlacementZone
						: Entry.ModulePlacementZone;
				if (!DoPotentialPlacementZonesOverlap(ProviderZone, Demand.Zone))
				{
					continue;
				}

				FProviderChoiceSummary& Summary = OutSummaries.AddDefaulted_GetRef();
				Summary.ProviderSummaryId = FLayoutId(*FString::Printf(
					TEXT("%s.ZoneFeatureChoice.%s.%s"),
					*EffectiveSnapshotId.ToString(),
					*Demand.RequirementId.ToString(),
					*Entry.EntryId.ToString()));
				Summary.RequirementId = Demand.RequirementId;
				Summary.SourceContentEntryId = Entry.EntryId;
				Summary.ContentKind = Entry.ContentKind;
				Summary.Weight = FMath::Max(1, Entry.Weight);
				if (Entry.ContentKind == ELayoutRegionContentKind::ChildRegion)
				{
					Summary.PlacementZone = ProviderZone;
					Summary.LevelPlacementPolicy = Entry.ChildLevelPlacementPolicy;
					Summary.SpecificLevel = Entry.ChildSpecificLevel;
					Summary.bOptional = Entry.bChildOptional;
				}
				else
				{
					Summary.PlacementZone = ProviderZone;
					Summary.LevelPlacementPolicy = Entry.ModuleLevelPlacementPolicy;
					Summary.SpecificLevel = Entry.ModuleSpecificLevel;
					Summary.bOptional = Entry.bModuleOptional;
				}
			}
		}

		OutSummaries.Sort([](
			const FProviderChoiceSummary& Left,
			const FProviderChoiceSummary& Right)
		{
			if (Left.RequirementId != Right.RequirementId)
			{
				return Left.RequirementId.LexicalLess(Right.RequirementId);
			}
			return Left.SourceContentEntryId.LexicalLess(
				Right.SourceContentEntryId);
		});
	}

	/** Compiles direct-child capacity used while parent module search waits for recursive placement. */
	inline void CompileDirectChildExternalProviderCapacity(
		const FLayoutId EffectiveSnapshotId,
		const TArray<FLayoutZoneFeatureRequirement>& Requirements,
		const TArray<FLayoutRegionContentEntrySolveSnapshot>& Entries,
		TMap<FLayoutId, int32>& OutCapacityByRequirementId)
	{
		OutCapacityByRequirementId.Reset();
		TArray<FHardDemand> Demands;
		CompileHardDemands(Requirements, Demands);
		TArray<FProviderChoiceSummary> ProviderChoices;
		CompileProviderChoiceSummaries(
			EffectiveSnapshotId,
			Demands,
			Entries,
			ProviderChoices);
		for (const FHardDemand& Demand : Demands)
		{
			if (ProviderChoices.ContainsByPredicate(
				[&Demand](const FProviderChoiceSummary& Choice)
				{
					return Choice.RequirementId == Demand.RequirementId
						&& Choice.ContentKind == ELayoutRegionContentKind::ChildRegion;
				}))
			{
				OutCapacityByRequirementId.Add(Demand.RequirementId, Demand.MinCount);
			}
		}
	}

	/** Rejects a frozen request whose mandatory minimum has neither precommitted credit nor any authored provider source. */
	inline bool ValidateStaticProviderPotential(
		const FLayoutId EffectiveSnapshotId,
		const FLayoutId ContentSetSnapshotId,
		const TArray<FLayoutZoneFeatureRequirement>& Requirements,
		const TArray<FLayoutRegionContentEntrySolveSnapshot>& Entries,
		const TArray<FLayoutZoneFeatureProviderCommitment>& PrecommittedCommitments,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();
		TArray<FHardDemand> Demands;
		CompileHardDemands(Requirements, Demands);
		TArray<FProviderChoiceSummary> ProviderChoices;
		CompileProviderChoiceSummaries(
			EffectiveSnapshotId,
			Demands,
			Entries,
			ProviderChoices);

		for (const FHardDemand& Demand : Demands)
		{
			if (Demand.MinCount <= 0)
			{
				continue;
			}

			int32 PrecommittedCount = 0;
			for (const FLayoutZoneFeatureProviderCommitment& Commitment : PrecommittedCommitments)
			{
				PrecommittedCount += Commitment.RequirementId == Demand.RequirementId;
			}
			if (PrecommittedCount >= Demand.MinCount
				|| ProviderChoices.ContainsByPredicate(
					[&Demand](const FProviderChoiceSummary& Choice)
					{
						return Choice.RequirementId == Demand.RequirementId;
					}))
			{
				continue;
			}

			TArray<FString> MissingFeatureEntries;
			TArray<FString> ZoneMismatchEntries;
			for (const FLayoutRegionContentEntrySolveSnapshot& Entry : Entries)
			{
				const FString EntryName = Entry.EntryId.IsNone()
					? TEXT("<unnamed>")
					: Entry.EntryId.ToString();
				if (!DoesProvidedFeatureSetMatchDemand(Entry.ProvidedZoneFeatures, Demand))
				{
					MissingFeatureEntries.Add(EntryName);
					continue;
				}

				const ELayoutPlacementZone ProviderZone =
					Entry.ContentKind == ELayoutRegionContentKind::ChildRegion
						? Entry.ChildPlacementZone
						: Entry.ModulePlacementZone;
				if (!DoPotentialPlacementZonesOverlap(ProviderZone, Demand.Zone))
				{
					ZoneMismatchEntries.Add(FString::Printf(
						TEXT("%s(%s)"),
						*EntryName,
						*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(
							static_cast<int64>(ProviderZone))));
				}
			}
			MissingFeatureEntries.Sort();
			ZoneMismatchEntries.Sort();
			OutFailureReason = FString::Printf(
				TEXT("Hard zone-feature requirement '%s' needs MinCount=%d, but frozen content-set snapshot '%s' has %d precommitted providers and no statically compatible module, composite, or direct-child source. RequiredFeatures=%s MatchMode=%s Zone=%s MissingFeatureEntries=%s ZoneMismatchEntries=%s."),
				*Demand.RequirementId.ToString(),
				Demand.MinCount,
				ContentSetSnapshotId.IsNone() ? TEXT("<none>") : *ContentSetSnapshotId.ToString(),
				PrecommittedCount,
				*Demand.RequiredFeatures.ToStringSimple(),
				*StaticEnum<ELayoutZoneFeatureMatchMode>()->GetNameStringByValue(
					static_cast<int64>(Demand.MatchMode)),
				*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(
					static_cast<int64>(Demand.Zone)),
				MissingFeatureEntries.IsEmpty()
					? TEXT("<none>")
					: *FString::Join(MissingFeatureEntries, TEXT(",")),
				ZoneMismatchEntries.IsEmpty()
					? TEXT("<none>")
					: *FString::Join(ZoneMismatchEntries, TEXT(",")));
			return false;
		}
		return true;
	}

	/** Removes feature credit owned by suppressed passive module placements while retaining direct-child commitments. */
	inline void RemoveSuppressedPassiveModuleCommitments(
		const TSet<FIntVector>& SuppressedCells,
		TArray<FLayoutZoneFeatureProviderCommitment>& InOutCommitments)
	{
		InOutCommitments.RemoveAll(
			[&SuppressedCells](const FLayoutZoneFeatureProviderCommitment& Commitment)
			{
				return !Commitment.ModuleSnapshotId.IsNone()
					&& SuppressedCells.Contains(Commitment.Cell);
			});
	}

	/** Builds deterministic provider identity without pointer or container-order authority. */
	inline FLayoutId BuildProviderCommitmentId(
		const FLayoutId EffectiveSnapshotId,
		const FString& RegionDebugPath,
		const FName RequirementId,
		const FName SourceContentEntryId,
		const FIntVector& RootCell,
		const int32 ModuleLevelIndex,
		const int32 TerrainStageIndex)
	{
		return FLayoutId(*FString::Printf(
			TEXT("%s.ZoneFeature.%s.%s.%s.%d.%d.%d.L%d.S%d"),
			*EffectiveSnapshotId.ToString(),
			RegionDebugPath.IsEmpty() ? TEXT("Region") : *RegionDebugPath,
			*RequirementId.ToString(),
			*SourceContentEntryId.ToString(),
			RootCell.X,
			RootCell.Y,
			RootCell.Z,
			ModuleLevelIndex,
			TerrainStageIndex));
	}

	/** Resolves one physical placement root to its canonical finalized planned cell. */
	inline const FLayoutPlannedCell* FindFinalizedPlannedCell(
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FIntVector& PhysicalCell)
	{
		return PlannedCells.FindByPredicate([&PhysicalCell](const FLayoutPlannedCell& PlannedCell)
		{
			return PlannedCell.Cell == PhysicalCell;
		});
	}

	/** Tests feature-credit eligibility from finalized zone and authored-level metadata. */
	inline bool DoesFinalizedCellMatchRequirementZone(
		const FLayoutPlannedCell& PlannedCell,
		const FIntPoint& FootprintSize,
		const ELayoutPlacementZone RequirementZone)
	{
		if (PlannedCell.bIsBridgeCell)
		{
			return false;
		}

		const int32 ModuleLevel = PlannedCell.ModuleLevelIndex != INDEX_NONE
			? PlannedCell.ModuleLevelIndex
			: PlannedCell.Cell.Z;
		const bool bCore = ModuleLevel == 0
			&& FMath::Abs(static_cast<float>(PlannedCell.Cell.X) - static_cast<float>(FootprintSize.X - 1) * 0.5f) <= 0.5f
			&& FMath::Abs(static_cast<float>(PlannedCell.Cell.Y) - static_cast<float>(FootprintSize.Y - 1) * 0.5f) <= 0.5f;

		switch (RequirementZone)
		{
		case ELayoutPlacementZone::Any:
			return true;
		case ELayoutPlacementZone::Perimeter:
			return PlannedCell.PlacementZone == ELayoutPlacementZone::Edge
				|| PlannedCell.PlacementZone == ELayoutPlacementZone::Corner;
		case ELayoutPlacementZone::Edge:
			return PlannedCell.PlacementZone == ELayoutPlacementZone::Edge;
		case ELayoutPlacementZone::Corner:
			return PlannedCell.PlacementZone == ELayoutPlacementZone::Corner;
		case ELayoutPlacementZone::Interior:
			return PlannedCell.PlacementZone == ELayoutPlacementZone::Interior;
		case ELayoutPlacementZone::Core:
			return bCore && (PlannedCell.PlacementZone == ELayoutPlacementZone::Interior
				|| PlannedCell.PlacementZone == ELayoutPlacementZone::Core);
		default:
			return false;
		}
	}
}
