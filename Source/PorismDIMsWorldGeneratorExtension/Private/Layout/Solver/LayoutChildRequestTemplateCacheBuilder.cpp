// Copyright 2026 Spotted Loaf Studio

#include "Layout/Solver/LayoutChildRequestTemplateCacheBuilder.h"

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Solver/LayoutChildRequestTemplateSnapshotBuilder.h"

namespace LayoutChildRequestTemplateCacheBuilder
{
	TSharedPtr<FLayoutChildRequestTemplateSnapshot> BuildChildRequestTemplateSnapshot(
		const ULayoutProfileAsset* Profile,
		FRecursiveChildTemplateBuildState& State,
		const int32 SnapshotSchemaVersion,
		FBuildChildProfileSnapshotFn BuildProfileSnapshot,
		FBuildChildContentSetSnapshotFn BuildContentSetSnapshot,
		FBuildChildModuleCatalogFn BuildModuleCatalog)
	{
		if (Profile == nullptr)
		{
			return nullptr;
		}

		if (const TSharedPtr<FLayoutChildRequestTemplateSnapshot>* ExistingTemplate =
			State.ChildRequestTemplatesByProfile.Find(Profile))
		{
			return *ExistingTemplate;
		}

		if (State.ActiveChildProfiles.Contains(Profile))
		{
			return nullptr;
		}

		State.ActiveChildProfiles.Add(Profile);

		TSharedPtr<FLayoutChildRequestTemplateSnapshot> Template = MakeShared<FLayoutChildRequestTemplateSnapshot>();
		Template->SnapshotSchemaVersion = SnapshotSchemaVersion;
		Template->Seed = 0;
		const FLayoutProfileSolveSnapshot ProfileSnapshot = BuildProfileSnapshot(Profile);
		const FLayoutRegionContentSetSolveSnapshot ContentSetSnapshot =
			BuildContentSetSnapshot(Profile->ContentSet);
		const FLayoutModuleCatalog ModuleCatalog =
			BuildModuleCatalog(Profile->ContentSet);
		LayoutChildRequestTemplateSnapshotBuilder::PopulateChildRequestTemplateSnapshotBase(
			*Template,
			ProfileSnapshot,
			ContentSetSnapshot,
			ModuleCatalog);

		State.ActiveChildProfiles.Remove(Profile);
		State.ChildRequestTemplatesByProfile.Add(Profile, Template);
		return Template;
	}
}
