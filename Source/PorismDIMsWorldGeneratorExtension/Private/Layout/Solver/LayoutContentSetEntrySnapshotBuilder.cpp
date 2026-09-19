// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutContentSetEntrySnapshotBuilder.h"

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutChildRequestTemplateSnapshotBuilder.h"
#include "Layout/Solver/LayoutContentEntrySnapshotBuilder.h"

namespace LayoutContentSetEntrySnapshotBuilder
{
	namespace
	{
		FLayoutProofRecord MakeSnapshotProofRecord(
			const FLayoutId ProofId,
			const ELayoutProofKind ProofKind,
			const FLayoutId TargetId,
			const TArray<FLayoutId>& SourceIds,
			const FString& ProofSummary)
		{
			FLayoutProofRecord Proof;
			Proof.ProofId = ProofId;
			Proof.ProofKind = ProofKind;
			Proof.TargetId = TargetId;
			Proof.SourceIds = SourceIds;
			Proof.ProofSummary = ProofSummary;
			return Proof;
		}
	}

	void AppendContentSetEntrySnapshot(
		FLayoutRegionContentSetSolveSnapshot& Snapshot,
		const FLayoutRegionContentEntry& Entry,
		int32& InOutModuleEntryCount,
		FBuildChildRequestTemplateFn BuildChildRequestTemplate)
	{
		FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot = Snapshot.Entries.AddDefaulted_GetRef();
		LayoutContentEntrySnapshotBuilder::PopulateContentEntrySnapshotBase(
			EntrySnapshot,
			Entry,
			InOutModuleEntryCount);

		if (Entry.ContentKind != ELayoutRegionContentKind::Module
			&& Entry.ChildRegionSettings.RegionProfile != nullptr)
		{
			const TSharedPtr<FLayoutChildRequestTemplateSnapshot> CompiledChildRequestTemplate =
				BuildChildRequestTemplate(Entry.ChildRegionSettings.RegionProfile);
			LayoutChildRequestTemplateSnapshotBuilder::FinalizeChildRequestTemplateForContentEntry(
				Snapshot,
				EntrySnapshot,
				CompiledChildRequestTemplate);
		}

		Snapshot.ProofRecords.Add(MakeSnapshotProofRecord(
			FLayoutId(*FString::Printf(TEXT("ContentSetSnapshot.Entry.%s"), *EntrySnapshot.EntryId.ToString())),
			ELayoutProofKind::SnapshotCopy,
			EntrySnapshot.EntryId,
			{Snapshot.SnapshotId},
			FString::Printf(
				TEXT("Copied content entry '%s' into immutable content-set snapshot '%s'."),
				*EntrySnapshot.EntryId.ToString(),
				*Snapshot.SnapshotId.ToString())));
	}
}
