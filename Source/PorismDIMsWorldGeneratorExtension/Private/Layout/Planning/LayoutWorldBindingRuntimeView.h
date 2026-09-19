// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Types/LayoutTypes.h"

class UObject;
class ULayoutProfileAsset;
class ULayoutRegionContentSetAsset;
class ULayoutWorldBindingAsset;
struct FLayoutWorldBindingCandidate;
struct FLayoutWorldBindingContinuationCandidate;
struct FLayoutWorldBindingContinuationFamily;
enum class ELayoutWorldBindingContinuationFamilyType : uint8;

/** One resolved world-binding-facing selection shared by current planning and direct-root selection paths. */
struct FLayoutWorldBindingRuntimeView
{
	/** Resolved world-facing placement kind for the selected binding/candidate context. */
	ELayoutWorldBindingPlacementKind PlacementKind = ELayoutWorldBindingPlacementKind::None;

	/** Stable world-binding identity carried through planning records and solve requests. */
	FName BindingId;

	/** Stable candidate identity carried through planning records and solve requests. */
	FName CandidateId;

	/** Worker-safe matching biome row selected by the source planning/discovery surface. */
	FName MatchingBiomeRowName;

	/** Pointer-free compatible biome allow-list selected upstream; empty until an authoritative producer exists. */
	TArray<FName> CompatibleBiomeRowNames;

	/** Minimal resolved continuation-selection contract carried by future path/bridge/tunnel world-binding requests. */
	FLayoutResolvedWorldBindingContinuationSelection ContinuationSelection;

	/** Loaded profile asset selected by the current binding/candidate context. */
	ULayoutProfileAsset* LayoutProfile = nullptr;

	/** Preferred content set resolved from the selected profile, when present. */
	ULayoutRegionContentSetAsset* ContentSet = nullptr;

	/** Binding-owned ordinary-root endpoint connector tags resolved from the selected candidate. */
	FGameplayTagContainer ExportedConnectorTypeTags;

	/** Shared cell size carried by the current content/module owner when one exists. */
	FIntVector SharedCellSizeInBlocks = FIntVector::ZeroValue;

	/** Request-owned template placement offset carried by the current binding/runtime contract. */
	int32 TemplatePlacementZOffsetBlocks = 0;

	/** Resolved world-facing terrain/alignment policy for this selected placement. */
	FLayoutWorldBindingPlacementPolicy PlacementPolicy;

	/** Resolved continuation-family path policy for this selected placement, when applicable. */
	FLayoutWorldBindingContinuationPolicy ContinuationPolicy;

	/** Root solve budget carried by the current binding/candidate context. */
	FLayoutRootSolveBudgetSettings SolveBudget;
};

/** One resolved authored ordinary-root candidate target selected for one site center. */
struct FLayoutWorldBindingCandidateTarget
{
	/** Index of the selected authored root candidate within the source binding. */
	int32 CandidateIndex = INDEX_NONE;

	/** Selected authored root candidate chosen for the current site center. */
	const FLayoutWorldBindingCandidate* Candidate = nullptr;
};

/** One resolved authored continuation-family target selected for one endpoint tag. */
struct FLayoutWorldBindingContinuationFamilyTarget
{
	/** Index of the selected authored continuation family within the source binding. */
	int32 FamilyIndex = INDEX_NONE;

	/** Selected authored continuation family selected for the current endpoint tag. */
	const FLayoutWorldBindingContinuationFamily* Family = nullptr;
};

/** One resolved authored continuation-family candidate target selected for one endpoint pair. */
struct FLayoutWorldBindingContinuationFamilyCandidateTarget
{
	/** Index of the selected authored continuation family within the source binding. */
	int32 FamilyIndex = INDEX_NONE;

	/** Selected authored continuation family selected for the current endpoint tag. */
	const FLayoutWorldBindingContinuationFamily* Family = nullptr;

	/** Index of the selected authored continuation-family candidate within that family. */
	int32 CandidateIndex = INDEX_NONE;

	/** Selected authored continuation-family candidate chosen for the current endpoint pair. */
	const FLayoutWorldBindingContinuationCandidate* Candidate = nullptr;
};

namespace LayoutWorldBindingRuntimeView
{
	/** Identical weighted order/hash for authored and captured continuation families, including invalid slots. */
	template <typename FamilyType>
	int32 ChooseWeightedWorldBindingContinuationCandidateIndex(const FamilyType& Family, const uint64 PairKey, const int32 WorldSeed)
	{
		if (Family.Candidates.IsEmpty()) return INDEX_NONE;
		int32 TotalWeight = 0;
		for (const auto& Candidate : Family.Candidates) TotalWeight += FMath::Max(1, Candidate.Weight);
		if (TotalWeight <= 0) return INDEX_NONE;
		const uint32 SelectionSeed = HashCombineFast(
			HashCombineFast(static_cast<uint32>(WorldSeed), GetTypeHash(Family.FamilyId)),
			HashCombineFast(static_cast<uint32>(PairKey), static_cast<uint32>(Family.Candidates.Num())));
		const int32 WeightedPick = static_cast<int32>(SelectionSeed % static_cast<uint32>(TotalWeight));
		int32 RunningWeight = 0;
		for (int32 Index = 0; Index < Family.Candidates.Num(); ++Index)
		{
			RunningWeight += FMath::Max(1, Family.Candidates[Index].Weight);
			if (WeightedPick < RunningWeight) return Index;
		}
		return Family.Candidates.Num() - 1;
	}

	/** Shares unchanged weight clamping, candidate order and site hashing between authored and captured inputs. */
	template <typename CandidateType>
	int32 ChooseWeightedWorldBindingCandidateIndex(
		const FName BindingId,
		const TArray<CandidateType>& Candidates,
		const FIntVector& SiteCenterBlockWorldPos,
		const int32 WorldSeed)
	{
		if (Candidates.IsEmpty())
		{
			return INDEX_NONE;
		}

		int32 TotalWeight = 0;
		for (const CandidateType& Candidate : Candidates)
		{
			TotalWeight += FMath::Max(1, Candidate.Weight);
		}

		if (TotalWeight <= 0)
		{
			return INDEX_NONE;
		}

		const uint32 SelectionSeed = HashCombineFast(
			HashCombineFast(static_cast<uint32>(WorldSeed), GetTypeHash(BindingId)),
			HashCombineFast(GetTypeHash(SiteCenterBlockWorldPos), static_cast<uint32>(Candidates.Num())));
		const int32 WeightedPick = static_cast<int32>(SelectionSeed % static_cast<uint32>(TotalWeight));

		int32 RunningWeight = 0;
		for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
		{
			RunningWeight += FMath::Max(1, Candidates[CandidateIndex].Weight);
			if (WeightedPick < RunningWeight)
			{
				return CandidateIndex;
			}
		}

		return Candidates.Num() - 1;
	}

	/**
	 * Resolves one authored ordinary-root candidate by deterministic weighted
	 * selection from site center and world seed so ordinary-root site planning
	 * can share the same authored target selection surface.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryResolveWorldBindingCandidateTarget(
		const ULayoutWorldBindingAsset* WorldBinding,
		FIntVector SiteCenterBlockWorldPos,
		int32 WorldSeed,
		FLayoutWorldBindingCandidateTarget& OutTarget,
		FString& OutFailureReason);

	/**
	 * Resolves one authored continuation family by deterministic family type plus
	 * endpoint connector tag so bounded continuation planners can share the same
	 * family-target lookup surface.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryResolveContinuationFamilyTarget(
		const ULayoutWorldBindingAsset* WorldBinding,
		ELayoutWorldBindingContinuationFamilyType FamilyType,
		FGameplayTag EndpointConnectorTypeTag,
		FLayoutWorldBindingContinuationFamilyTarget& OutTarget,
		FString& OutFailureReason);

	/**
	 * Resolves one authored continuation family plus one deterministic weighted
	 * family candidate for the current endpoint pair so bounded continuation
	 * planners can share the same family-and-candidate target selection surface.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryResolveContinuationFamilyCandidateTarget(
		const ULayoutWorldBindingAsset* WorldBinding,
		ELayoutWorldBindingContinuationFamilyType FamilyType,
		FGameplayTag EndpointConnectorTypeTag,
		uint64 PairKey,
		int32 WorldSeed,
		FLayoutWorldBindingContinuationFamilyCandidateTarget& OutTarget,
		FString& OutFailureReason);

	/**
	 * Resolves one deterministic weighted continuation-family candidate from an
	 * already-resolved family target so pair planners can reuse shared family
	 * lookup work instead of rescanning authored families for every endpoint pair.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryResolveContinuationFamilyCandidateTargetFromFamilyTarget(
		const ULayoutWorldBindingAsset* WorldBinding,
		const FLayoutWorldBindingContinuationFamilyTarget& FamilyTarget,
		uint64 PairKey,
		int32 WorldSeed,
		FLayoutWorldBindingContinuationFamilyCandidateTarget& OutTarget,
		FString& OutFailureReason);

	/**
	 * Resolves one authored world binding plus one selected candidate into the
	 * current runtime-facing binding view.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildRuntimeViewFromWorldBinding(
		const ULayoutWorldBindingAsset* WorldBinding,
		int32 CandidateIndex,
		FName MatchingBiomeRowName,
		FLayoutWorldBindingRuntimeView& OutView,
		FString& OutFailureReason);

	/**
	 * Resolves one authored world binding plus one already-selected ordinary-root
	 * target into the same runtime-facing binding view so site planning can stay
	 * on the shared target-selection surface through runtime-view rebuild.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildRuntimeViewFromWorldBindingCandidateTarget(
		const ULayoutWorldBindingAsset* WorldBinding,
		const FLayoutWorldBindingCandidateTarget& CandidateTarget,
		FName MatchingBiomeRowName,
		FLayoutWorldBindingRuntimeView& OutView,
		FString& OutFailureReason);

	/**
	 * Resolves one authored world binding plus one already-owned ordinary-root
	 * candidate id into the same runtime-facing binding view so planned/resolved
	 * site records can rebuild authored state without reopening weighted target
	 * selection.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildRuntimeViewFromWorldBindingCandidateId(
		const ULayoutWorldBindingAsset* WorldBinding,
		FName CandidateId,
		FName MatchingBiomeRowName,
		FLayoutWorldBindingRuntimeView& OutView,
		FString& OutFailureReason);

	/**
	 * Resolves one authored continuation family plus one selected family
	 * candidate into the same runtime-facing binding view used by shared
	 * request-building paths. `MatchingBiomeRowName` may be `NAME_None` when a
	 * cached same-binding cross-biome connector no longer preserves one shared
	 * row name but still retains valid live family/candidate identity.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildRuntimeViewFromWorldBindingContinuationFamily(
		const ULayoutWorldBindingAsset* WorldBinding,
		int32 FamilyIndex,
		int32 CandidateIndex,
		int32 ResolvedEntryLevel,
		FName MatchingBiomeRowName,
		FLayoutWorldBindingRuntimeView& OutView,
		FString& OutFailureReason);

	/**
	 * Rebuilds one runtime-facing binding view from a planned-site record plus
	 * the authored world binding that produced it.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildRuntimeViewFromPlannedSiteRecord(
		const ULayoutWorldBindingAsset* WorldBinding,
		const FPlannedLayoutSiteRecord& PlannedSiteRecord,
		FLayoutWorldBindingRuntimeView& OutView,
		FString& OutFailureReason);

	/**
	 * Rebuilds one runtime-facing binding view from a resolved site record after
	 * planning-window import preserved the binding/candidate/continuation carrier.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildRuntimeViewFromResolvedSiteRecord(
		const ULayoutWorldBindingAsset* WorldBinding,
		const FResolvedLayoutSiteRecord& ResolvedSiteRecord,
		FLayoutWorldBindingRuntimeView& OutView,
		FString& OutFailureReason);

	/**
	 * Rebuilds one connector-facing runtime view from a resolved connector
	 * record so connector request assembly can reuse the shared world-binding
	 * frontend contract instead of keeping a second local synthesis helper.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildRuntimeViewFromResolvedConnectorRecord(
		const ULayoutWorldBindingAsset* WorldBinding,
		const FResolvedLayoutConnectorRecord& ResolvedConnectorRecord,
		const FLayoutRootSolveBudgetSettings& SolveBudget,
		FLayoutWorldBindingRuntimeView& OutView,
		FString& OutFailureReason);

	/** Internal binding-aware explicit-root rebuild seam used by runtime helpers and editor tooling. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildExplicitToolRuntimeViewFromWorldBindingProfile(
		const ULayoutWorldBindingAsset* WorldBinding,
		ULayoutProfileAsset* LayoutProfile,
		FLayoutWorldBindingRuntimeView& OutView,
		FLayoutWorldBindingSiteFrontendSelection& OutFrontendSelection,
		FString& OutFailureReason);

}
