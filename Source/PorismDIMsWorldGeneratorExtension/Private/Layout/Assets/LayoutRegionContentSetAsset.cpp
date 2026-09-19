// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutRegionContentSetAsset.h"

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Diagnostics/LayoutEditorMessageLog.h"
#include "Layout/Types/LayoutGameplayTags.h"

namespace
{
	bool IsContentSetLeafModuleSingleCell(const ULayoutModuleAsset* Module)
	{
		if (Module == nullptr)
		{
			return false;
		}

		const TArray<FIntVector> OccupiedLocalCells = Module->GetOccupiedLocalCells();
		return OccupiedLocalCells.Num() == 1
			&& OccupiedLocalCells[0] == FIntVector::ZeroValue;
	}

	bool IsContentSetBoundaryLikePlacementZone(const ELayoutPlacementZone Zone)
	{
		return Zone == ELayoutPlacementZone::Perimeter
			|| Zone == ELayoutPlacementZone::Edge
			|| Zone == ELayoutPlacementZone::Corner;
	}

	bool DoesContentSetPlacementZoneOverlapTargetZone(
		const ELayoutPlacementZone EntryPlacementZone,
		const ELayoutPlacementZone TargetZone)
	{
		if (EntryPlacementZone == ELayoutPlacementZone::Any || TargetZone == ELayoutPlacementZone::Any)
		{
			return true;
		}

		if (EntryPlacementZone == TargetZone)
		{
			return true;
		}

		switch (TargetZone)
		{
		case ELayoutPlacementZone::Perimeter:
			return EntryPlacementZone == ELayoutPlacementZone::Edge
				|| EntryPlacementZone == ELayoutPlacementZone::Corner;
		case ELayoutPlacementZone::Edge:
		case ELayoutPlacementZone::Corner:
			return EntryPlacementZone == ELayoutPlacementZone::Perimeter;
		case ELayoutPlacementZone::Interior:
			return EntryPlacementZone == ELayoutPlacementZone::Core;
		case ELayoutPlacementZone::Core:
			return EntryPlacementZone == ELayoutPlacementZone::Interior;
		case ELayoutPlacementZone::Any:
		default:
			return false;
		}
	}

	void AppendContentSetValidationResult(
		FDataValidationContext& Context,
		const FLayoutValidationResult& ValidationResult)
	{
		for (const FLayoutValidationMessage& Message : ValidationResult.Messages)
		{
			if (Message.Severity == ELayoutValidationSeverity::Error)
			{
				Context.AddError(FText::FromString(Message.Message));
			}
			else
			{
				Context.AddWarning(FText::FromString(Message.Message));
			}
		}
	}

	FString BuildContentSetValidationMessage(
		const ULayoutRegionContentSetAsset* ContentSet,
		const FString& Summary,
		const TArray<FString>& DetailLines)
	{
		FString Message = Summary;
		Message += FString::Printf(TEXT("\nContentSet: %s"), ContentSet != nullptr ? *ContentSet->GetName() : TEXT("<none>"));
		for (const FString& DetailLine : DetailLines)
		{
			if (!DetailLine.IsEmpty())
			{
				Message += TEXT("\n");
				Message += DetailLine;
			}
		}

		return Message;
	}

	bool IsContentSetLayoutFeatureTag(const FGameplayTag& Tag)
	{
		return Tag.IsValid() && Tag.ToString().StartsWith(TEXT("Layout.Feature."));
	}

	bool DoesLevelPlacementPolicyRequireSpecificLevel(const ELayoutLevelPlacementPolicy Policy)
	{
		return Policy == ELayoutLevelPlacementPolicy::SpecificLevel;
	}

	bool DoesModuleContentSettingsReferenceExactlyOneSource(const FLayoutModuleContentSettings& Settings);
	bool ModuleContentSettingsSupportsIntent(
		const FLayoutModuleContentSettings& Settings,
		const ELayoutCellIntent Intent);
	FGameplayTagContainer GetModuleContentSettingsTraversalChannels(const FLayoutModuleContentSettings& Settings);
	TArray<FIntVector> GetModuleContentSettingsOccupiedLocalCells(const FLayoutModuleContentSettings& Settings);
	FIntVector GetModuleContentSettingsSharedCellSizeInBlocks(const FLayoutModuleContentSettings& Settings);
	bool ModuleContentSettingsCanDeriveSeamSpanOffers(const FLayoutModuleContentSettings& Settings);
	FString DescribeModuleContentSource(const FLayoutModuleContentSettings& Settings);

	bool ProfileHasVerticalAccessCapableContent(const ULayoutProfileAsset* Profile)
	{
		if (Profile == nullptr || Profile->ContentSet == nullptr)
		{
			return false;
		}

		for (const FLayoutRegionContentEntry& Entry : Profile->ContentSet->Entries)
		{
			if (Entry.ContentKind != ELayoutRegionContentKind::Module
				|| !DoesModuleContentSettingsReferenceExactlyOneSource(Entry.ModuleSettings))
			{
				continue;
			}

			if (ModuleContentSettingsSupportsIntent(Entry.ModuleSettings, ELayoutCellIntent::VerticalAccess))
			{
				return true;
			}
		}

		return false;
	}

	bool ProfileHasBoundaryEntryCapableContent(const ULayoutProfileAsset* Profile)
	{
		if (Profile == nullptr || Profile->ContentSet == nullptr)
		{
			return false;
		}

		for (const FLayoutRegionContentEntry& Entry : Profile->ContentSet->Entries)
		{
			if (Entry.ContentKind != ELayoutRegionContentKind::Module
				|| !DoesModuleContentSettingsReferenceExactlyOneSource(Entry.ModuleSettings))
			{
				continue;
			}

			if (ModuleContentSettingsSupportsIntent(Entry.ModuleSettings, ELayoutCellIntent::Entry)
				&& DoesContentSetPlacementZoneOverlapTargetZone(
					Entry.ModuleSettings.PlacementZone,
					ELayoutPlacementZone::Perimeter))
			{
				return true;
			}
		}

		return false;
	}

	bool DoesModuleContentSettingsReferenceExactlyOneSource(const FLayoutModuleContentSettings& Settings)
	{
		const bool bHasLeafModule = Settings.Module != nullptr;
		const bool bHasCompositeModule = Settings.CompositeModule != nullptr;
		return bHasLeafModule != bHasCompositeModule;
	}

	bool ModuleContentSettingsSupportsIntent(
		const FLayoutModuleContentSettings& Settings,
		const ELayoutCellIntent Intent)
	{
		if (Settings.Module != nullptr)
		{
			return Settings.Module->SupportsIntent(Intent);
		}
		if (Settings.CompositeModule != nullptr)
		{
			return Settings.CompositeModule->SupportsIntent(Intent);
		}

		return false;
	}

	FGameplayTagContainer GetModuleContentSettingsTraversalChannels(const FLayoutModuleContentSettings& Settings)
	{
		if (Settings.Module != nullptr)
		{
			return Settings.Module->GetEffectiveTraversalChannels();
		}
		if (Settings.CompositeModule != nullptr)
		{
			return Settings.CompositeModule->GetEffectiveTraversalChannels();
		}

		return FGameplayTagContainer();
	}

	TArray<FIntVector> GetModuleContentSettingsOccupiedLocalCells(const FLayoutModuleContentSettings& Settings)
	{
		if (Settings.Module != nullptr)
		{
			return Settings.Module->GetOccupiedLocalCells();
		}
		if (Settings.CompositeModule != nullptr)
		{
			return Settings.CompositeModule->GetOccupiedLocalCells();
		}

		return {};
	}

	FIntVector GetModuleContentSettingsSharedCellSizeInBlocks(const FLayoutModuleContentSettings& Settings)
	{
		if (Settings.Module != nullptr)
		{
			return Settings.Module->GetEffectiveCellSizeInBlocks();
		}
		if (Settings.CompositeModule != nullptr)
		{
			return Settings.CompositeModule->GetSharedCellSizeInBlocks();
		}

		return FIntVector::ZeroValue;
	}

	bool TryResolveDerivedContentSetSharedCellSizeInBlocks(
		const TArray<FLayoutRegionContentEntry>& Entries,
		FIntVector& OutSharedCellSizeInBlocks)
	{
		for (const FLayoutRegionContentEntry& Entry : Entries)
		{
			if (Entry.ContentKind != ELayoutRegionContentKind::Module
				|| !DoesModuleContentSettingsReferenceExactlyOneSource(Entry.ModuleSettings))
			{
				continue;
			}

			const FIntVector DerivedSharedCellSizeInBlocks =
				GetModuleContentSettingsSharedCellSizeInBlocks(Entry.ModuleSettings);
			if (DerivedSharedCellSizeInBlocks != FIntVector::ZeroValue)
			{
				OutSharedCellSizeInBlocks = DerivedSharedCellSizeInBlocks;
				return true;
			}
		}

		return false;
	}

	bool ModuleContentSettingsCanDeriveSeamSpanOffers(const FLayoutModuleContentSettings& Settings)
	{
		return ModuleContentSettingsSupportsIntent(Settings, ELayoutCellIntent::Boundary)
			|| ModuleContentSettingsSupportsIntent(Settings, ELayoutCellIntent::Entry)
			|| ModuleContentSettingsSupportsIntent(Settings, ELayoutCellIntent::VerticalAccess);
	}

	FString DescribeModuleContentSource(const FLayoutModuleContentSettings& Settings)
	{
		if (Settings.Module != nullptr)
		{
			return FString::Printf(TEXT("Module: %s"), *Settings.Module->GetName());
		}
		if (Settings.CompositeModule != nullptr)
		{
			return FString::Printf(TEXT("CompositeModule: %s"), *Settings.CompositeModule->GetName());
		}

		return TEXT("Module source: <none>");
	}

	void AppendPossibleLevelsForPolicy(
		const ELayoutLevelPlacementPolicy LevelPlacementPolicy,
		const int32 SpecificLevel,
		const int32 LevelCount,
		TSet<int32>& InOutPossibleLevels)
	{
		switch (LevelPlacementPolicy)
		{
		case ELayoutLevelPlacementPolicy::GroundOnly:
			InOutPossibleLevels.Add(0);
			return;

		case ELayoutLevelPlacementPolicy::SpecificLevel:
			if (SpecificLevel >= 0 && SpecificLevel < LevelCount)
			{
				InOutPossibleLevels.Add(SpecificLevel);
			}
			return;

		case ELayoutLevelPlacementPolicy::TopLevelOnly:
			InOutPossibleLevels.Add(FMath::Max(0, LevelCount - 1));
			return;

		case ELayoutLevelPlacementPolicy::AboveGroundLevel:
			for (int32 LevelIndex = 1; LevelIndex < LevelCount; ++LevelIndex)
			{
				InOutPossibleLevels.Add(LevelIndex);
			}
			return;

		case ELayoutLevelPlacementPolicy::BelowTopLevel:
			for (int32 LevelIndex = 0; LevelIndex < FMath::Max(0, LevelCount - 1); ++LevelIndex)
			{
				InOutPossibleLevels.Add(LevelIndex);
			}
			return;

		case ELayoutLevelPlacementPolicy::AnyLevel:
		default:
			for (int32 LevelIndex = 0; LevelIndex < LevelCount; ++LevelIndex)
			{
				InOutPossibleLevels.Add(LevelIndex);
			}
			return;
		}
	}

	void AppendPossibleCoveredLevelsForModuleEntry(
		const FLayoutRegionContentEntry& Entry,
		const int32 LevelCount,
		TSet<int32>& InOutPossibleLevels)
	{
		if (Entry.ContentKind != ELayoutRegionContentKind::Module
			|| !DoesModuleContentSettingsReferenceExactlyOneSource(Entry.ModuleSettings)
			|| LevelCount <= 0)
		{
			return;
		}

		TSet<int32> CandidateBaseLevels;
		AppendPossibleLevelsForPolicy(
			Entry.ModuleSettings.LevelPlacementPolicy,
			Entry.ModuleSettings.SpecificLevel,
			LevelCount,
			CandidateBaseLevels);

		const TArray<FIntVector> OccupiedLocalCells =
			GetModuleContentSettingsOccupiedLocalCells(Entry.ModuleSettings);
		if (OccupiedLocalCells.IsEmpty())
		{
			return;
		}

		for (const int32 CandidateBaseLevel : CandidateBaseLevels)
		{
			bool bFitsWithinProfileLevels = true;
			for (const FIntVector& OccupiedLocalCell : OccupiedLocalCells)
			{
				const int32 CoveredLevel = CandidateBaseLevel + OccupiedLocalCell.Z;
				if (CoveredLevel < 0 || CoveredLevel >= LevelCount)
				{
					bFitsWithinProfileLevels = false;
					break;
				}
			}

			if (!bFitsWithinProfileLevels)
			{
				continue;
			}

			for (const FIntVector& OccupiedLocalCell : OccupiedLocalCells)
			{
				InOutPossibleLevels.Add(CandidateBaseLevel + OccupiedLocalCell.Z);
			}
		}
	}

	bool ProfileHasBoundaryEntryCapableContentOnDistinctLevels(const ULayoutProfileAsset* Profile)
	{
		if (Profile == nullptr || Profile->ContentSet == nullptr || Profile->LevelCount <= 1)
		{
			return false;
		}

		TSet<int32> PossibleLevels;
		for (const FLayoutRegionContentEntry& Entry : Profile->ContentSet->Entries)
		{
			if (Entry.ContentKind != ELayoutRegionContentKind::Module
				|| !DoesModuleContentSettingsReferenceExactlyOneSource(Entry.ModuleSettings)
				|| !ModuleContentSettingsSupportsIntent(Entry.ModuleSettings, ELayoutCellIntent::Entry)
				|| !DoesContentSetPlacementZoneOverlapTargetZone(
					Entry.ModuleSettings.PlacementZone,
					ELayoutPlacementZone::Perimeter))
			{
				continue;
			}

			AppendPossibleCoveredLevelsForModuleEntry(
				Entry,
				Profile->LevelCount,
				PossibleLevels);
			if (PossibleLevels.Num() >= 2)
			{
				return true;
			}
		}

		return false;
	}

	bool ProfileCanCarryVerticalAccessAcrossBoundaryEntryLevels(const ULayoutProfileAsset* Profile)
	{
		if (Profile == nullptr || Profile->ContentSet == nullptr || Profile->LevelCount <= 1)
		{
			return false;
		}

		TSet<int32> BoundaryEntryLevels;
		TSet<int32> VerticalAccessCoveredLevels;
		for (const FLayoutRegionContentEntry& Entry : Profile->ContentSet->Entries)
		{
			if (Entry.ContentKind != ELayoutRegionContentKind::Module
				|| !DoesModuleContentSettingsReferenceExactlyOneSource(Entry.ModuleSettings))
			{
				continue;
			}

			if (ModuleContentSettingsSupportsIntent(Entry.ModuleSettings, ELayoutCellIntent::Entry)
				&& DoesContentSetPlacementZoneOverlapTargetZone(
					Entry.ModuleSettings.PlacementZone,
					ELayoutPlacementZone::Perimeter))
			{
				AppendPossibleCoveredLevelsForModuleEntry(
					Entry,
					Profile->LevelCount,
					BoundaryEntryLevels);
			}

			if (ModuleContentSettingsSupportsIntent(Entry.ModuleSettings, ELayoutCellIntent::VerticalAccess))
			{
				AppendPossibleCoveredLevelsForModuleEntry(
					Entry,
					Profile->LevelCount,
					VerticalAccessCoveredLevels);
			}
		}

		TArray<int32> SortedBoundaryEntryLevels = BoundaryEntryLevels.Array();
		SortedBoundaryEntryLevels.Sort();
		for (int32 LowerIndex = 0; LowerIndex < SortedBoundaryEntryLevels.Num(); ++LowerIndex)
		{
			for (int32 UpperIndex = LowerIndex + 1; UpperIndex < SortedBoundaryEntryLevels.Num(); ++UpperIndex)
			{
				const int32 LowerLevel = SortedBoundaryEntryLevels[LowerIndex];
				const int32 UpperLevel = SortedBoundaryEntryLevels[UpperIndex];
				bool bAllCovered = true;
				for (int32 LevelIndex = LowerLevel; LevelIndex <= UpperLevel; ++LevelIndex)
				{
					if (!VerticalAccessCoveredLevels.Contains(LevelIndex))
					{
						bAllCovered = false;
						break;
					}
				}

				if (bAllCovered)
				{
					return true;
				}
			}
		}

		return false;
	}

	bool ProfileHasGenerallyConnectableBoundaryEntryLevels(const ULayoutProfileAsset* Profile)
	{
		if (Profile == nullptr || Profile->ContentSet == nullptr || Profile->LevelCount <= 1)
		{
			return false;
		}

		TMap<int32, FGameplayTagContainer> TraversalChannelsByLevel;
		for (const FLayoutRegionContentEntry& Entry : Profile->ContentSet->Entries)
		{
			if (Entry.ContentKind != ELayoutRegionContentKind::Module
				|| !DoesModuleContentSettingsReferenceExactlyOneSource(Entry.ModuleSettings)
				|| !ModuleContentSettingsSupportsIntent(Entry.ModuleSettings, ELayoutCellIntent::Entry)
				|| !DoesContentSetPlacementZoneOverlapTargetZone(
					Entry.ModuleSettings.PlacementZone,
					ELayoutPlacementZone::Perimeter))
			{
				continue;
			}

			const FGameplayTagContainer TraversalChannels =
				GetModuleContentSettingsTraversalChannels(Entry.ModuleSettings);
			if (TraversalChannels.IsEmpty())
			{
				continue;
			}

			TSet<int32> PossibleCoveredLevels;
			AppendPossibleCoveredLevelsForModuleEntry(
				Entry,
				Profile->LevelCount,
				PossibleCoveredLevels);
			for (const int32 LevelIndex : PossibleCoveredLevels)
			{
				TraversalChannelsByLevel.FindOrAdd(LevelIndex).AppendTags(TraversalChannels);
			}
		}

		TArray<int32> SortedLevels;
		TraversalChannelsByLevel.GetKeys(SortedLevels);
		SortedLevels.Sort();
		for (int32 LowerIndex = 0; LowerIndex < SortedLevels.Num(); ++LowerIndex)
		{
			const FGameplayTagContainer* LowerChannels =
				TraversalChannelsByLevel.Find(SortedLevels[LowerIndex]);
			if (LowerChannels == nullptr || LowerChannels->IsEmpty())
			{
				continue;
			}

			for (int32 UpperIndex = LowerIndex + 1; UpperIndex < SortedLevels.Num(); ++UpperIndex)
			{
				const FGameplayTagContainer* UpperChannels =
					TraversalChannelsByLevel.Find(SortedLevels[UpperIndex]);
				if (UpperChannels != nullptr
					&& UpperChannels->HasAnyExact(*LowerChannels))
				{
					return true;
				}
			}
		}

		return false;
	}

	bool ProfileHasGenerallyConnectableBoundaryEntryLevelPairWithVerticalCoverage(
		const ULayoutProfileAsset* Profile)
	{
		if (Profile == nullptr || Profile->ContentSet == nullptr || Profile->LevelCount <= 1)
		{
			return false;
		}

		TMap<int32, FGameplayTagContainer> TraversalChannelsByLevel;
		TSet<int32> VerticalAccessCoveredLevels;
		for (const FLayoutRegionContentEntry& Entry : Profile->ContentSet->Entries)
		{
			if (Entry.ContentKind != ELayoutRegionContentKind::Module
				|| !DoesModuleContentSettingsReferenceExactlyOneSource(Entry.ModuleSettings))
			{
				continue;
			}

			TSet<int32> PossibleCoveredLevels;
			AppendPossibleCoveredLevelsForModuleEntry(
				Entry,
				Profile->LevelCount,
				PossibleCoveredLevels);

			if (ModuleContentSettingsSupportsIntent(Entry.ModuleSettings, ELayoutCellIntent::Entry)
				&& DoesContentSetPlacementZoneOverlapTargetZone(
					Entry.ModuleSettings.PlacementZone,
					ELayoutPlacementZone::Perimeter))
			{
				const FGameplayTagContainer TraversalChannels =
					GetModuleContentSettingsTraversalChannels(Entry.ModuleSettings);
				if (!TraversalChannels.IsEmpty())
				{
					for (const int32 LevelIndex : PossibleCoveredLevels)
					{
						TraversalChannelsByLevel.FindOrAdd(LevelIndex).AppendTags(TraversalChannels);
					}
				}
			}

			if (ModuleContentSettingsSupportsIntent(Entry.ModuleSettings, ELayoutCellIntent::VerticalAccess))
			{
				for (const int32 LevelIndex : PossibleCoveredLevels)
				{
					VerticalAccessCoveredLevels.Add(LevelIndex);
				}
			}
		}

		TArray<int32> SortedLevels;
		TraversalChannelsByLevel.GetKeys(SortedLevels);
		SortedLevels.Sort();
		for (int32 LowerIndex = 0; LowerIndex < SortedLevels.Num(); ++LowerIndex)
		{
			const int32 LowerLevel = SortedLevels[LowerIndex];
			const FGameplayTagContainer* LowerChannels =
				TraversalChannelsByLevel.Find(LowerLevel);
			if (LowerChannels == nullptr || LowerChannels->IsEmpty())
			{
				continue;
			}

			for (int32 UpperIndex = LowerIndex + 1; UpperIndex < SortedLevels.Num(); ++UpperIndex)
			{
				const int32 UpperLevel = SortedLevels[UpperIndex];
				const FGameplayTagContainer* UpperChannels =
					TraversalChannelsByLevel.Find(UpperLevel);
				if (UpperChannels == nullptr
					|| !UpperChannels->HasAnyExact(*LowerChannels))
				{
					continue;
				}

				bool bAllCovered = true;
				for (int32 LevelIndex = LowerLevel; LevelIndex <= UpperLevel; ++LevelIndex)
				{
					if (!VerticalAccessCoveredLevels.Contains(LevelIndex))
					{
						bAllCovered = false;
						break;
					}
				}

				if (bAllCovered)
				{
					return true;
				}
			}
		}

		return false;
	}
}

FIntVector ULayoutRegionContentSetAsset::GetDerivedSharedCellSizeInBlocks() const
{
	FIntVector DerivedSharedCellSizeInBlocks = FIntVector::ZeroValue;
	if (TryResolveDerivedContentSetSharedCellSizeInBlocks(Entries, DerivedSharedCellSizeInBlocks))
	{
		return DerivedSharedCellSizeInBlocks;
	}

	return FIntVector::ZeroValue;
}

FIntVector ULayoutRegionContentSetAsset::GetSharedCellSizeInBlocks() const
{
	return GetDerivedSharedCellSizeInBlocks();
}

TArray<ULayoutModuleAsset*> ULayoutRegionContentSetAsset::GetModulesForRole(const ELayoutModuleRole Role) const
{
	TArray<ULayoutModuleAsset*> Result;
	for (const FLayoutRegionContentEntry& Entry : Entries)
	{
		if (Entry.ContentKind != ELayoutRegionContentKind::Module)
		{
			continue;
		}

		ULayoutModuleAsset* Module = Entry.ModuleSettings.Module;
		if (Module != nullptr && Module->HasEffectiveRole(Role))
		{
			Result.Add(Module);
		}
	}

	return Result;
}

FLayoutValidationResult ULayoutRegionContentSetAsset::ValidateContentSet() const
{
	FLayoutValidationResult Result;

	if (Entries.IsEmpty())
	{
		Result.AddError(BuildContentSetValidationMessage(
			this,
			TEXT("Layout region content set has no entries."),
			{
				TEXT("Problem: The solver has no modules or child regions to choose from."),
				TEXT("Fix: Add at least one content entry.")
			}));
		return Result;
	}

	TSet<FName> SeenEntryIds;
	for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
	{
		const FLayoutRegionContentEntry& Entry = Entries[EntryIndex];
		const FString EntryPrefix = FString::Printf(TEXT("Entries[%d]"), EntryIndex);

		if (Entry.EntryId.IsNone())
		{
			Result.AddError(BuildContentSetValidationMessage(
				this,
				FString::Printf(TEXT("%s is missing its EntryId."), *EntryPrefix),
				{
					FString::Printf(TEXT("Entry index: %d"), EntryIndex),
					TEXT("Problem: Every content entry needs a stable EntryId."),
					TEXT("Fix: Assign a unique non-empty EntryId.")
				}));
		}
		else if (SeenEntryIds.Contains(Entry.EntryId))
		{
			Result.AddError(BuildContentSetValidationMessage(
				this,
				FString::Printf(TEXT("%s duplicates EntryId '%s'."), *EntryPrefix, *Entry.EntryId.ToString()),
				{
					TEXT("Problem: EntryIds must be unique within one content set."),
					TEXT("Fix: Rename one of the duplicate entries.")
				}));
		}
		else
		{
			SeenEntryIds.Add(Entry.EntryId);
		}

		if (Entry.Weight <= 0)
		{
			Result.AddError(BuildContentSetValidationMessage(
				this,
				FString::Printf(TEXT("%s has an invalid weight."), *EntryPrefix),
				{
					FString::Printf(TEXT("EntryId: %s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
					FString::Printf(TEXT("Weight: %d"), Entry.Weight),
					TEXT("Fix: Set the weight to a positive value.")
			}));
		}

		TArray<FGameplayTag> ProvidedFeatureTags;
		Entry.ProvidedZoneFeatures.GetGameplayTagArray(ProvidedFeatureTags);
		for (const FGameplayTag& FeatureTag : ProvidedFeatureTags)
		{
			if (IsContentSetLayoutFeatureTag(FeatureTag))
			{
				continue;
			}

			Result.AddError(BuildContentSetValidationMessage(
				this,
				FString::Printf(TEXT("%s provides a non-layout feature tag."), *EntryPrefix),
				{
					FString::Printf(TEXT("EntryId: %s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
					FString::Printf(TEXT("Provided feature: %s"), *FeatureTag.ToString()),
					TEXT("Problem: Zone feature accounting only accepts tags under the Layout.Feature.* hierarchy."),
					TEXT("Fix: Rename this feature tag so it uses the Layout.Feature.* prefix, or remove it from ProvidedZoneFeatures.")
				}));
		}

		TSet<FName> SeenProviderIntentIds;
		for (int32 ProviderIntentIndex = 0; ProviderIntentIndex < Entry.ClosureProviderIntents.Num(); ++ProviderIntentIndex)
		{
			const FLayoutClosureProviderIntent& ProviderIntent = Entry.ClosureProviderIntents[ProviderIntentIndex];
			const FString IntentPrefix = FString::Printf(TEXT("%s.ClosureProviderIntents[%d]"), *EntryPrefix, ProviderIntentIndex);
			if (ProviderIntent.ProviderIntentId.IsNone())
			{
				Result.AddError(FString::Printf(TEXT("%s must have a non-empty ProviderIntentId."), *IntentPrefix));
			}
			else if (SeenProviderIntentIds.Contains(ProviderIntent.ProviderIntentId))
			{
				Result.AddError(FString::Printf(TEXT("%s uses duplicate ProviderIntentId '%s' within one content entry."), *IntentPrefix, *ProviderIntent.ProviderIntentId.ToString()));
			}
			else
			{
				SeenProviderIntentIds.Add(ProviderIntent.ProviderIntentId);
			}

			if (!IsContentSetBoundaryLikePlacementZone(ProviderIntent.Zone))
			{
				Result.AddWarning(FString::Printf(
					TEXT("%s uses Zone=%s. Current closure-provider solving is primarily intended for Perimeter, Edge, or Corner provider intent."),
					*IntentPrefix,
					*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(ProviderIntent.Zone))));
			}
		}

		TSet<FName> SeenSeamIntentIds;
		for (int32 SeamIntentIndex = 0; SeamIntentIndex < Entry.SeamProviderIntents.Num(); ++SeamIntentIndex)
		{
			const FLayoutSeamProviderIntent& SeamIntent = Entry.SeamProviderIntents[SeamIntentIndex];
			const FString IntentPrefix = FString::Printf(TEXT("%s.SeamProviderIntents[%d]"), *EntryPrefix, SeamIntentIndex);
			if (SeamIntent.SeamIntentId.IsNone())
			{
				Result.AddError(FString::Printf(TEXT("%s must have a non-empty SeamIntentId."), *IntentPrefix));
			}
			else if (SeenSeamIntentIds.Contains(SeamIntent.SeamIntentId))
			{
				Result.AddError(FString::Printf(TEXT("%s uses duplicate SeamIntentId '%s' within one content entry."), *IntentPrefix, *SeamIntent.SeamIntentId.ToString()));
			}
			else
			{
				SeenSeamIntentIds.Add(SeamIntent.SeamIntentId);
			}

			if (!SeamIntent.InterfaceFamily.IsValid())
			{
				Result.AddError(FString::Printf(TEXT("%s must define a valid InterfaceFamily gameplay tag."), *IntentPrefix));
			}

			if (!StaticEnum<ELayoutSeamJunctionUsage>()->IsValidEnumValue(
					static_cast<int64>(SeamIntent.JunctionUsage)))
			{
				Result.AddError(FString::Printf(
					TEXT("%s must define a valid JunctionUsage."),
					*IntentPrefix));
			}

			if (!SeamIntent.bCanOwnSeam && !SeamIntent.bCanAcceptSeam)
			{
				Result.AddError(FString::Printf(TEXT("%s must allow owning, accepting, or both for the seam intent."), *IntentPrefix));
			}
		}

		if (Entry.ContentKind == ELayoutRegionContentKind::Module)
		{
			if (DoesLevelPlacementPolicyRequireSpecificLevel(Entry.ModuleSettings.LevelPlacementPolicy)
				&& Entry.ModuleSettings.SpecificLevel < 0)
			{
				Result.AddError(BuildContentSetValidationMessage(
					this,
					FString::Printf(TEXT("%s uses an invalid module specific level."), *EntryPrefix),
					{
						FString::Printf(TEXT("EntryId: %s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
						FString::Printf(TEXT("Level placement policy: %s"), *StaticEnum<ELayoutLevelPlacementPolicy>()->GetNameStringByValue(static_cast<int64>(Entry.ModuleSettings.LevelPlacementPolicy))),
						FString::Printf(TEXT("Specific level: %d"), Entry.ModuleSettings.SpecificLevel),
						TEXT("Problem: SpecificLevel placement requires a non-negative specific level index."),
						TEXT("Fix: Set SpecificLevel to 0 or greater, or change the level placement policy.")
					}));
			}

			const bool bHasLeafModule = Entry.ModuleSettings.Module != nullptr;
			const bool bHasCompositeModule = Entry.ModuleSettings.CompositeModule != nullptr;
			if (!bHasLeafModule && !bHasCompositeModule)
			{
				Result.AddError(BuildContentSetValidationMessage(
					this,
					FString::Printf(TEXT("%s references no module or composite module."), *EntryPrefix),
					{
						FString::Printf(TEXT("EntryId: %s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
						TEXT("Fix: Assign either one leaf Module or one CompositeModule for this entry, or change the content kind if this should be a child region.")
					}));
				continue;
			}

			if (bHasLeafModule && bHasCompositeModule)
			{
				Result.AddError(BuildContentSetValidationMessage(
					this,
					FString::Printf(TEXT("%s references both a module and a composite module."), *EntryPrefix),
					{
						FString::Printf(TEXT("EntryId: %s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
						FString::Printf(TEXT("Module: %s"), *Entry.ModuleSettings.Module->GetName()),
						FString::Printf(TEXT("CompositeModule: %s"), *Entry.ModuleSettings.CompositeModule->GetName()),
						TEXT("Problem: Module-backed content entries must author exactly one source asset."),
						TEXT("Fix: Keep either the leaf Module or the CompositeModule reference, but not both.")
					}));
				continue;
			}

			if (Entry.ModuleSettings.Module != nullptr)
			{
				const FLayoutValidationResult ModuleValidation = Entry.ModuleSettings.Module->ValidateModule();
				Result.Messages.Append(ModuleValidation.Messages);

				// Multi-cell leaf module check is informational only — composite modules
			}
			else
			{
				const FLayoutValidationResult CompositeValidation = Entry.ModuleSettings.CompositeModule->ValidateCompositeModule();
				Result.Messages.Append(CompositeValidation.Messages);
			}

			if (!Entry.SeamProviderIntents.IsEmpty()
				&& !ModuleContentSettingsCanDeriveSeamSpanOffers(Entry.ModuleSettings))
			{
				Result.AddError(BuildContentSetValidationMessage(
					this,
					FString::Printf(TEXT("%s authors seam intents on a module entry that can never derive seam span offers."), *EntryPrefix),
					{
						FString::Printf(TEXT("EntryId: %s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
						DescribeModuleContentSource(Entry.ModuleSettings),
						TEXT("Problem: Seam capability compilation only derives seam span offers from modules that participate in Boundary, Entry, or VerticalAccess solving. This entry authors seam intents on a module that can never expose that seam surface."),
						TEXT("Fix: Add a Boundary, Entry, or VerticalAccess role to the module, or remove the seam intents from this content entry.")
					}));
			}

			for (int32 ProviderIntentIndex = 0; ProviderIntentIndex < Entry.ClosureProviderIntents.Num(); ++ProviderIntentIndex)
			{
				const FLayoutClosureProviderIntent& ProviderIntent = Entry.ClosureProviderIntents[ProviderIntentIndex];
				if (DoesContentSetPlacementZoneOverlapTargetZone(Entry.ModuleSettings.PlacementZone, ProviderIntent.Zone))
				{
					continue;
				}

				Result.AddError(BuildContentSetValidationMessage(
					this,
					FString::Printf(TEXT("%s.ClosureProviderIntents[%d] cannot be reached by the module entry placement zone."), *EntryPrefix, ProviderIntentIndex),
					{
						FString::Printf(TEXT("EntryId: %s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
						FString::Printf(TEXT("Module placement zone: %s"), *StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(Entry.ModuleSettings.PlacementZone))),
						FString::Printf(TEXT("Closure provider zone: %s"), *StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(ProviderIntent.Zone))),
						TEXT("Problem: This module entry is restricted to cells that can never participate in the authored closure-provider zone."),
						TEXT("Fix: Widen the module entry PlacementZone, or change the closure-provider zone to one this entry can actually occupy.")
					}));
			}

			continue;
		}

		if (DoesLevelPlacementPolicyRequireSpecificLevel(Entry.ChildRegionSettings.LevelPlacementPolicy)
			&& Entry.ChildRegionSettings.SpecificLevel < 0)
		{
			Result.AddError(BuildContentSetValidationMessage(
				this,
				FString::Printf(TEXT("%s uses an invalid child specific level."), *EntryPrefix),
				{
					FString::Printf(TEXT("EntryId: %s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
					FString::Printf(TEXT("Level placement policy: %s"), *StaticEnum<ELayoutLevelPlacementPolicy>()->GetNameStringByValue(static_cast<int64>(Entry.ChildRegionSettings.LevelPlacementPolicy))),
					FString::Printf(TEXT("Specific level: %d"), Entry.ChildRegionSettings.SpecificLevel),
					TEXT("Problem: SpecificLevel placement requires a non-negative specific level index."),
					TEXT("Fix: Set SpecificLevel to 0 or greater, or change the level placement policy.")
				}));
		}

		if (Entry.ChildRegionSettings.RegionProfile == nullptr)
		{
			Result.AddError(BuildContentSetValidationMessage(
				this,
				FString::Printf(TEXT("%s references no child region profile."), *EntryPrefix),
				{
					FString::Printf(TEXT("EntryId: %s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
					TEXT("Fix: Assign the child region profile for this entry, or change the content kind if this should be a module.")
				}));
			continue;
		}

		if (Entry.ChildRegionSettings.bContributesHostVerticalAccess
			&& Entry.ChildRegionSettings.RegionProfile->LevelCount <= 1)
		{
			Result.AddError(BuildContentSetValidationMessage(
				this,
				FString::Printf(TEXT("%s cannot contribute host vertical access from a single-level child profile."), *EntryPrefix),
				{
					FString::Printf(TEXT("EntryId: %s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
					FString::Printf(TEXT("ChildProfile: %s"), *Entry.ChildRegionSettings.RegionProfile->GetName()),
					FString::Printf(TEXT("Child LevelCount: %d"), Entry.ChildRegionSettings.RegionProfile->LevelCount),
					TEXT("Problem: child_contributes_host_vertical_access requires a child profile that can own a real cross-level ascent."),
					TEXT("Fix: Use a child profile with more than one level, or disable child_contributes_host_vertical_access for this entry.")
				}));
		}

		if (Entry.ChildRegionSettings.bContributesHostVerticalAccess
			&& !ProfileHasVerticalAccessCapableContent(Entry.ChildRegionSettings.RegionProfile))
		{
			Result.AddError(BuildContentSetValidationMessage(
				this,
				FString::Printf(TEXT("%s cannot contribute host vertical access because the child profile has no VerticalAccess-capable content."), *EntryPrefix),
				{
					FString::Printf(TEXT("EntryId: %s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
					FString::Printf(TEXT("ChildProfile: %s"), *Entry.ChildRegionSettings.RegionProfile->GetName()),
					FString::Printf(TEXT("Child ContentSet: %s"), Entry.ChildRegionSettings.RegionProfile->ContentSet != nullptr ? *Entry.ChildRegionSettings.RegionProfile->ContentSet->GetName() : TEXT("<none>")),
					TEXT("Problem: child_contributes_host_vertical_access requires at least one VerticalAccess-capable module entry in the child profile content set."),
					TEXT("Fix: Add VerticalAccess-capable child content, or disable child_contributes_host_vertical_access for this entry.")
				}));
		}

		if (Entry.ChildRegionSettings.bContributesHostVerticalAccess
			&& !ProfileHasBoundaryEntryCapableContent(Entry.ChildRegionSettings.RegionProfile))
		{
			Result.AddError(BuildContentSetValidationMessage(
				this,
				FString::Printf(TEXT("%s cannot contribute host vertical access because the child profile has no boundary-reachable Entry-capable content."), *EntryPrefix),
				{
					FString::Printf(TEXT("EntryId: %s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
					FString::Printf(TEXT("ChildProfile: %s"), *Entry.ChildRegionSettings.RegionProfile->GetName()),
					FString::Printf(TEXT("Child ContentSet: %s"), Entry.ChildRegionSettings.RegionProfile->ContentSet != nullptr ? *Entry.ChildRegionSettings.RegionProfile->ContentSet->GetName() : TEXT("<none>")),
					TEXT("Problem: child_contributes_host_vertical_access requires host-facing endpoint content that can reach a boundary/perimeter placement zone."),
					TEXT("Fix: Add an Entry-capable child module entry that can occupy the boundary/perimeter band, or disable child_contributes_host_vertical_access for this entry.")
				}));
		}

		if (Entry.ChildRegionSettings.bContributesHostVerticalAccess
			&& Entry.ChildRegionSettings.RegionProfile->LevelCount > 1
			&& ProfileHasBoundaryEntryCapableContent(Entry.ChildRegionSettings.RegionProfile)
			&& !ProfileHasBoundaryEntryCapableContentOnDistinctLevels(Entry.ChildRegionSettings.RegionProfile))
		{
			Result.AddError(BuildContentSetValidationMessage(
				this,
				FString::Printf(TEXT("%s cannot contribute host vertical access because the child profile does not expose boundary-reachable Entry-capable content on at least two distinct levels."), *EntryPrefix),
				{
					FString::Printf(TEXT("EntryId: %s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
					FString::Printf(TEXT("ChildProfile: %s"), *Entry.ChildRegionSettings.RegionProfile->GetName()),
					FString::Printf(TEXT("Child LevelCount: %d"), Entry.ChildRegionSettings.RegionProfile->LevelCount),
					TEXT("Problem: child_contributes_host_vertical_access requires lower and upper host-facing endpoint candidates on distinct levels."),
					TEXT("Fix: Author boundary-reachable Entry-capable child content that can appear on at least two distinct levels, or disable child_contributes_host_vertical_access for this entry.")
				}));
		}

		if (Entry.ChildRegionSettings.bContributesHostVerticalAccess
			&& Entry.ChildRegionSettings.RegionProfile->LevelCount > 1
			&& ProfileHasBoundaryEntryCapableContentOnDistinctLevels(Entry.ChildRegionSettings.RegionProfile)
			&& !ProfileCanCarryVerticalAccessAcrossBoundaryEntryLevels(Entry.ChildRegionSettings.RegionProfile))
		{
			Result.AddError(BuildContentSetValidationMessage(
				this,
				FString::Printf(TEXT("%s cannot contribute host vertical access because the child profile has no VerticalAccess-capable coverage across the span between its possible lower and upper boundary-entry levels."), *EntryPrefix),
				{
					FString::Printf(TEXT("EntryId: %s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
					FString::Printf(TEXT("ChildProfile: %s"), *Entry.ChildRegionSettings.RegionProfile->GetName()),
					FString::Printf(TEXT("Child LevelCount: %d"), Entry.ChildRegionSettings.RegionProfile->LevelCount),
					TEXT("Problem: child_contributes_host_vertical_access requires VerticalAccess-capable child content that can cover every level between one possible lower and upper host-facing boundary-entry level pair."),
					TEXT("Fix: Author VerticalAccess-capable child content whose covered levels span between the possible lower and upper boundary-entry levels, or disable child_contributes_host_vertical_access for this entry.")
				}));
		}

		if (Entry.ChildRegionSettings.bContributesHostVerticalAccess
			&& Entry.ChildRegionSettings.RegionProfile->LevelCount > 1
			&& ProfileCanCarryVerticalAccessAcrossBoundaryEntryLevels(Entry.ChildRegionSettings.RegionProfile)
			&& !ProfileHasGenerallyConnectableBoundaryEntryLevels(Entry.ChildRegionSettings.RegionProfile))
		{
			Result.AddError(BuildContentSetValidationMessage(
				this,
				FString::Printf(TEXT("%s cannot contribute host vertical access because the child profile exposes no shared traversal channel across any possible lower and upper boundary-entry level pair."), *EntryPrefix),
				{
					FString::Printf(TEXT("EntryId: %s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
					FString::Printf(TEXT("ChildProfile: %s"), *Entry.ChildRegionSettings.RegionProfile->GetName()),
					FString::Printf(TEXT("Child LevelCount: %d"), Entry.ChildRegionSettings.RegionProfile->LevelCount),
					TEXT("Problem: child_contributes_host_vertical_access requires at least one lower/upper boundary-entry level pair that also shares a traversal channel, or later negotiation can never prove a generally connectable host anchor pair."),
					TEXT("Fix: Author boundary-reachable Entry-capable child content on distinct levels that shares at least one traversal channel, or disable child_contributes_host_vertical_access for this entry.")
				}));
		}

		if (Entry.ChildRegionSettings.bContributesHostVerticalAccess
			&& Entry.ChildRegionSettings.RegionProfile->LevelCount > 1
			&& ProfileHasGenerallyConnectableBoundaryEntryLevels(Entry.ChildRegionSettings.RegionProfile)
			&& ProfileCanCarryVerticalAccessAcrossBoundaryEntryLevels(Entry.ChildRegionSettings.RegionProfile)
			&& !ProfileHasGenerallyConnectableBoundaryEntryLevelPairWithVerticalCoverage(Entry.ChildRegionSettings.RegionProfile))
		{
			Result.AddError(BuildContentSetValidationMessage(
				this,
				FString::Printf(TEXT("%s cannot contribute host vertical access because no possible lower and upper boundary-entry level pair has both shared traversal channels and full VerticalAccess coverage across the same span."), *EntryPrefix),
				{
					FString::Printf(TEXT("EntryId: %s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
					FString::Printf(TEXT("ChildProfile: %s"), *Entry.ChildRegionSettings.RegionProfile->GetName()),
					FString::Printf(TEXT("Child LevelCount: %d"), Entry.ChildRegionSettings.RegionProfile->LevelCount),
					TEXT("Problem: child_contributes_host_vertical_access requires at least one lower/upper boundary-entry level pair that both shares a traversal channel and can be carried by VerticalAccess-capable child content across that same level span."),
					TEXT("Fix: Author boundary-reachable Entry-capable child content and VerticalAccess-capable child content so the same lower/upper level pair shares traversal and full span coverage, or disable child_contributes_host_vertical_access for this entry.")
				}));
		}
	}

	return Result;
}

#if WITH_EDITOR
void ULayoutRegionContentSetAsset::ValidateLayoutRegionContentSetInEditor() const
{
	PorismLayoutEditorMessageLog::ReportValidationResult(
		this,
		ValidateContentSet(),
		TEXT("Layout region content set validation passed."));
}

EDataValidationResult ULayoutRegionContentSetAsset::IsDataValid(FDataValidationContext& Context) const
{
	const FLayoutValidationResult ValidationResult = ValidateContentSet();
	AppendContentSetValidationResult(Context, ValidationResult);
	return ValidationResult.IsValid() ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
