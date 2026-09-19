// Copyright 2026 Spotted Loaf Studio

#include "Layout/Solver/LayoutContentEntrySnapshotBuilder.h"

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"

namespace LayoutContentEntrySnapshotBuilder
{
	void PopulateContentEntrySnapshotBase(
		FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		const FLayoutRegionContentEntry& Entry,
		int32& InOutModuleEntryCount)
	{
		EntrySnapshot.EntryId = Entry.EntryId;
		EntrySnapshot.ContentKind = Entry.ContentKind;
		EntrySnapshot.Weight = FMath::Max(1, Entry.Weight);
		EntrySnapshot.ProvidedZoneFeatures = Entry.ProvidedZoneFeatures;
		EntrySnapshot.ClosureProviderIntents = Entry.ClosureProviderIntents;
		EntrySnapshot.SeamProviderIntents = Entry.SeamProviderIntents;
		if (Entry.ContentKind == ELayoutRegionContentKind::Module)
		{
			EntrySnapshot.ModuleSnapshotIndex = InOutModuleEntryCount++;
			EntrySnapshot.ModulePlacementZone = Entry.ModuleSettings.PlacementZone;
			EntrySnapshot.ModuleLevelPlacementPolicy = Entry.ModuleSettings.LevelPlacementPolicy;
			EntrySnapshot.ModuleSpecificLevel = FMath::Max(0, Entry.ModuleSettings.SpecificLevel);
			EntrySnapshot.bModuleOptional = Entry.ModuleSettings.bOptional;
			return;
		}

		EntrySnapshot.ChildProfileSnapshotId = Entry.ChildRegionSettings.RegionProfile != nullptr
			? Entry.ChildRegionSettings.RegionProfile->GetFName()
			: NAME_None;
		EntrySnapshot.ChildProfilePath = Entry.ChildRegionSettings.RegionProfile != nullptr
			? FSoftObjectPath(Entry.ChildRegionSettings.RegionProfile)
			: FSoftObjectPath();
		EntrySnapshot.ChildPlacementZone = Entry.ChildRegionSettings.PlacementZone;
		EntrySnapshot.ChildLevelPlacementPolicy = Entry.ChildRegionSettings.LevelPlacementPolicy;
		EntrySnapshot.ChildSpecificLevel = FMath::Max(0, Entry.ChildRegionSettings.SpecificLevel);
		EntrySnapshot.bChildOptional = Entry.ChildRegionSettings.bOptional;
		EntrySnapshot.bChildContributesHostVerticalAccess = Entry.ChildRegionSettings.bContributesHostVerticalAccess;
	}
}
