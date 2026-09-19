// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutProfileSnapshotBuilder.h"

#include "Layout/Assets/LayoutProfileAsset.h"

namespace LayoutProfileSnapshotBuilder
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

	void PopulateProfileSnapshotBase(
		FLayoutProfileSolveSnapshot& Snapshot,
		const ULayoutProfileAsset& Profile,
		const TArray<FLayoutSparsePlacementRuleSolveSnapshot>& SparsePlacementRuleSnapshots)
	{
		Snapshot.SnapshotId = Profile.GetFName();
		Snapshot.SourceProfile = &Profile;
		Snapshot.SourceProfilePath = FSoftObjectPath(&Profile);
		Snapshot.DebugName = Profile.GetFName();
		Snapshot.MinimumFootprintInCells = Profile.MinimumFootprintInCells;
		Snapshot.MaximumFootprintInCells = Profile.MaximumFootprintInCells;
		Snapshot.LevelCount = Profile.LevelCount;
		Snapshot.EntryCountMode = Profile.EntryCountMode;
		Snapshot.EntryCount = Profile.EntryCount;
		Snapshot.MinEntryCount = Profile.MinEntryCount;
		Snapshot.MaxEntryCount = Profile.MaxEntryCount;
		Snapshot.VerticalAccessCountMode = Profile.VerticalAccessCountMode;
		Snapshot.VerticalAccessCount = Profile.VerticalAccessCount;
		Snapshot.MinVerticalAccessCount = Profile.MinVerticalAccessCount;
		Snapshot.MaxVerticalAccessCount = Profile.MaxVerticalAccessCount;
		Snapshot.bRestrictVerticalAccessModulesToVerticalAccessCells = Profile.bRestrictVerticalAccessModulesToVerticalAccessCells;
		Snapshot.ClosureRequirements = Profile.ClosureRequirements;
		Snapshot.ZoneFeatureRequirements = Profile.ZoneFeatureRequirements;
		Snapshot.LevelFillRules = Profile.LevelFillRules;
		Snapshot.ReservedOpenSpaceRules = Profile.ReservedOpenSpaceRules;
		Snapshot.SparsePlacementRules = SparsePlacementRuleSnapshots;
		Snapshot.bRequireAllTraversalChannelsReachable = Profile.bRequireAllTraversalChannelsReachable;
		Snapshot.bSupportsSteppedTerrainSolve = Profile.bSupportsSteppedTerrainSolve;
		Snapshot.bEnableTerrainSeams = Profile.bEnableTerrainSeams;
		Snapshot.bUndergroundPlacement = Profile.bUndergroundPlacement;
		Snapshot.Validation = Profile.ValidateProfile();
		for (const FLayoutSparsePlacementRuleSolveSnapshot& SparseRuleSnapshot : Snapshot.SparsePlacementRules)
		{
			Snapshot.Validation.Messages.Append(SparseRuleSnapshot.Validation.Messages);
			Snapshot.ProofRecords.Append(SparseRuleSnapshot.ProofRecords);
			Snapshot.ValidationAssertions.Append(SparseRuleSnapshot.ValidationAssertions);
		}
	}

	FLayoutProfileSolveSnapshot BuildProfileSnapshotFromProfile(
		const int32 SnapshotSchemaVersion,
		const ULayoutProfileAsset* Profile,
		FBuildSparsePlacementRuleSnapshotFn BuildSparsePlacementRuleSnapshot)
	{
		FLayoutProfileSolveSnapshot Snapshot;
		Snapshot.SnapshotSchemaVersion = SnapshotSchemaVersion;
		if (Profile == nullptr)
		{
			Snapshot.Validation.AddError(TEXT("Layout profile snapshot requires a profile."));
			Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
				TEXT("ProfileSnapshot.SourcePresent"),
				ELayoutValidationAssertionKind::SnapshotSourcePresent,
				false,
				{},
				TEXT("Layout profile snapshot requires a profile.")));
			AppendFailedAssertionsToValidation(Snapshot.ValidationAssertions, Snapshot.Validation);
			return Snapshot;
		}

		TArray<FLayoutSparsePlacementRuleSolveSnapshot> SparsePlacementRuleSnapshots;
		SparsePlacementRuleSnapshots.Reserve(Profile->SparsePlacementRules.Num());
		for (const FInstancedStruct& SparseRule : Profile->SparsePlacementRules)
		{
			SparsePlacementRuleSnapshots.Add(BuildSparsePlacementRuleSnapshot(SparseRule));
		}

		PopulateProfileSnapshotBase(
			Snapshot,
			*Profile,
			SparsePlacementRuleSnapshots);
		Snapshot.ProofRecords.Add(MakeSnapshotProofRecord(
			TEXT("ProfileSnapshot.Copy"),
			ELayoutProofKind::SnapshotCopy,
			Snapshot.SnapshotId,
			{Profile->GetFName()},
			TEXT("Copied profile solve contract into immutable solver snapshot.")));
		for (const FLayoutClosureRequirement& ClosureRequirement : Snapshot.ClosureRequirements)
		{
			Snapshot.ProofRecords.Add(MakeSnapshotProofRecord(
				FLayoutId(*FString::Printf(TEXT("ProfileSnapshot.Closure.%s"), *ClosureRequirement.ClosureId.ToString())),
				ELayoutProofKind::DerivedContract,
				ClosureRequirement.ClosureId,
				{Snapshot.SnapshotId},
				TEXT("Copied explicit boundary-closure requirement into the immutable profile snapshot.")));
		}
		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ProfileSnapshot.SourcePresent"),
			ELayoutValidationAssertionKind::SnapshotSourcePresent,
			true,
			{Profile->GetFName()}));
		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ProfileSnapshot.AssetValidationPassed"),
			ELayoutValidationAssertionKind::AssetValidationPassed,
			Snapshot.Validation.IsValid(),
			{Profile->GetFName()},
			Snapshot.Validation.IsValid() ? FString() : FString::Printf(TEXT("Profile '%s' failed asset validation before snapshot solve setup."), *Profile->GetName())));
		AppendFailedAssertionsToValidation(Snapshot.ValidationAssertions, Snapshot.Validation);
		return Snapshot;
	}
}
