// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutProfileSolver.h"

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutContentSetSnapshotBuilder.h"
#include "Layout/Solver/LayoutContentSetSolveSnapshotBuilder.h"
#include "Layout/Solver/LayoutContentSetEntrySnapshotBuilder.h"
#include "Layout/Solver/LayoutContentEntrySnapshotBuilder.h"
#include "Layout/Solver/LayoutChildRequestTemplateCacheBuilder.h"
#include "Layout/Solver/LayoutChildRequestTemplateSnapshotBuilder.h"
#include "Layout/Solver/LayoutCompositeModuleSnapshotBuilder.h"
#include "Layout/Solver/LayoutModuleDerivedContractBuilder.h"
#include "Layout/Solver/LayoutModuleSnapshotBuilder.h"
#include "Layout/Solver/LayoutModuleCatalogBuilder.h"
#include "Layout/Solver/LayoutProfileSnapshotBuilder.h"
#include "Layout/Solver/LayoutRegionRequestSnapshotBuilder.h"
#include "Layout/Solver/LayoutStandaloneRegionRequestBuilder.h"
#include "Layout/Solver/LayoutSparsePlacementRuleSnapshotBuilder.h"

namespace LayoutSolveSnapshotBuilderPrivate
{
	constexpr int32 SnapshotSchemaVersion = 2;
	constexpr int32 SparsePlacementProfileSnapshotSchemaVersion = 4;

	struct FRecursiveSnapshotBuildContext
	{
		LayoutChildRequestTemplateCacheBuilder::FRecursiveChildTemplateBuildState ChildTemplateState;
	};

	FLayoutWorldBindingPlacementPolicy BuildProfileOnlyStandalonePlacementPolicy()
	{
		FLayoutWorldBindingPlacementPolicy Settings;
		// Profile-only standalone solves are not world-facing placements. Keep the
		// carrier explicitly neutral so later solve/runtime stages do not inherit
		// ordinary-root terrain defaults from the world-binding-authored struct.
		Settings.SurfaceSearch.TerrainSearchStartZ = 0;
		Settings.SurfaceSearch.TerrainSearchDepthBlocks = 0;
		return Settings;
	}

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

	FLayoutRegionContentSetSolveSnapshot BuildContentSetSnapshotInternal(
		const ULayoutRegionContentSetAsset* ContentSet,
		FRecursiveSnapshotBuildContext* RecursiveContext);

	FLayoutProfileSolveSnapshot BuildProfileSnapshotInternal(
		const ULayoutProfileAsset* Profile,
		FRecursiveSnapshotBuildContext* RecursiveContext);

	TSharedPtr<FLayoutChildRequestTemplateSnapshot> BuildChildRequestTemplateSnapshotInternal(
		const ULayoutProfileAsset* Profile,
		FRecursiveSnapshotBuildContext& RecursiveContext)
	{
		return LayoutChildRequestTemplateCacheBuilder::BuildChildRequestTemplateSnapshot(
			Profile,
			RecursiveContext.ChildTemplateState,
			SnapshotSchemaVersion,
			[&RecursiveContext](const ULayoutProfileAsset* ChildProfile)
			{
				return BuildProfileSnapshotInternal(ChildProfile, &RecursiveContext);
			},
			[&RecursiveContext](const ULayoutRegionContentSetAsset* ChildContentSet)
			{
				return BuildContentSetSnapshotInternal(ChildContentSet, &RecursiveContext);
			},
			[](const ULayoutRegionContentSetAsset* ChildContentSet)
			{
				return FLayoutProfileSolver::BuildModuleCatalog(ChildContentSet);
			});
	}

	FLayoutSparsePlacementRuleSolveSnapshot BuildSparsePlacementRuleSnapshot(
		const FInstancedStruct& Rule,
		FRecursiveSnapshotBuildContext* RecursiveContext)
	{
		FLayoutSparsePlacementRuleSolveSnapshot Snapshot;
		Snapshot.SnapshotSchemaVersion = SparsePlacementProfileSnapshotSchemaVersion;
		const FLayoutSparsePlacementRuleBase* const RuleBase =
			Rule.GetPtr<FLayoutSparsePlacementRuleBase>();
		if (RuleBase == nullptr)
		{
			Snapshot.Validation.AddError(TEXT("Sparse placement rule uses an empty or unsupported struct type."));
			return Snapshot;
		}

		ELayoutSparsePlacementRuleKind RuleKind = ELayoutSparsePlacementRuleKind::PreserveTerrain;
		ELayoutSparseCandidateSource CandidateSource = ELayoutSparseCandidateSource::PreserveSupportedTerrain;
		const ULayoutRegionContentSetAsset* ContentSet = nullptr;
		int32 Count = 0;
		int32 MinCount = 0;
		int32 MaxCount = 0;
		int32 MinSpacingCells = 0;
		if (Rule.GetPtr<FLayoutSparsePreserveTerrainRule>() != nullptr)
		{
			RuleKind = ELayoutSparsePlacementRuleKind::PreserveTerrain;
		}
		else if (const FLayoutSparseExactPlacementRule* ExactRule = Rule.GetPtr<FLayoutSparseExactPlacementRule>())
		{
			RuleKind = ELayoutSparsePlacementRuleKind::Exact;
			CandidateSource = ExactRule->CandidateSource;
			ContentSet = ExactRule->ContentSet;
			Count = ExactRule->Count;
			MinSpacingCells = ExactRule->MinSpacingCells;
		}
		else if (const FLayoutSparseRangePlacementRule* RangeRule = Rule.GetPtr<FLayoutSparseRangePlacementRule>())
		{
			RuleKind = ELayoutSparsePlacementRuleKind::Range;
			CandidateSource = RangeRule->CandidateSource;
			ContentSet = RangeRule->ContentSet;
			MinCount = RangeRule->MinCount;
			MaxCount = RangeRule->MaxCount;
			MinSpacingCells = RangeRule->MinSpacingCells;
		}
		else if (const FLayoutSparseFillAvailablePlacementRule* FillRule = Rule.GetPtr<FLayoutSparseFillAvailablePlacementRule>())
		{
			RuleKind = ELayoutSparsePlacementRuleKind::FillAvailable;
			CandidateSource = FillRule->CandidateSource;
			ContentSet = FillRule->ContentSet;
			MinSpacingCells = FillRule->MinSpacingCells;
		}
		else
		{
			Snapshot.Validation.AddError(FString::Printf(
				TEXT("Sparse placement rule '%s' uses unsupported struct type '%s'."),
				*RuleBase->RuleId.ToString(),
				Rule.GetScriptStruct() != nullptr ? *Rule.GetScriptStruct()->GetName() : TEXT("<none>")));
			return Snapshot;
		}

		FLayoutRegionContentSetSolveSnapshot ContentSetSnapshot;
		FLayoutModuleCatalog ModuleCatalog;
		if (RuleKind != ELayoutSparsePlacementRuleKind::PreserveTerrain)
		{
			ContentSetSnapshot = BuildContentSetSnapshotInternal(ContentSet, RecursiveContext);
			ModuleCatalog = FLayoutProfileSolver::BuildModuleCatalog(ContentSet);
		}
		LayoutSparsePlacementRuleSnapshotBuilder::PopulateSparsePlacementRuleSnapshotBase(
			Snapshot,
			*RuleBase,
			RuleKind,
			CandidateSource,
			Count,
			MinCount,
			MaxCount,
			MinSpacingCells,
			ContentSetSnapshot,
			ModuleCatalog);
		Snapshot.ProofRecords.Add(MakeSnapshotProofRecord(
			FLayoutId(*FString::Printf(TEXT("SparseRuleSnapshot.%s.Copy"), *Snapshot.RuleId.ToString())),
			ELayoutProofKind::SnapshotCopy,
			Snapshot.RuleId,
			{Snapshot.ContentSetSnapshot.SnapshotId, Snapshot.ModuleCatalog.SnapshotId},
			TEXT("Copied typed sparse placement rule into an immutable worker snapshot.")));
		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			FLayoutId(*FString::Printf(TEXT("SparseRuleSnapshot.%s.Initialized"), *Snapshot.RuleId.ToString())),
			ELayoutValidationAssertionKind::SnapshotContractInitialized,
			Snapshot.RuleId != NAME_None,
			{Snapshot.RuleId},
			Snapshot.RuleId != NAME_None
				? FString()
				: TEXT("Sparse placement rule snapshot requires a non-empty RuleId.")));
		AppendFailedAssertionsToValidation(Snapshot.ValidationAssertions, Snapshot.Validation);
		return Snapshot;
	}
}

namespace SnapshotBuilder = LayoutSolveSnapshotBuilderPrivate;

FLayoutModuleSolveSnapshot FLayoutProfileSolver::BuildModuleSnapshot(
	const ULayoutModuleAsset* Module,
	const int32 Weight,
	const ELayoutPlacementZone PlacementZone,
	const ELayoutLevelPlacementPolicy LevelPlacementPolicy,
	const int32 SpecificLevel,
	const bool bOptional,
	const FIntVector& EstablishedSharedCellSizeInBlocks)
{
	FLayoutModuleSolveSnapshot Snapshot = LayoutModuleSnapshotBuilder::BuildLeafModuleSnapshotFromAsset(
		SnapshotBuilder::SnapshotSchemaVersion,
		Module,
		Weight,
		PlacementZone,
		LevelPlacementPolicy,
		SpecificLevel,
		bOptional,
		EstablishedSharedCellSizeInBlocks);
	LayoutModuleDerivedContractBuilder::BuildModuleLocalShapeContracts(Snapshot);
	LayoutModuleDerivedContractBuilder::DeriveModuleInterfaceContracts(Snapshot);
	SnapshotBuilder::AppendFailedAssertionsToValidation(Snapshot.ValidationAssertions, Snapshot.Validation);
	return Snapshot;
}

FLayoutModuleSolveSnapshot FLayoutProfileSolver::BuildCompositeModuleSnapshot(
	const ULayoutCompositeModuleAsset* Composite,
	const int32 Weight,
	const ELayoutPlacementZone PlacementZone,
	const ELayoutLevelPlacementPolicy LevelPlacementPolicy,
	const int32 SpecificLevel,
	const bool bOptional,
	const FIntVector& EstablishedSharedCellSizeInBlocks)
{
	FLayoutModuleSolveSnapshot Snapshot = LayoutCompositeModuleSnapshotBuilder::BuildCompositeModuleSnapshotFromAsset(
		SnapshotBuilder::SnapshotSchemaVersion,
		Composite,
		Weight,
		PlacementZone,
		LevelPlacementPolicy,
		SpecificLevel,
		bOptional,
		EstablishedSharedCellSizeInBlocks);
	LayoutModuleDerivedContractBuilder::DeriveModuleInterfaceContracts(Snapshot);
	SnapshotBuilder::AppendFailedAssertionsToValidation(Snapshot.ValidationAssertions, Snapshot.Validation);
	return Snapshot;
}

FLayoutRegionContentSetSolveSnapshot LayoutSolveSnapshotBuilderPrivate::BuildContentSetSnapshotInternal(
	const ULayoutRegionContentSetAsset* ContentSet,
	FRecursiveSnapshotBuildContext* RecursiveContext)
{
	return LayoutContentSetSolveSnapshotBuilder::BuildContentSetSnapshotFromContentSet(
		SnapshotBuilder::SnapshotSchemaVersion,
		ContentSet,
		[RecursiveContext](const ULayoutProfileAsset* ChildProfile)
		{
			if (RecursiveContext == nullptr || ChildProfile == nullptr)
			{
				return TSharedPtr<FLayoutChildRequestTemplateSnapshot>();
			}

			return SnapshotBuilder::BuildChildRequestTemplateSnapshotInternal(
				ChildProfile,
				*RecursiveContext);
		});
}

FLayoutRegionContentSetSolveSnapshot FLayoutProfileSolver::BuildContentSetSnapshot(const ULayoutRegionContentSetAsset* ContentSet)
{
	SnapshotBuilder::FRecursiveSnapshotBuildContext RecursiveContext;
	return SnapshotBuilder::BuildContentSetSnapshotInternal(ContentSet, &RecursiveContext);
}

FLayoutModuleCatalog FLayoutProfileSolver::BuildModuleCatalog(const ULayoutRegionContentSetAsset* ContentSet)
{
	return LayoutContentSetSolveSnapshotBuilder::BuildModuleCatalogFromContentSet(
		SnapshotBuilder::SnapshotSchemaVersion,
		ContentSet);
}

FLayoutProfileSolveSnapshot LayoutSolveSnapshotBuilderPrivate::BuildProfileSnapshotInternal(
	const ULayoutProfileAsset* Profile,
	FRecursiveSnapshotBuildContext* RecursiveContext)
{
	return LayoutProfileSnapshotBuilder::BuildProfileSnapshotFromProfile(
		SnapshotBuilder::SparsePlacementProfileSnapshotSchemaVersion,
		Profile,
		[RecursiveContext](const FInstancedStruct& SparseRule)
		{
			return SnapshotBuilder::BuildSparsePlacementRuleSnapshot(SparseRule, RecursiveContext);
		});
}

FLayoutProfileSolveSnapshot FLayoutProfileSolver::BuildProfileSnapshot(const ULayoutProfileAsset* Profile)
{
	SnapshotBuilder::FRecursiveSnapshotBuildContext RecursiveContext;
	return SnapshotBuilder::BuildProfileSnapshotInternal(Profile, &RecursiveContext);
}



FLayoutRegionSolveRequest FLayoutProfileSolver::BuildStandaloneRegionRequest(
	const ULayoutRegionContentSetAsset* ContentSet,
	const ULayoutProfileAsset* Profile,
	const int32 Seed,
	const FString& RegionDebugPath,
	const FLayoutSolverExecutionSettings& ExecutionSettings,
	const FLayoutId RootPlacementPolicyId,
	const FLayoutId RootCandidateId,
	const int32 TemplatePlacementZOffsetBlocks,
	const FLayoutId RootSolveId,
	const FIntVector* SharedCellSizeOverride)
{
	const FLayoutId ResolvedRootSolveId =
		RootSolveId != NAME_None ? RootSolveId : (!RegionDebugPath.IsEmpty() ? FLayoutId(*RegionDebugPath) : NAME_None);
	const FLayoutId ResolvedRootCandidateId =
		RootCandidateId != NAME_None ? RootCandidateId : (!RegionDebugPath.IsEmpty() ? FLayoutId(*RegionDebugPath) : NAME_None);
	const FLayoutWorldBindingPlacementPolicy ProfileOnlyPlacementPolicy =
		LayoutSolveSnapshotBuilderPrivate::BuildProfileOnlyStandalonePlacementPolicy();
	return LayoutStandaloneRegionRequestBuilder::BuildRequestFromContentSet(
		SnapshotBuilder::SnapshotSchemaVersion,
		ContentSet,
		Profile,
		Seed,
		RegionDebugPath,
		ExecutionSettings,
		RootPlacementPolicyId,
		ResolvedRootCandidateId,
		TemplatePlacementZOffsetBlocks,
		ResolvedRootSolveId,
		ELayoutWorldBindingPlacementKind::None,
		ProfileOnlyPlacementPolicy,
		nullptr,
		SharedCellSizeOverride);
}

FLayoutRegionSolveRequest FLayoutProfileSolver::BuildStandaloneRegionRequest(
	const ULayoutRegionContentSetAsset* ContentSet,
	const ULayoutProfileAsset* Profile,
	const int32 Seed,
	const FString& RegionDebugPath,
	const FLayoutSolverExecutionSettings& ExecutionSettings,
	const FLayoutId RootPlacementPolicyId,
	const FLayoutId RootCandidateId,
	const int32 TemplatePlacementZOffsetBlocks,
	const FLayoutId RootSolveId,
	const ELayoutWorldBindingPlacementKind RootPlacementKind,
	const FLayoutWorldBindingPlacementPolicy& WorldBindingPlacementPolicy,
	const FLayoutSteppedTerrainSupportMap* SteppedTerrainSupportMap,
	const FIntVector* SharedCellSizeOverride)
{
	const FLayoutId ResolvedRootSolveId =
		RootSolveId != NAME_None ? RootSolveId : (!RegionDebugPath.IsEmpty() ? FLayoutId(*RegionDebugPath) : NAME_None);
	const FLayoutId ResolvedRootCandidateId =
		RootCandidateId != NAME_None ? RootCandidateId : (!RegionDebugPath.IsEmpty() ? FLayoutId(*RegionDebugPath) : NAME_None);
	return LayoutStandaloneRegionRequestBuilder::BuildRequestFromContentSet(
		SnapshotBuilder::SnapshotSchemaVersion,
		ContentSet,
		Profile,
		Seed,
		RegionDebugPath,
		ExecutionSettings,
		RootPlacementPolicyId,
		ResolvedRootCandidateId,
		TemplatePlacementZOffsetBlocks,
		ResolvedRootSolveId,
		RootPlacementKind,
		WorldBindingPlacementPolicy,
		SteppedTerrainSupportMap,
		SharedCellSizeOverride);
}

FLayoutRegionSolveRequest FLayoutProfileSolver::BuildStandaloneRegionRequest(
	const ULayoutProfileAsset* Profile,
	const int32 Seed,
	const FString& RegionDebugPath,
	const FLayoutSolverExecutionSettings& ExecutionSettings,
	const FLayoutId RootPlacementPolicyId,
	const FLayoutId RootCandidateId,
	const int32 TemplatePlacementZOffsetBlocks,
	const FLayoutId RootSolveId,
	const FIntVector* SharedCellSizeOverride)
{
	const FLayoutId ResolvedRootSolveId =
		RootSolveId != NAME_None ? RootSolveId : (!RegionDebugPath.IsEmpty() ? FLayoutId(*RegionDebugPath) : NAME_None);
	const FLayoutId ResolvedRootCandidateId =
		RootCandidateId != NAME_None ? RootCandidateId : (!RegionDebugPath.IsEmpty() ? FLayoutId(*RegionDebugPath) : NAME_None);
	const FLayoutWorldBindingPlacementPolicy ProfileOnlyPlacementPolicy =
		LayoutSolveSnapshotBuilderPrivate::BuildProfileOnlyStandalonePlacementPolicy();
	return LayoutStandaloneRegionRequestBuilder::BuildRequestFromProfile(
		SnapshotBuilder::SnapshotSchemaVersion,
		Profile,
		Seed,
		RegionDebugPath,
		ExecutionSettings,
		RootPlacementPolicyId,
		ResolvedRootCandidateId,
		TemplatePlacementZOffsetBlocks,
		ResolvedRootSolveId,
		ELayoutWorldBindingPlacementKind::None,
		ProfileOnlyPlacementPolicy,
		nullptr,
		SharedCellSizeOverride);
}

FLayoutRegionSolveRequest FLayoutProfileSolver::BuildStandaloneRegionRequest(
	const ULayoutProfileAsset* Profile,
	const int32 Seed,
	const FString& RegionDebugPath,
	const FLayoutSolverExecutionSettings& ExecutionSettings,
	const FLayoutId RootPlacementPolicyId,
	const FLayoutId RootCandidateId,
	const int32 TemplatePlacementZOffsetBlocks,
	const FLayoutId RootSolveId,
	const ELayoutWorldBindingPlacementKind RootPlacementKind,
	const FLayoutWorldBindingPlacementPolicy& WorldBindingPlacementPolicy,
	const FLayoutSteppedTerrainSupportMap* SteppedTerrainSupportMap,
	const FIntVector* SharedCellSizeOverride)
{
	const FLayoutId ResolvedRootSolveId =
		RootSolveId != NAME_None ? RootSolveId : (!RegionDebugPath.IsEmpty() ? FLayoutId(*RegionDebugPath) : NAME_None);
	const FLayoutId ResolvedRootCandidateId =
		RootCandidateId != NAME_None ? RootCandidateId : (!RegionDebugPath.IsEmpty() ? FLayoutId(*RegionDebugPath) : NAME_None);
	return LayoutStandaloneRegionRequestBuilder::BuildRequestFromProfile(
		SnapshotBuilder::SnapshotSchemaVersion,
		Profile,
		Seed,
		RegionDebugPath,
		ExecutionSettings,
		RootPlacementPolicyId,
		ResolvedRootCandidateId,
		TemplatePlacementZOffsetBlocks,
		ResolvedRootSolveId,
		RootPlacementKind,
		WorldBindingPlacementPolicy,
		SteppedTerrainSupportMap,
		SharedCellSizeOverride);
}
