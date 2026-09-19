// Copyright 2026 Spotted Loaf Studio

#include "Layout/Async/LayoutBackgroundSolveSnapshot.h"

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"

namespace
{
	void ScrubModuleSnapshot(FLayoutModuleSolveSnapshot& Snapshot)
	{
		Snapshot.SourceModule = nullptr;
		Snapshot.SourceCompositeModule = nullptr;
	}

	void ScrubModuleCatalog(FLayoutModuleCatalog& Snapshot)
	{
		for (FLayoutModuleSolveSnapshot& ModuleSnapshot : Snapshot.Modules)
		{
			ScrubModuleSnapshot(ModuleSnapshot);
		}
	}

	void ScrubContentSetSnapshot(FLayoutRegionContentSetSolveSnapshot& Snapshot)
	{
		Snapshot.SourceContentSet = nullptr;
	}

	void ScrubProfileSnapshot(FLayoutProfileSolveSnapshot& Snapshot)
	{
		Snapshot.SourceProfile = nullptr;
		for (FLayoutSparsePlacementRuleSolveSnapshot& SparseRule : Snapshot.SparsePlacementRules)
		{
			ScrubContentSetSnapshot(SparseRule.ContentSetSnapshot);
			ScrubModuleCatalog(SparseRule.ModuleCatalog);
		}
	}

	bool ValidateModuleSnapshot(const FLayoutModuleSolveSnapshot& Snapshot, FString& OutFailureReason, const FString& Path)
	{
		if (Snapshot.SourceModule != nullptr || Snapshot.SourceCompositeModule != nullptr)
		{
			OutFailureReason = FString::Printf(TEXT("Worker solve snapshot '%s' still carries live module/composite source pointers."), *Path);
			return false;
		}
		return true;
	}

	bool ValidateModuleCatalog(const FLayoutModuleCatalog& Snapshot, FString& OutFailureReason, const FString& Path)
	{
		for (int32 ModuleIndex = 0; ModuleIndex < Snapshot.Modules.Num(); ++ModuleIndex)
		{
			if (!ValidateModuleSnapshot(Snapshot.Modules[ModuleIndex], OutFailureReason, FString::Printf(TEXT("%s.Module[%d]"), *Path, ModuleIndex)))
			{
				return false;
			}
		}
		return true;
	}

	bool ValidateContentSetSnapshot(const FLayoutRegionContentSetSolveSnapshot& Snapshot, FString& OutFailureReason, const FString& Path)
	{
		if (Snapshot.SourceContentSet != nullptr)
		{
			OutFailureReason = FString::Printf(TEXT("Worker solve snapshot '%s' still carries a live content set source pointer."), *Path);
			return false;
		}
		return true;
	}

	bool ValidateProfileSnapshot(const FLayoutProfileSolveSnapshot& Snapshot, FString& OutFailureReason, const FString& Path)
	{
		if (Snapshot.SourceProfile != nullptr)
		{
			OutFailureReason = FString::Printf(TEXT("Worker solve snapshot '%s' still carries a live profile source pointer."), *Path);
			return false;
		}
		for (int32 RuleIndex = 0; RuleIndex < Snapshot.SparsePlacementRules.Num(); ++RuleIndex)
		{
			const FLayoutSparsePlacementRuleSolveSnapshot& SparseRule = Snapshot.SparsePlacementRules[RuleIndex];
			const FString RulePath = FString::Printf(TEXT("%s.SparseRule[%d]"), *Path, RuleIndex);
			if (!ValidateContentSetSnapshot(SparseRule.ContentSetSnapshot, OutFailureReason, RulePath + TEXT(".ContentSet"))
				|| !ValidateModuleCatalog(SparseRule.ModuleCatalog, OutFailureReason, RulePath + TEXT(".ModuleCatalog")))
			{
				return false;
			}
		}
		return true;
	}
}

FLayoutWorldBindingRuntimeSnapshot FLayoutWorldBindingRuntimeSnapshot::CaptureFromRuntimeView(
	const FLayoutWorldBindingRuntimeView& RuntimeView)
{
	check(IsInGameThread());
	FLayoutWorldBindingRuntimeSnapshot Snapshot;
	Snapshot.PlacementKind = RuntimeView.PlacementKind;
	Snapshot.BindingId = RuntimeView.BindingId;
	Snapshot.CandidateId = RuntimeView.CandidateId;
	Snapshot.MatchingBiomeRowName = RuntimeView.MatchingBiomeRowName;
	Snapshot.CompatibleBiomeRowNames = RuntimeView.CompatibleBiomeRowNames;
	Snapshot.ContinuationSelection = RuntimeView.ContinuationSelection;
	Snapshot.LayoutProfilePath = RuntimeView.LayoutProfile != nullptr ? FSoftObjectPath(RuntimeView.LayoutProfile->GetPathName()) : FSoftObjectPath();
	Snapshot.ContentSetPath = RuntimeView.ContentSet != nullptr ? FSoftObjectPath(RuntimeView.ContentSet->GetPathName()) : FSoftObjectPath();
	Snapshot.ExportedConnectorTypeTags = RuntimeView.ExportedConnectorTypeTags;
	Snapshot.SharedCellSizeInBlocks = RuntimeView.SharedCellSizeInBlocks;
	Snapshot.TemplatePlacementZOffsetBlocks = RuntimeView.TemplatePlacementZOffsetBlocks;
	Snapshot.PlacementPolicy = RuntimeView.PlacementPolicy;
	Snapshot.ContinuationPolicy = RuntimeView.ContinuationPolicy;
	Snapshot.SolveBudget = RuntimeView.SolveBudget;
	return Snapshot;
}

FLayoutWorldBindingRuntimeSnapshot FLayoutWorldBindingRuntimeSnapshot::CaptureFromConnectorRecord(
	const FResolvedLayoutConnectorRecord& ConnectorRecord)
{
	check(IsInGameThread());
	FLayoutWorldBindingRuntimeSnapshot Snapshot;
	Snapshot.PlacementKind = ConnectorRecord.PlacementKind;
	Snapshot.BindingId = ConnectorRecord.WorldBindingId;
	Snapshot.CandidateId = ConnectorRecord.ContinuationFamilyCandidateId;
	Snapshot.ContinuationSelection = ConnectorRecord.ResolvedContinuationSelection;
	Snapshot.LayoutProfilePath = ConnectorRecord.LayoutProfile.ToSoftObjectPath();
	Snapshot.ContentSetPath = ConnectorRecord.ContentSet.ToSoftObjectPath();
	Snapshot.SharedCellSizeInBlocks = ConnectorRecord.FrontendSharedCellSizeInBlocks;
	Snapshot.TemplatePlacementZOffsetBlocks = ConnectorRecord.FrontendTemplatePlacementZOffsetBlocks;
	Snapshot.PlacementPolicy = ConnectorRecord.WorldBindingPlacementPolicy;
	Snapshot.ContinuationPolicy = ConnectorRecord.ContinuationPolicy;
	Snapshot.SolveBudget = ConnectorRecord.SolveBudget;
	return Snapshot;
}

bool FLayoutWorldBindingRuntimeSnapshot::ValidateNoLiveObjectCarriers(FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	return true;
}

FLayoutRegionSolveRequest LayoutBackgroundSolveSnapshot::MakeWorkerSafeSolveRequest(const FLayoutRegionSolveRequest& Request)
{
	FLayoutRegionSolveRequest WorkerRequest = Request;
	ScrubProfileSnapshot(WorkerRequest.ProfileSnapshot);
	ScrubContentSetSnapshot(WorkerRequest.ContentSetSnapshot);
	ScrubModuleCatalog(WorkerRequest.ModuleCatalog);
	return WorkerRequest;
}

bool LayoutBackgroundSolveSnapshot::ValidateWorkerSafeSolveRequest(const FLayoutRegionSolveRequest& Request, FString& OutFailureReason)
{
	OutFailureReason.Reset();
	return ValidateProfileSnapshot(Request.ProfileSnapshot, OutFailureReason, TEXT("ProfileSnapshot"))
		&& ValidateContentSetSnapshot(Request.ContentSetSnapshot, OutFailureReason, TEXT("ContentSetSnapshot"))
		&& ValidateModuleCatalog(Request.ModuleCatalog, OutFailureReason, TEXT("ModuleCatalog"));
}

void LayoutBackgroundSolveSnapshot::ScrubWorkerSolveResult(FLayoutSolveResult& Result)
{
	for (FLayoutPlacedModule& Placement : Result.Placements)
	{
		Placement.Module = nullptr;
		Placement.CompositeModule = nullptr;
	}
}

bool LayoutBackgroundSolveSnapshot::ValidateWorkerSafeSolveResult(const FLayoutSolveResult& Result, FString& OutFailureReason)
{
	OutFailureReason.Reset();
	for (int32 PlacementIndex = 0; PlacementIndex < Result.Placements.Num(); ++PlacementIndex)
	{
		const FLayoutPlacedModule& Placement = Result.Placements[PlacementIndex];
		if (Placement.Module != nullptr || Placement.CompositeModule != nullptr)
		{
			OutFailureReason = FString::Printf(TEXT("Worker solve result placement %d still carries live module/composite pointers."), PlacementIndex);
			return false;
		}
	}
	return true;
}
