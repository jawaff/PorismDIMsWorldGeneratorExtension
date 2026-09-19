// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutModuleCatalogBuilder.h"

#include "Layout/Assets/LayoutRegionContentSetAsset.h"

namespace LayoutModuleCatalogBuilder
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

	void PopulateModuleCatalogFromContentSet(
		FLayoutModuleCatalog& Snapshot,
		const ULayoutRegionContentSetAsset* ContentSet)
	{
		if (ContentSet == nullptr)
		{
			return;
		}

		for (const FLayoutRegionContentEntry& Entry : ContentSet->Entries)
		{
			if (Entry.ContentKind != ELayoutRegionContentKind::Module)
			{
				continue;
			}

			FLayoutModuleSolveSnapshot EntrySnapshot;
			if (Entry.ModuleSettings.Module != nullptr && Entry.ModuleSettings.CompositeModule == nullptr)
			{
				EntrySnapshot = FLayoutProfileSolver::BuildModuleSnapshot(
					Entry.ModuleSettings.Module,
					Entry.Weight,
					Entry.ModuleSettings.PlacementZone,
					Entry.ModuleSettings.LevelPlacementPolicy,
					Entry.ModuleSettings.SpecificLevel,
					Entry.ModuleSettings.bOptional,
					Snapshot.SharedCellSizeInBlocks);
			}
			else if (Entry.ModuleSettings.Module == nullptr && Entry.ModuleSettings.CompositeModule != nullptr)
			{
				EntrySnapshot = FLayoutProfileSolver::BuildCompositeModuleSnapshot(
					Entry.ModuleSettings.CompositeModule,
					Entry.Weight,
					Entry.ModuleSettings.PlacementZone,
					Entry.ModuleSettings.LevelPlacementPolicy,
					Entry.ModuleSettings.SpecificLevel,
					Entry.ModuleSettings.bOptional,
					Snapshot.SharedCellSizeInBlocks);
			}
			else
			{
				continue;
			}

			EntrySnapshot.SourceContentEntryId = Entry.EntryId;
			EntrySnapshot.ProvidedZoneFeatures = Entry.ProvidedZoneFeatures;
			EntrySnapshot.ClosureProviderIntents = Entry.ClosureProviderIntents;
			EntrySnapshot.SeamProviderIntents = Entry.SeamProviderIntents;
			Snapshot.Modules.Add(MoveTemp(EntrySnapshot));
		}

		// Exact witnesses must distinguish entries that reuse an asset with different
		// placement contracts. Keep source/debug asset identity unchanged for realization.
		TMap<FLayoutId, int32> UsesByAsset;
		for (const FLayoutModuleSolveSnapshot& Module : Snapshot.Modules)
		{
			++UsesByAsset.FindOrAdd(Module.SnapshotId);
		}
		for (FLayoutModuleSolveSnapshot& Module : Snapshot.Modules)
		{
			if (UsesByAsset.FindRef(Module.SnapshotId) < 2 || Module.SourceContentEntryId.IsNone()) continue;
			const FLayoutId AssetId = Module.SnapshotId;
			Module.SnapshotId = FLayoutId(*FString::Printf(TEXT("%s.Entry.%s"),
				*AssetId.ToString(), *Module.SourceContentEntryId.ToString()));
			for (FLayoutProofRecord& Proof : Module.ProofRecords)
			{
				if (Proof.TargetId == AssetId) Proof.TargetId = Module.SnapshotId;
				if (Proof.ProofKind == ELayoutProofKind::DerivedContract)
				{
					for (FLayoutId& SourceId : Proof.SourceIds)
					{
						if (SourceId == AssetId) SourceId = Module.SnapshotId;
					}
				}
			}
		}
	}

}
