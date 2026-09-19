// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutChildRequestTemplateSnapshotBuilder.h"

namespace LayoutChildRequestTemplateSnapshotBuilder
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
	}

	void PopulateChildRequestTemplateSnapshotBase(
		FLayoutChildRequestTemplateSnapshot& Template,
		const FLayoutProfileSolveSnapshot& ProfileSnapshot,
		const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot,
		const FLayoutModuleCatalog& ModuleCatalog)
	{
		Template.ProfileSnapshot = ProfileSnapshot;
		Template.ContentSetSnapshot = ContentSetSnapshot;
		Template.ModuleCatalog = ModuleCatalog;
		Template.ChildProfilePath = Template.ProfileSnapshot.SourceProfilePath;
		Template.EffectiveSnapshotId = FLayoutId(*FString::Printf(
			TEXT("ChildTemplate.%s.%s"),
			*Template.ProfileSnapshot.SnapshotId.ToString(),
			*Template.ContentSetSnapshot.SnapshotId.ToString()));
		Template.ProofRecords.Append(Template.ContentSetSnapshot.ProofRecords);
		Template.ProofRecords.Append(Template.ModuleCatalog.ProofRecords);
		Template.ProofRecords.Append(Template.ProfileSnapshot.ProofRecords);
		Template.ProofRecords.Add(MakeSnapshotProofRecord(
			FLayoutId(*FString::Printf(TEXT("%s.Copy"), *Template.EffectiveSnapshotId.ToString())),
			ELayoutProofKind::SnapshotCopy,
			Template.EffectiveSnapshotId,
			{Template.ProfileSnapshot.SnapshotId, Template.ContentSetSnapshot.SnapshotId, Template.ModuleCatalog.SnapshotId},
			TEXT("Compiled immutable child request template from the child profile and its content graph.")));
		Template.ValidationAssertions.Append(Template.ContentSetSnapshot.ValidationAssertions);
		Template.ValidationAssertions.Append(Template.ModuleCatalog.ValidationAssertions);
		Template.ValidationAssertions.Append(Template.ProfileSnapshot.ValidationAssertions);
		Template.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ChildRequestTemplate.SnapshotContractInitialized"),
			ELayoutValidationAssertionKind::SnapshotContractInitialized,
			Template.EffectiveSnapshotId != NAME_None,
			{Template.ProfileSnapshot.SnapshotId, Template.ContentSetSnapshot.SnapshotId, Template.ModuleCatalog.SnapshotId},
			Template.EffectiveSnapshotId != NAME_None
				? FString()
				: FString::Printf(
					TEXT("Child request template for profile snapshot '%s' failed to initialize an effective snapshot id."),
					*Template.ProfileSnapshot.DebugName.ToString())));
	}

	void FinalizeChildRequestTemplateForContentEntry(
		FLayoutRegionContentSetSolveSnapshot& Snapshot,
		FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		const TSharedPtr<FLayoutChildRequestTemplateSnapshot>& CompiledChildRequestTemplate)
	{
		EntrySnapshot.CompiledChildRequestTemplate = CompiledChildRequestTemplate;
		if (EntrySnapshot.CompiledChildRequestTemplate.IsValid())
		{
			EntrySnapshot.ChildContentSetSnapshotId =
				EntrySnapshot.CompiledChildRequestTemplate->ContentSetSnapshot.SnapshotId;
			Snapshot.ProofRecords.Append(EntrySnapshot.CompiledChildRequestTemplate->ProofRecords);
			Snapshot.ValidationAssertions.Append(EntrySnapshot.CompiledChildRequestTemplate->ValidationAssertions);
			Snapshot.ProofRecords.Add(MakeSnapshotProofRecord(
				FLayoutId(*FString::Printf(TEXT("ContentSetSnapshot.Entry.%s.ChildTemplate"), *EntrySnapshot.EntryId.ToString())),
				ELayoutProofKind::SnapshotCopy,
				EntrySnapshot.EntryId,
				{EntrySnapshot.ChildProfileSnapshotId, EntrySnapshot.ChildContentSetSnapshotId},
				FString::Printf(
					TEXT("Compiled snapshot-only child request template for content entry '%s'."),
					*EntrySnapshot.EntryId.ToString())));
			return;
		}

		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ContentSetSnapshot.ChildRequestTemplatePresent"),
			ELayoutValidationAssertionKind::SnapshotContractInitialized,
			false,
			{EntrySnapshot.EntryId, EntrySnapshot.ChildProfileSnapshotId},
			FString::Printf(
				TEXT("Content entry '%s' could not compile a snapshot-only child request template. Cyclic or unresolved child profile expansion is not supported."),
				*EntrySnapshot.EntryId.ToString())));
	}
}
