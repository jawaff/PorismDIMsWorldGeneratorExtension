// Copyright 2026 Spotted Loaf Studio

#include "Layout/Solver/LayoutContentSetSnapshotBuilder.h"

#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"

namespace LayoutContentSetSnapshotBuilder
{
	void PopulateContentSetSnapshotBase(
		FLayoutRegionContentSetSolveSnapshot& Snapshot,
		const ULayoutRegionContentSetAsset* ContentSet)
	{
		if (ContentSet == nullptr)
		{
			return;
		}

		Snapshot.SnapshotId = ContentSet->GetFName();
		Snapshot.SourceContentSet = ContentSet;
		Snapshot.DebugName = ContentSet->GetFName();
		// Standalone request snapshots should only freeze shared metrics derived from real structural content.
		Snapshot.SharedCellSizeInBlocks = ContentSet->GetDerivedSharedCellSizeInBlocks();
		Snapshot.Validation = ContentSet->ValidateContentSet();
		Snapshot.Entries.Reserve(ContentSet->Entries.Num());
	}

}
