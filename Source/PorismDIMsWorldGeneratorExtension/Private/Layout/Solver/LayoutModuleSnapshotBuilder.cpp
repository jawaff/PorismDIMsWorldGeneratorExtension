// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutModuleSnapshotBuilder.h"

#include "Layout/Assets/LayoutModuleAsset.h"

namespace LayoutModuleSnapshotBuilder
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

	void PopulateLeafModuleSnapshotBase(
		FLayoutModuleSolveSnapshot& Snapshot,
		const ULayoutModuleAsset* Module,
		const int32 Weight,
		const ELayoutPlacementZone PlacementZone,
		const ELayoutLevelPlacementPolicy LevelPlacementPolicy,
		const int32 SpecificLevel,
		const bool bOptional,
		const FIntVector& EstablishedSharedCellSizeInBlocks)
	{
		Snapshot.PlacementZone = PlacementZone;
		Snapshot.LevelPlacementPolicy = LevelPlacementPolicy;
		Snapshot.SpecificLevel = FMath::Max(0, SpecificLevel);
		Snapshot.bOptional = bOptional;
		if (Module == nullptr)
		{
			return;
		}

		Snapshot.SnapshotId = Module->GetFName();
		Snapshot.SourceModule = const_cast<ULayoutModuleAsset*>(Module);
		Snapshot.DebugName = Module->GetFName();
		Snapshot.Template = Module->Template;
		const FIntVector EffectiveCellSizeInBlocks = Module->GetEffectiveCellSizeInBlocks();
		const bool bEstablishedSharedCellSizeProvided = EstablishedSharedCellSizeInBlocks != FIntVector::ZeroValue;
		Snapshot.CellSizeInBlocks = bEstablishedSharedCellSizeProvided
			? EstablishedSharedCellSizeInBlocks
			: EffectiveCellSizeInBlocks;
		Snapshot.BoundsCells = Module->GetOccupiedBoundsCells();
		Snapshot.TemplateDimensionsBlocks = Module->GetEffectiveTemplateDimensionsBlocks();
		Snapshot.OccupiedLocalCells = Module->GetOccupiedLocalCells();
		Snapshot.Roles = Module->GetEffectiveRoles();
		Snapshot.SupportedCellIntents = Module->GetEffectiveSupportedCellIntents();
		Snapshot.RootSupportedCellIntents = Snapshot.SupportedCellIntents;
		Snapshot.AllowedYawRotationSteps = Module->GetEffectiveYawRotationSteps();
		Snapshot.EffectiveFaceRules = Module->GetEffectiveFaceRules();
		Snapshot.TraversalChannels = Module->GetEffectiveTraversalChannels();
		Snapshot.InternalAccessLinks = Module->GetEffectiveInternalAccessLinks();
		Snapshot.MinTraversableNeighborFaces = Module->MinWalkableFaces;
		Snapshot.Weight = FMath::Max(1, Weight);
		Snapshot.Validation = Module->ValidateModule();
		if (bEstablishedSharedCellSizeProvided)
		{
			const bool bSharedCellSizeMatches = EffectiveCellSizeInBlocks == EstablishedSharedCellSizeInBlocks;
			Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
				TEXT("ModuleSnapshot.SharedCellSizeContractValid"),
				ELayoutValidationAssertionKind::SnapshotSharedCellSizeContractValid,
				bSharedCellSizeMatches,
				{Module->GetFName()},
				bSharedCellSizeMatches
					? FString()
					: FString::Printf(
						TEXT("Module '%s' violates the established shared cell size contract.\nEstablishedSharedCellSizeInBlocks: %s\nEffectiveLeafCellSizeInBlocks: %s"),
						*Module->GetName(),
						*EstablishedSharedCellSizeInBlocks.ToString(),
						*EffectiveCellSizeInBlocks.ToString())));
		}
	}

	FLayoutModuleSolveSnapshot BuildLeafModuleSnapshotFromAsset(
		const int32 SnapshotSchemaVersion,
		const ULayoutModuleAsset* Module,
		const int32 Weight,
		const ELayoutPlacementZone PlacementZone,
		const ELayoutLevelPlacementPolicy LevelPlacementPolicy,
		const int32 SpecificLevel,
		const bool bOptional,
		const FIntVector& EstablishedSharedCellSizeInBlocks)
	{
		FLayoutModuleSolveSnapshot Snapshot;
		Snapshot.SnapshotSchemaVersion = SnapshotSchemaVersion;
		PopulateLeafModuleSnapshotBase(
			Snapshot,
			Module,
			Weight,
			PlacementZone,
			LevelPlacementPolicy,
			SpecificLevel,
			bOptional,
			EstablishedSharedCellSizeInBlocks);
		if (Module == nullptr)
		{
			Snapshot.Validation.AddError(TEXT("Layout module snapshot requires a module asset."));
			Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
				TEXT("ModuleSnapshot.SourcePresent"),
				ELayoutValidationAssertionKind::SnapshotSourcePresent,
				false,
				{},
				TEXT("Layout module snapshot requires a module asset.")));
			AppendFailedAssertionsToValidation(Snapshot.ValidationAssertions, Snapshot.Validation);
			return Snapshot;
		}

		Snapshot.ProofRecords.Add(MakeSnapshotProofRecord(
			TEXT("ModuleSnapshot.Copy"),
			ELayoutProofKind::SnapshotCopy,
			Snapshot.SnapshotId,
			{Module->GetFName()},
			TEXT("Copied effective module contract into immutable solver snapshot.")));
		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ModuleSnapshot.SourcePresent"),
			ELayoutValidationAssertionKind::SnapshotSourcePresent,
			true,
			{Module->GetFName()}));
		const bool bSnapshotValidationPassed = Snapshot.Validation.IsValid();
		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ModuleSnapshot.AssetValidationPassed"),
			ELayoutValidationAssertionKind::AssetValidationPassed,
			bSnapshotValidationPassed,
			{Module->GetFName()},
			bSnapshotValidationPassed ? FString() : FString::Printf(TEXT("Module '%s' failed asset validation before snapshot solve setup."), *Module->GetName())));
		return Snapshot;
	}
}
