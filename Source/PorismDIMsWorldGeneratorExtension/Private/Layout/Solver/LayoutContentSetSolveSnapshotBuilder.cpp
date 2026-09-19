// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutContentSetSolveSnapshotBuilder.h"

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutContentSetEntrySnapshotBuilder.h"
#include "Layout/Solver/LayoutContentSetSnapshotBuilder.h"
#include "Layout/Solver/LayoutModuleCatalogBuilder.h"

namespace LayoutContentSetSolveSnapshotBuilder
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

		FLayoutValidationAssertionRecord MakeSnapshotAssertionRecord(
			const FLayoutId AssertionId,
			const ELayoutValidationAssertionKind AssertionKind,
			const bool bPassed,
			const TArray<FLayoutId>& RelatedIds,
			const FString& FailureReason = FString())
		{
			FLayoutValidationAssertionRecord Assertion;
			Assertion.AssertionId = AssertionId;
			Assertion.AssertionKind = AssertionKind;
			Assertion.bPassed = bPassed;
			Assertion.RelatedIds = RelatedIds;
			Assertion.FailureReason = FailureReason;
			return Assertion;
		}

		void AppendFailedAssertionsToValidation(
			const TArray<FLayoutValidationAssertionRecord>& Assertions,
			FLayoutValidationResult& Validation)
		{
			for (const FLayoutValidationAssertionRecord& Assertion : Assertions)
			{
				if (!Assertion.bPassed)
				{
					Validation.AddError(
						Assertion.FailureReason.IsEmpty()
							? FString::Printf(TEXT("Snapshot assertion '%s' failed."), *Assertion.AssertionId.ToString())
							: Assertion.FailureReason);
				}
			}
		}
	}

	FLayoutRegionContentSetSolveSnapshot BuildContentSetSnapshotFromContentSet(
		const int32 SnapshotSchemaVersion,
		const ULayoutRegionContentSetAsset* ContentSet,
		FBuildChildRequestTemplateFn BuildChildRequestTemplate,
		const FIntVector* SharedCellSizeOverride)
	{
		FLayoutRegionContentSetSolveSnapshot Snapshot;
		Snapshot.SnapshotSchemaVersion = SnapshotSchemaVersion;
		if (ContentSet == nullptr)
		{
			Snapshot.Validation.AddError(TEXT("Layout region content-set snapshot requires a content set."));
			Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
				TEXT("ContentSetSnapshot.SourcePresent"),
				ELayoutValidationAssertionKind::SnapshotSourcePresent,
				false,
				{},
				TEXT("Layout region content-set snapshot requires a content set.")));
			AppendFailedAssertionsToValidation(Snapshot.ValidationAssertions, Snapshot.Validation);
			return Snapshot;
		}

		LayoutContentSetSnapshotBuilder::PopulateContentSetSnapshotBase(Snapshot, ContentSet);
		if (SharedCellSizeOverride != nullptr && *SharedCellSizeOverride != FIntVector::ZeroValue)
		{
			Snapshot.SharedCellSizeInBlocks = *SharedCellSizeOverride;
		}
		int32 ModuleEntryCount = 0;
		for (int32 EntryIndex = 0; EntryIndex < ContentSet->Entries.Num(); ++EntryIndex)
		{
			const FLayoutRegionContentEntry& Entry = ContentSet->Entries[EntryIndex];
			LayoutContentSetEntrySnapshotBuilder::AppendContentSetEntrySnapshot(
				Snapshot,
				Entry,
				ModuleEntryCount,
				BuildChildRequestTemplate);
		}

		Snapshot.ProofRecords.Add(MakeSnapshotProofRecord(
			TEXT("ContentSetSnapshot.Copy"),
			ELayoutProofKind::SnapshotCopy,
			Snapshot.SnapshotId,
			{ContentSet->GetFName()},
			TEXT("Copied region content-set contract into immutable solver snapshot.")));
		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ContentSetSnapshot.SourcePresent"),
			ELayoutValidationAssertionKind::SnapshotSourcePresent,
			true,
			{ContentSet->GetFName()}));
		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ContentSetSnapshot.AssetValidationPassed"),
			ELayoutValidationAssertionKind::AssetValidationPassed,
			Snapshot.Validation.IsValid(),
			{ContentSet->GetFName()},
			Snapshot.Validation.IsValid() ? FString() : FString::Printf(TEXT("Content set '%s' failed asset validation before snapshot solve setup."), *ContentSet->GetName())));
		AppendFailedAssertionsToValidation(Snapshot.ValidationAssertions, Snapshot.Validation);
		return Snapshot;
	}

	FLayoutModuleCatalog BuildModuleCatalogFromContentSet(
		const int32 SnapshotSchemaVersion,
		const ULayoutRegionContentSetAsset* ContentSet,
		const FIntVector* SharedCellSizeOverride)
	{
		FLayoutModuleCatalog Snapshot;
		Snapshot.SnapshotSchemaVersion = SnapshotSchemaVersion;
		if (ContentSet == nullptr)
		{
			Snapshot.Validation.AddError(TEXT("Layout module-solve snapshot requires a content set."));
			Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
				TEXT("ModuleCatalog.ContentSetSourcePresent"),
				ELayoutValidationAssertionKind::SnapshotSourcePresent,
				false,
				{},
				TEXT("Layout module-solve snapshot requires a content set.")));
			AppendFailedAssertionsToValidation(Snapshot.ValidationAssertions, Snapshot.Validation);
			return Snapshot;
		}

		Snapshot.SnapshotId = ContentSet->GetFName();
		Snapshot.DebugName = ContentSet->GetFName();
		Snapshot.SharedCellSizeInBlocks =
			SharedCellSizeOverride != nullptr && *SharedCellSizeOverride != FIntVector::ZeroValue
				? *SharedCellSizeOverride
				: ContentSet->GetDerivedSharedCellSizeInBlocks();
		Snapshot.Validation = ContentSet->ValidateContentSet();
		LayoutModuleCatalogBuilder::PopulateModuleCatalogFromContentSet(Snapshot, ContentSet);
		if (Snapshot.Modules.IsEmpty())
		{
			Snapshot.Validation.AddError(TEXT("Content set does not contain any module entries that the current module-solve path can place."));
		}

		Snapshot.ProofRecords.Add(MakeSnapshotProofRecord(
			TEXT("ModuleCatalog.FromContentSet"),
			ELayoutProofKind::SnapshotCopy,
			Snapshot.SnapshotId,
			{ContentSet->GetFName()},
			TEXT("Compiled module-backed content entries from the unified content set into the current module-solve snapshot path.")));
		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ModuleCatalog.ContentSetSourcePresent"),
			ELayoutValidationAssertionKind::SnapshotSourcePresent,
			true,
			{ContentSet->GetFName()}));
		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ModuleCatalog.ContentSetValidationPassed"),
			ELayoutValidationAssertionKind::AssetValidationPassed,
			Snapshot.Validation.IsValid(),
			{ContentSet->GetFName()},
			Snapshot.Validation.IsValid() ? FString() : FString::Printf(TEXT("Content set '%s' failed validation before module-solve snapshot setup."), *ContentSet->GetName())));
		AppendFailedAssertionsToValidation(Snapshot.ValidationAssertions, Snapshot.Validation);
		return Snapshot;
	}
}
