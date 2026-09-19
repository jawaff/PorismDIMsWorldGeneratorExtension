// Copyright 2026 Spotted Loaf Studio

#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"

#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Runtime/LayoutWorldBindingRuntimeHelpers.h"

namespace
{
	FName ResolveWorldBindingRuntimeBindingId(const ULayoutWorldBindingAsset* const WorldBinding)
	{
		if (WorldBinding == nullptr)
		{
			return NAME_None;
		}

		return !WorldBinding->BindingId.IsNone()
			? WorldBinding->BindingId
			: WorldBinding->GetFName();
	}

	FGameplayTagContainer ResolveEffectiveWorldBindingCandidateExportedConnectorTypeTags(
		const FLayoutWorldBindingCandidate& Candidate)
	{
		return Candidate.ExportedConnectorTypeTags;
	}

	ELayoutWorldBindingPlacementKind ResolveContinuationFamilyPlacementKind(
		const ELayoutWorldBindingContinuationFamilyType FamilyType)
	{
		switch (FamilyType)
		{
		case ELayoutWorldBindingContinuationFamilyType::SurfacePath:
			return ELayoutWorldBindingPlacementKind::SurfacePath;
		case ELayoutWorldBindingContinuationFamilyType::BridgeContinuation:
			return ELayoutWorldBindingPlacementKind::BridgeContinuation;
		case ELayoutWorldBindingContinuationFamilyType::TunnelContinuation:
			return ELayoutWorldBindingPlacementKind::TunnelContinuation;
		default:
			return ELayoutWorldBindingPlacementKind::None;
		}
	}

	FLayoutWorldBindingPlacementPolicy BuildOrdinaryRootRuntimePlacementPolicy(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutWorldBindingCandidate& Candidate)
	{
		FLayoutWorldBindingPlacementPolicy PlacementPolicy =
			WorldBinding != nullptr
				? WorldBinding->DefaultPlacementPolicy
				: FLayoutWorldBindingPlacementPolicy();
		if (Candidate.bOverrideTerrainTransitionPolicy)
		{
			PlacementPolicy.TerrainTransition = Candidate.TerrainTransitionPolicyOverride;
		}
		return PlacementPolicy;
	}

	FLayoutWorldBindingPlacementPolicy BuildContinuationFamilyRuntimePlacementPolicy(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutWorldBindingContinuationFamily& Family)
	{
		FLayoutWorldBindingPlacementPolicy PlacementPolicy =
			WorldBinding != nullptr
				? WorldBinding->DefaultPlacementPolicy
				: FLayoutWorldBindingPlacementPolicy();
		if (Family.bOverrideTerrainTransitionPolicy)
		{
			PlacementPolicy.TerrainTransition =
				Family.TerrainTransitionPolicyOverride;
		}
		return PlacementPolicy;
	}

	FLayoutWorldBindingContinuationPolicy BuildContinuationFamilyRuntimePolicy(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutWorldBindingContinuationFamily& Family)
	{
		FLayoutWorldBindingContinuationPolicy ContinuationPolicy = Family.ContinuationPolicy;
		return ContinuationPolicy;
	}


	int32 FindWorldBindingCandidateIndexById(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FName CandidateId)
	{
		if (WorldBinding == nullptr || CandidateId.IsNone())
		{
			return INDEX_NONE;
		}

		for (int32 CandidateIndex = 0; CandidateIndex < WorldBinding->Candidates.Num(); ++CandidateIndex)
		{
			if (WorldBinding->Candidates[CandidateIndex].CandidateId == CandidateId)
			{
				return CandidateIndex;
			}
		}

		return INDEX_NONE;
	}

	const FLayoutWorldBindingContinuationFamily* FindWorldBindingContinuationFamilyById(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FName FamilyId)
	{
		if (WorldBinding == nullptr || FamilyId.IsNone())
		{
			return nullptr;
		}

		for (const FLayoutWorldBindingContinuationFamily& Family : WorldBinding->ContinuationFamilies)
		{
			if (Family.FamilyId == FamilyId)
			{
				return &Family;
			}
		}

		return nullptr;
	}

	int32 FindWorldBindingContinuationFamilyIndexById(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FName FamilyId)
	{
		if (WorldBinding == nullptr || FamilyId.IsNone())
		{
			return INDEX_NONE;
		}

		for (int32 FamilyIndex = 0; FamilyIndex < WorldBinding->ContinuationFamilies.Num(); ++FamilyIndex)
		{
			if (WorldBinding->ContinuationFamilies[FamilyIndex].FamilyId == FamilyId)
			{
				return FamilyIndex;
			}
		}

		return INDEX_NONE;
	}

	int32 FindWorldBindingContinuationFamilyCandidateIndexById(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const int32 FamilyIndex,
		const FName CandidateId)
	{
		if (WorldBinding == nullptr
			|| !WorldBinding->ContinuationFamilies.IsValidIndex(FamilyIndex)
			|| CandidateId.IsNone())
		{
			return INDEX_NONE;
		}

		const TArray<FLayoutWorldBindingContinuationCandidate>& Candidates =
			WorldBinding->ContinuationFamilies[FamilyIndex].Candidates;
		for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
		{
			if (Candidates[CandidateIndex].CandidateId == CandidateId)
			{
				return CandidateIndex;
			}
		}

		return INDEX_NONE;
	}

	bool HasResolvedConnectorWorldBindingFrontendCarrierForRuntimeView(
		const FResolvedLayoutConnectorRecord& ResolvedConnectorRecord)
	{
		const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
			ResolvedConnectorRecord.GetResolvedConnectorFrontendSelection();
		// Default nested placement-policy state is not authoritative proof that a
		// connector came from the world-binding continuation path.
		return FrontendSelection.PlacementKind != ELayoutWorldBindingPlacementKind::None
			|| !FrontendSelection.ResolvedContinuationSelection.FamilyId.IsNone()
			|| !FrontendSelection.ContinuationFamilyId.IsNone();
	}

	int32 ResolveResolvedConnectorRuntimeEntryLevel(
		const FResolvedLayoutConnectorRecord& ResolvedConnectorRecord)
	{
		const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
			ResolvedConnectorRecord.GetResolvedConnectorFrontendSelection();
		return FrontendSelection.ResolvedContinuationSelection.ResolvedEntryLevel != INDEX_NONE
			? FrontendSelection.ResolvedContinuationSelection.ResolvedEntryLevel
			: ResolvedConnectorRecord.SolveResult.ResolvedTerrainAlignmentLevel;
	}

	ELayoutWorldBindingPlacementKind ResolveResolvedConnectorRuntimePlacementKind(
		const FResolvedLayoutConnectorRecord& ResolvedConnectorRecord)
	{
		const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
			ResolvedConnectorRecord.GetResolvedConnectorFrontendSelection();
		// The final normalized connector frontend contract must preserve placement
		// kind explicitly. Nested continuation-policy state is no longer an
		// authoritative fallback once connector records are validated.
		if (FrontendSelection.PlacementKind != ELayoutWorldBindingPlacementKind::None)
		{
			return FrontendSelection.PlacementKind;
		}

		if (FrontendSelection.ResolvedContinuationSelection.PlacementKind
			!= ELayoutWorldBindingPlacementKind::None)
		{
			return FrontendSelection.ResolvedContinuationSelection.PlacementKind;
		}

		return ResolvedConnectorRecord.SolveResult.RootPlacementKind;
	}

	FLayoutWorldBindingPlacementPolicy ResolveResolvedConnectorRuntimePlacementPolicy(
		const FResolvedLayoutConnectorRecord& ResolvedConnectorRecord)
	{
		const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
			ResolvedConnectorRecord.GetResolvedConnectorFrontendSelection();
		const auto NormalizeContinuationPlacementPolicy =
			[&FrontendSelection, &ResolvedConnectorRecord](FLayoutWorldBindingPlacementPolicy PlacementPolicy)
		{
			const ELayoutWorldBindingPlacementKind ResolvedPlacementKind =
				ResolveResolvedConnectorRuntimePlacementKind(ResolvedConnectorRecord);
			const bool bContinuationPlacement =
				ResolvedPlacementKind == ELayoutWorldBindingPlacementKind::SurfacePath
				|| ResolvedPlacementKind == ELayoutWorldBindingPlacementKind::BridgeContinuation
				|| ResolvedPlacementKind == ELayoutWorldBindingPlacementKind::TunnelContinuation;
			if (bContinuationPlacement)
			{
				// Connector path discovery already consumed the continuation slope
				// budget. Keep the rebuilt runtime-view placement policy on the same
				// ceiling so later stepped-contract compilation evaluates the same
				// continuation family that path selection already accepted.
			}
			return PlacementPolicy;
		};

		if (ResolveResolvedConnectorRuntimePlacementKind(ResolvedConnectorRecord)
			!= ELayoutWorldBindingPlacementKind::OrdinaryRoot)
		{
			return NormalizeContinuationPlacementPolicy(
				FrontendSelection.WorldBindingPlacementPolicy);
		}

		return NormalizeContinuationPlacementPolicy(
			ResolvedConnectorRecord.SolveResult.WorldBindingPlacementPolicy);
	}

	bool TryBuildRuntimeViewBase(
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FName BindingId,
		const FName CandidateId,
		ULayoutProfileAsset* const LoadedProfile,
		const FGameplayTagContainer& ExportedConnectorTypeTags,
		const FLayoutWorldBindingPlacementPolicy& PlacementPolicy,
		const FLayoutWorldBindingContinuationPolicy& ContinuationPolicy,
		const FLayoutRootSolveBudgetSettings& SolveBudgetSettings,
		const int32 TemplatePlacementZOffsetBlocks,
		const FIntVector& EstablishedSharedCellSizeInBlocks,
		const bool bRequireSharedCellSize,
		FLayoutWorldBindingRuntimeView& OutView,
		FString& OutFailureReason)
	{
		OutView = FLayoutWorldBindingRuntimeView();
		OutFailureReason.Reset();

		ULayoutRegionContentSetAsset* const LoadedContentSet =
			LayoutWorldBindingRuntimeHelpers::ResolveRuntimePreferredContentSet(LoadedProfile);
		if (LoadedContentSet == nullptr)
		{
			OutFailureReason = FString::Printf(
				TEXT("profile=%s contentSet=%s."),
				*GetNameSafe(LoadedProfile),
				*GetNameSafe(LoadedContentSet),
				TEXT(""));
			return false;
		}

		OutView.PlacementKind = PlacementKind;
		OutView.BindingId = BindingId;
		OutView.CandidateId = CandidateId;
		OutView.LayoutProfile = LoadedProfile;
		OutView.ContentSet = LoadedContentSet;
		OutView.ExportedConnectorTypeTags = ExportedConnectorTypeTags;
		FLayoutWorldBindingPlacementPolicy EffectivePlacementPolicy = PlacementPolicy;
		const bool bContinuationPlacement =
			PlacementKind == ELayoutWorldBindingPlacementKind::SurfacePath
			|| PlacementKind == ELayoutWorldBindingPlacementKind::BridgeContinuation
			|| PlacementKind == ELayoutWorldBindingPlacementKind::TunnelContinuation;
		if (bContinuationPlacement)
		{
			// Continuation path discovery already admitted the family on its
			// authored slope ceiling. Keep the runtime-view placement carrier on
			// that same ceiling so stepped-contract compilation does not collapse
			// the accepted continuation family back to the direct-root default.
		}
		const bool bEstablishedSharedCellSizeProvided = EstablishedSharedCellSizeInBlocks != FIntVector::ZeroValue;
		const FIntVector CandidateSharedCellSizeInBlocks =
			bEstablishedSharedCellSizeProvided
				? FIntVector::ZeroValue
				: LayoutWorldBindingRuntimeHelpers::ResolveRuntimeSharedCellSizeInBlocks(LoadedContentSet);
		OutView.SharedCellSizeInBlocks = bEstablishedSharedCellSizeProvided
			? EstablishedSharedCellSizeInBlocks
			: CandidateSharedCellSizeInBlocks;
		OutView.TemplatePlacementZOffsetBlocks = TemplatePlacementZOffsetBlocks;
		OutView.PlacementPolicy = EffectivePlacementPolicy;
		OutView.ContinuationPolicy = ContinuationPolicy;
		OutView.SolveBudget = SolveBudgetSettings;

		if (bRequireSharedCellSize && OutView.SharedCellSizeInBlocks == FIntVector::ZeroValue)
		{
			OutFailureReason = FString::Printf(
				TEXT("profile=%s contentSet=%s sharedCellSize=%s."),
				*GetNameSafe(LoadedProfile),
				*GetNameSafe(LoadedContentSet),
				*OutView.SharedCellSizeInBlocks.ToString());
			return false;
		}

		return true;
	}

	bool TryApplyStoredContinuationSelection(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutResolvedWorldBindingContinuationSelection& ContinuationSelection,
		FLayoutWorldBindingRuntimeView& OutView,
		FString& OutFailureReason)
	{
		OutView.ContinuationSelection = ContinuationSelection;
		if (ContinuationSelection.FamilyId.IsNone())
		{
			if (ContinuationSelection.PlacementKind != ELayoutWorldBindingPlacementKind::None
				|| ContinuationSelection.ResolvedEntryLevel != INDEX_NONE)
			{
				OutView = FLayoutWorldBindingRuntimeView();
				OutFailureReason = FString::Printf(
					TEXT("worldBinding=%s continuationFamilyId=<none> storedPlacementKind=%d resolvedEntryLevel=%d."),
					*GetNameSafe(WorldBinding),
					static_cast<int32>(ContinuationSelection.PlacementKind),
					ContinuationSelection.ResolvedEntryLevel);
				return false;
			}

			return true;
		}

		if (const FLayoutWorldBindingContinuationFamily* Family =
			FindWorldBindingContinuationFamilyById(WorldBinding, ContinuationSelection.FamilyId))
		{
			const FLayoutWorldBindingPlacementPolicy FamilyRuntimePlacementPolicy =
				BuildContinuationFamilyRuntimePlacementPolicy(WorldBinding, *Family);
			const ELayoutWorldBindingPlacementKind FamilyRuntimePlacementKind =
				ResolveContinuationFamilyPlacementKind(Family->FamilyType);
			const FLayoutWorldBindingContinuationPolicy FamilyRuntimePolicy =
				BuildContinuationFamilyRuntimePolicy(WorldBinding, *Family);
			if (ContinuationSelection.PlacementKind != ELayoutWorldBindingPlacementKind::None
				&& ContinuationSelection.PlacementKind
					!= FamilyRuntimePlacementKind)
			{
				OutView = FLayoutWorldBindingRuntimeView();
				OutFailureReason = FString::Printf(
					TEXT("worldBinding=%s continuationFamilyId=%s storedPlacementKind=%d resolvedFamilyPlacementKind=%d."),
					*GetNameSafe(WorldBinding),
					*ContinuationSelection.FamilyId.ToString(),
					static_cast<int32>(ContinuationSelection.PlacementKind),
					static_cast<int32>(FamilyRuntimePlacementKind));
				return false;
			}

			OutView.ContinuationSelection.PlacementKind =
				FamilyRuntimePlacementKind;
			OutView.PlacementPolicy = FamilyRuntimePlacementPolicy;
			OutView.ContinuationPolicy = FamilyRuntimePolicy;
			OutView.PlacementKind = FamilyRuntimePlacementKind;
			return true;
		}

		OutView = FLayoutWorldBindingRuntimeView();
		OutFailureReason = FString::Printf(
			TEXT("worldBinding=%s continuationFamilyId=%s."),
			*GetNameSafe(WorldBinding),
			*ContinuationSelection.FamilyId.ToString());
		return false;
	}
}

namespace LayoutWorldBindingRuntimeView
{
	bool TryResolveContinuationFamilyCandidateTargetFromFamilyTarget(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutWorldBindingContinuationFamilyTarget& FamilyTarget,
		const uint64 PairKey,
		const int32 WorldSeed,
		FLayoutWorldBindingContinuationFamilyCandidateTarget& OutTarget,
		FString& OutFailureReason);

	bool TryResolveWorldBindingCandidateTarget(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FIntVector SiteCenterBlockWorldPos,
		const int32 WorldSeed,
		FLayoutWorldBindingCandidateTarget& OutTarget,
		FString& OutFailureReason)
	{
		OutTarget = FLayoutWorldBindingCandidateTarget();
		OutFailureReason.Reset();

		const int32 CandidateIndex = WorldBinding != nullptr ? ChooseWeightedWorldBindingCandidateIndex(
			ResolveWorldBindingRuntimeBindingId(WorldBinding), WorldBinding->Candidates,
			SiteCenterBlockWorldPos, WorldSeed) : INDEX_NONE;
		if (WorldBinding == nullptr || !WorldBinding->Candidates.IsValidIndex(CandidateIndex))
		{
			OutFailureReason = FString::Printf(
				TEXT("worldBinding=%s siteCenter=%s worldSeed=%d candidateIndex=%d candidateCount=%d."),
				*GetNameSafe(WorldBinding),
				*SiteCenterBlockWorldPos.ToString(),
				WorldSeed,
				CandidateIndex,
				WorldBinding != nullptr ? WorldBinding->Candidates.Num() : 0);
			return false;
		}

		OutTarget.CandidateIndex = CandidateIndex;
		OutTarget.Candidate = &WorldBinding->Candidates[CandidateIndex];
		return true;
	}

	bool TryResolveContinuationFamilyTarget(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const FGameplayTag EndpointConnectorTypeTag,
		FLayoutWorldBindingContinuationFamilyTarget& OutTarget,
		FString& OutFailureReason)
	{
		OutTarget = FLayoutWorldBindingContinuationFamilyTarget();
		OutFailureReason.Reset();

		if (WorldBinding == nullptr || !EndpointConnectorTypeTag.IsValid())
		{
			OutFailureReason = FString::Printf(
				TEXT("worldBinding=%s familyType=%d endpointTag=%s."),
				*GetNameSafe(WorldBinding),
				static_cast<int32>(FamilyType),
				*EndpointConnectorTypeTag.ToString());
			return false;
		}

		for (int32 FamilyIndex = 0; FamilyIndex < WorldBinding->ContinuationFamilies.Num(); ++FamilyIndex)
		{
			const FLayoutWorldBindingContinuationFamily& Family =
				WorldBinding->ContinuationFamilies[FamilyIndex];
			if (Family.FamilyType == FamilyType
				&& Family.EndpointConnectorTypeTag == EndpointConnectorTypeTag)
			{
				OutTarget.FamilyIndex = FamilyIndex;
				OutTarget.Family = &Family;
				return true;
			}
		}

		OutFailureReason = FString::Printf(
			TEXT("worldBinding=%s familyType=%d endpointTag=%s continuationFamilyCount=%d."),
			*GetNameSafe(WorldBinding),
			static_cast<int32>(FamilyType),
			*EndpointConnectorTypeTag.ToString(),
			WorldBinding->ContinuationFamilies.Num());
		return false;
	}

	bool TryResolveContinuationFamilyCandidateTarget(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const FGameplayTag EndpointConnectorTypeTag,
		const uint64 PairKey,
		const int32 WorldSeed,
		FLayoutWorldBindingContinuationFamilyCandidateTarget& OutTarget,
		FString& OutFailureReason)
	{
		OutTarget = FLayoutWorldBindingContinuationFamilyCandidateTarget();
		OutFailureReason.Reset();

		FLayoutWorldBindingContinuationFamilyTarget FamilyTarget;
		if (!TryResolveContinuationFamilyTarget(
			WorldBinding,
			FamilyType,
			EndpointConnectorTypeTag,
			FamilyTarget,
			OutFailureReason))
		{
			return false;
		}

		return TryResolveContinuationFamilyCandidateTargetFromFamilyTarget(
			WorldBinding,
			FamilyTarget,
			PairKey,
			WorldSeed,
			OutTarget,
			OutFailureReason);
	}

	bool TryResolveContinuationFamilyCandidateTargetFromFamilyTarget(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutWorldBindingContinuationFamilyTarget& FamilyTarget,
		const uint64 PairKey,
		const int32 WorldSeed,
		FLayoutWorldBindingContinuationFamilyCandidateTarget& OutTarget,
		FString& OutFailureReason)
	{
		OutTarget = FLayoutWorldBindingContinuationFamilyCandidateTarget();
		OutFailureReason.Reset();

		if (WorldBinding == nullptr
			|| FamilyTarget.Family == nullptr
			|| !WorldBinding->ContinuationFamilies.IsValidIndex(FamilyTarget.FamilyIndex)
			|| &WorldBinding->ContinuationFamilies[FamilyTarget.FamilyIndex] != FamilyTarget.Family)
		{
			OutFailureReason = FString::Printf(
				TEXT("worldBinding=%s familyIndex=%d familyId=%s."),
				*GetNameSafe(WorldBinding),
				FamilyTarget.FamilyIndex,
				FamilyTarget.Family != nullptr
					? *FamilyTarget.Family->FamilyId.ToString()
					: TEXT("<none>"));
			return false;
		}

		const int32 CandidateIndex = ChooseWeightedWorldBindingContinuationCandidateIndex(
			*FamilyTarget.Family,
			PairKey,
			WorldSeed);
		if (FamilyTarget.Family == nullptr
			|| !FamilyTarget.Family->Candidates.IsValidIndex(CandidateIndex))
		{
			OutFailureReason = FString::Printf(
				TEXT("worldBinding=%s familyId=%s familyIndex=%d pairKey=%llu worldSeed=%d candidateIndex=%d candidateCount=%d."),
				*GetNameSafe(WorldBinding),
				FamilyTarget.Family != nullptr ? *FamilyTarget.Family->FamilyId.ToString() : TEXT("<none>"),
				FamilyTarget.FamilyIndex,
				static_cast<unsigned long long>(PairKey),
				WorldSeed,
				CandidateIndex,
				FamilyTarget.Family != nullptr ? FamilyTarget.Family->Candidates.Num() : 0);
			return false;
		}

		OutTarget.FamilyIndex = FamilyTarget.FamilyIndex;
		OutTarget.Family = FamilyTarget.Family;
		OutTarget.CandidateIndex = CandidateIndex;
		OutTarget.Candidate = &FamilyTarget.Family->Candidates[CandidateIndex];
		return true;
	}

	bool TryBuildRuntimeViewFromWorldBinding(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const int32 CandidateIndex,
		const FName MatchingBiomeRowName,
		FLayoutWorldBindingRuntimeView& OutView,
		FString& OutFailureReason)
	{
		OutView = FLayoutWorldBindingRuntimeView();
		OutFailureReason.Reset();

		if (WorldBinding == nullptr)
		{
			OutFailureReason = TEXT("worldBinding=<none>.");
			return false;
		}

		if (!WorldBinding->BiomeRowNames.Contains(MatchingBiomeRowName))
		{
			OutFailureReason = FString::Printf(
				TEXT("worldBinding=%s matchingBiomeRow=%s."),
				*GetNameSafe(WorldBinding),
				*MatchingBiomeRowName.ToString());
			return false;
		}

		if (!WorldBinding->Candidates.IsValidIndex(CandidateIndex))
		{
			OutFailureReason = FString::Printf(
				TEXT("worldBinding=%s candidateIndex=%d candidateCount=%d."),
				*GetNameSafe(WorldBinding),
				CandidateIndex,
				WorldBinding->Candidates.Num());
			return false;
		}

		const FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates[CandidateIndex];
		const bool bBuiltView = TryBuildRuntimeViewBase(
			ELayoutWorldBindingPlacementKind::OrdinaryRoot,
			ResolveWorldBindingRuntimeBindingId(WorldBinding),
			Candidate.CandidateId,
			Candidate.LayoutProfile,
			ResolveEffectiveWorldBindingCandidateExportedConnectorTypeTags(Candidate),
			BuildOrdinaryRootRuntimePlacementPolicy(WorldBinding, Candidate),
			FLayoutWorldBindingContinuationPolicy(),
			WorldBinding->SolveBudget,
			WorldBinding->TemplatePlacementZOffsetBlocks,
			WorldBinding->BaseCellDimensionsBlocks,
			true,
			OutView,
			OutFailureReason);
		if (bBuiltView)
		{
			OutView.MatchingBiomeRowName = MatchingBiomeRowName;
		}
		return bBuiltView;
	}

	bool TryBuildRuntimeViewFromWorldBindingCandidateTarget(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutWorldBindingCandidateTarget& CandidateTarget,
		const FName MatchingBiomeRowName,
		FLayoutWorldBindingRuntimeView& OutView,
		FString& OutFailureReason)
	{
		OutView = FLayoutWorldBindingRuntimeView();
		OutFailureReason.Reset();

		if (WorldBinding == nullptr)
		{
			OutFailureReason = TEXT("worldBinding=<none>.");
			return false;
		}

		if (!WorldBinding->Candidates.IsValidIndex(CandidateTarget.CandidateIndex))
		{
			OutFailureReason = FString::Printf(
				TEXT("worldBinding=%s candidateIndex=%d candidateCount=%d."),
				*GetNameSafe(WorldBinding),
				CandidateTarget.CandidateIndex,
				WorldBinding->Candidates.Num());
			return false;
		}

		const FLayoutWorldBindingCandidate& SelectedCandidate =
			WorldBinding->Candidates[CandidateTarget.CandidateIndex];
		if (CandidateTarget.Candidate != &SelectedCandidate)
		{
			OutFailureReason = FString::Printf(
				TEXT("worldBinding=%s candidateIndex=%d candidateId=%s targetCandidateId=%s."),
				*GetNameSafe(WorldBinding),
				CandidateTarget.CandidateIndex,
				*SelectedCandidate.CandidateId.ToString(),
				CandidateTarget.Candidate != nullptr
					? *CandidateTarget.Candidate->CandidateId.ToString()
					: TEXT("<none>"));
			return false;
		}

		const bool bBuiltView = TryBuildRuntimeViewBase(
			ELayoutWorldBindingPlacementKind::OrdinaryRoot,
			ResolveWorldBindingRuntimeBindingId(WorldBinding),
			SelectedCandidate.CandidateId,
			SelectedCandidate.LayoutProfile,
			ResolveEffectiveWorldBindingCandidateExportedConnectorTypeTags(SelectedCandidate),
			BuildOrdinaryRootRuntimePlacementPolicy(WorldBinding, SelectedCandidate),
			FLayoutWorldBindingContinuationPolicy(),
			WorldBinding->SolveBudget,
			WorldBinding->TemplatePlacementZOffsetBlocks,
			WorldBinding->BaseCellDimensionsBlocks,
			true,
			OutView,
			OutFailureReason);
		if (bBuiltView)
		{
			OutView.MatchingBiomeRowName = MatchingBiomeRowName;
		}
		return bBuiltView;
	}

	bool TryBuildRuntimeViewFromWorldBindingCandidateId(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FName CandidateId,
		const FName MatchingBiomeRowName,
		FLayoutWorldBindingRuntimeView& OutView,
		FString& OutFailureReason)
	{
		const int32 CandidateIndex = FindWorldBindingCandidateIndexById(WorldBinding, CandidateId);
		if (CandidateIndex == INDEX_NONE)
		{
			OutView = FLayoutWorldBindingRuntimeView();
			OutFailureReason = FString::Printf(
				TEXT("worldBinding=%s candidateId=%s."),
				*GetNameSafe(WorldBinding),
				*CandidateId.ToString());
			return false;
		}

		return TryBuildRuntimeViewFromWorldBinding(
			WorldBinding,
			CandidateIndex,
			MatchingBiomeRowName,
			OutView,
			OutFailureReason);
	}

	bool TryBuildRuntimeViewFromWorldBindingContinuationFamily(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const int32 FamilyIndex,
		const int32 CandidateIndex,
		const int32 ResolvedEntryLevel,
		const FName MatchingBiomeRowName,
		FLayoutWorldBindingRuntimeView& OutView,
		FString& OutFailureReason)
	{
		OutView = FLayoutWorldBindingRuntimeView();
		OutFailureReason.Reset();

		if (WorldBinding == nullptr)
		{
			OutFailureReason = TEXT("worldBinding=<none>.");
			return false;
		}

		// Cross-biome cached continuation connectors do not preserve one shared
		// biome row name, but they can still rebuild through the authored family
		// entrypoint while the live binding/family/candidate identity is intact.
		if (!MatchingBiomeRowName.IsNone()
			&& !WorldBinding->BiomeRowNames.Contains(MatchingBiomeRowName))
		{
			OutFailureReason = FString::Printf(
				TEXT("worldBinding=%s matchingBiomeRow=%s."),
				*GetNameSafe(WorldBinding),
				*MatchingBiomeRowName.ToString());
			return false;
		}

		if (!WorldBinding->ContinuationFamilies.IsValidIndex(FamilyIndex))
		{
			OutFailureReason = FString::Printf(
				TEXT("worldBinding=%s familyIndex=%d familyCount=%d."),
				*GetNameSafe(WorldBinding),
				FamilyIndex,
				WorldBinding->ContinuationFamilies.Num());
			return false;
		}

		const FLayoutWorldBindingContinuationFamily& Family =
			WorldBinding->ContinuationFamilies[FamilyIndex];
		if (!Family.Candidates.IsValidIndex(CandidateIndex))
		{
			OutFailureReason = FString::Printf(
				TEXT("worldBinding=%s continuationFamilyId=%s candidateIndex=%d candidateCount=%d."),
				*GetNameSafe(WorldBinding),
				*Family.FamilyId.ToString(),
				CandidateIndex,
				Family.Candidates.Num());
			return false;
		}

		const FLayoutWorldBindingContinuationCandidate& Candidate = Family.Candidates[CandidateIndex];
		const FLayoutWorldBindingPlacementPolicy FamilyRuntimePlacementPolicy =
			BuildContinuationFamilyRuntimePlacementPolicy(WorldBinding, Family);
		const ELayoutWorldBindingPlacementKind FamilyRuntimePlacementKind =
			ResolveContinuationFamilyPlacementKind(Family.FamilyType);
		const FLayoutWorldBindingContinuationPolicy FamilyRuntimePolicy =
			BuildContinuationFamilyRuntimePolicy(WorldBinding, Family);
		if (!TryBuildRuntimeViewBase(
			FamilyRuntimePlacementKind,
			ResolveWorldBindingRuntimeBindingId(WorldBinding),
			Candidate.CandidateId,
			Candidate.LayoutProfile,
			FGameplayTagContainer(),
			FamilyRuntimePlacementPolicy,
			FamilyRuntimePolicy,
			Family.SolveBudget,
			WorldBinding->TemplatePlacementZOffsetBlocks,
			WorldBinding->BaseCellDimensionsBlocks,
			true,
			OutView,
			OutFailureReason))
		{
			return false;
		}

		OutView.ContinuationSelection.FamilyId = Family.FamilyId;
		OutView.ContinuationSelection.PlacementKind = FamilyRuntimePlacementKind;
		OutView.ContinuationSelection.ResolvedEntryLevel = ResolvedEntryLevel;
		return true;
	}

	bool TryBuildRuntimeViewFromPlannedSiteRecord(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FPlannedLayoutSiteRecord& PlannedSiteRecord,
		FLayoutWorldBindingRuntimeView& OutView,
		FString& OutFailureReason)
	{
		const FLayoutWorldBindingSiteFrontendSelection FrontendSelection =
			PlannedSiteRecord.GetWorldBindingFrontendSelection();
		if (!TryBuildRuntimeViewFromWorldBindingCandidateId(
			WorldBinding,
			FrontendSelection.WorldBindingCandidateId,
			FrontendSelection.BiomeRowName,
			OutView,
			OutFailureReason))
		{
			return false;
		}

		return TryApplyStoredContinuationSelection(
			WorldBinding,
			FrontendSelection.ResolvedContinuationSelection,
			OutView,
			OutFailureReason);
	}

	bool TryBuildRuntimeViewFromResolvedSiteRecord(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FResolvedLayoutSiteRecord& ResolvedSiteRecord,
		FLayoutWorldBindingRuntimeView& OutView,
		FString& OutFailureReason)
	{
		const FLayoutWorldBindingSiteFrontendSelection FrontendSelection =
			ResolvedSiteRecord.GetWorldBindingFrontendSelection();
		if (!TryBuildRuntimeViewFromWorldBindingCandidateId(
			WorldBinding,
			FrontendSelection.WorldBindingCandidateId,
			FrontendSelection.BiomeRowName,
			OutView,
			OutFailureReason))
		{
			return false;
		}

		return TryApplyStoredContinuationSelection(
			WorldBinding,
			FrontendSelection.ResolvedContinuationSelection,
			OutView,
			OutFailureReason);
	}

	bool TryBuildRuntimeViewFromResolvedConnectorRecord(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FResolvedLayoutConnectorRecord& ResolvedConnectorRecord,
		const FLayoutRootSolveBudgetSettings& SolveBudget,
		FLayoutWorldBindingRuntimeView& OutView,
		FString& OutFailureReason)
	{
		OutView = FLayoutWorldBindingRuntimeView();
		OutFailureReason.Reset();

		const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
			ResolvedConnectorRecord.GetResolvedConnectorFrontendSelection();
		const FLayoutResolvedConnectorSolveSourceSelection SolveSourceSelection =
			ResolvedConnectorRecord.GetResolvedConnectorSolveSourceSelection();
		const FLayoutRootPublicationMetadata PublicationMetadata =
			ResolvedConnectorRecord.GetRootPublicationMetadata();

		const bool bHasWorldBindingFrontendCarrier =
			HasResolvedConnectorWorldBindingFrontendCarrierForRuntimeView(ResolvedConnectorRecord);

		const FName NormalizedContinuationFamilyId =
			bHasWorldBindingFrontendCarrier
				&& !FrontendSelection.ResolvedContinuationSelection.FamilyId.IsNone()
				? FrontendSelection.ResolvedContinuationSelection.FamilyId
				: FrontendSelection.ContinuationFamilyId;
		const ELayoutWorldBindingPlacementKind NormalizedPlacementKind =
			bHasWorldBindingFrontendCarrier
				? ResolveResolvedConnectorRuntimePlacementKind(ResolvedConnectorRecord)
				: ELayoutWorldBindingPlacementKind::None;
		FLayoutWorldBindingPlacementPolicy LegacyDirectRootPlacementPolicy;
		const FLayoutWorldBindingPlacementPolicy NormalizedPlacementPolicy =
			bHasWorldBindingFrontendCarrier
				? ResolveResolvedConnectorRuntimePlacementPolicy(ResolvedConnectorRecord)
				: LegacyDirectRootPlacementPolicy;
		const int32 NormalizedResolvedEntryLevel =
			bHasWorldBindingFrontendCarrier
				? ResolveResolvedConnectorRuntimeEntryLevel(ResolvedConnectorRecord)
				: INDEX_NONE;
		const int32 FamilyIndex = FindWorldBindingContinuationFamilyIndexById(
			WorldBinding,
			NormalizedContinuationFamilyId);
		const int32 CandidateIndex = FindWorldBindingContinuationFamilyCandidateIndexById(
			WorldBinding,
			FamilyIndex,
			FrontendSelection.ContinuationFamilyCandidateId);
		if (bHasWorldBindingFrontendCarrier)
		{
			if (WorldBinding == nullptr)
			{
				OutFailureReason = FString::Printf(
					TEXT("resolved connector carrier requires one live world binding for worldBindingId=%s continuationFamilyId=%s candidateId=%s."),
					*FrontendSelection.WorldBindingId.ToString(),
					*NormalizedContinuationFamilyId.ToString(),
					*FrontendSelection.ContinuationFamilyCandidateId.ToString());
				return false;
			}

			if (NormalizedContinuationFamilyId.IsNone()
				|| FrontendSelection.ContinuationFamilyCandidateId.IsNone()
				|| NormalizedPlacementKind == ELayoutWorldBindingPlacementKind::None
				|| NormalizedResolvedEntryLevel == INDEX_NONE)
			{
				OutFailureReason = FString::Printf(
					TEXT("resolved connector carrier is missing normalized continuation data for worldBinding=%s continuationFamilyId=%s candidateId=%s placementKind=%d resolvedEntryLevel=%d."),
					*GetNameSafe(WorldBinding),
					*NormalizedContinuationFamilyId.ToString(),
					*FrontendSelection.ContinuationFamilyCandidateId.ToString(),
					static_cast<int32>(NormalizedPlacementKind),
					NormalizedResolvedEntryLevel);
				return false;
			}

			if (FamilyIndex == INDEX_NONE || CandidateIndex == INDEX_NONE)
			{
				OutFailureReason = FString::Printf(
					TEXT("resolved connector carrier references continuationFamilyId=%s candidateId=%s that cannot be rebuilt from worldBinding=%s."),
					*NormalizedContinuationFamilyId.ToString(),
					*FrontendSelection.ContinuationFamilyCandidateId.ToString(),
					*GetNameSafe(WorldBinding));
				return false;
			}

			return TryBuildRuntimeViewFromWorldBindingContinuationFamily(
				WorldBinding,
				FamilyIndex,
				CandidateIndex,
				NormalizedResolvedEntryLevel,
				FrontendSelection.BiomeRowName,
				OutView,
				OutFailureReason);
		}

			ULayoutProfileAsset* const LoadedProfile = SolveSourceSelection.LayoutProfile.LoadSynchronous();
			ULayoutRegionContentSetAsset* const LoadedContentSet =
				SolveSourceSelection.ContentSet.LoadSynchronous();
			const FIntVector DerivedContentSetSharedCellSizeInBlocks =
				LoadedContentSet != nullptr
					? LoadedContentSet->GetDerivedSharedCellSizeInBlocks()
					: FIntVector::ZeroValue;
			const bool bHasStoredSolveBudget =
				FrontendSelection.SolveBudget.MaxSolveDurationSeconds > 0.0f;
			const bool bHasStoredFrontendSharedCellSize =
				FrontendSelection.SharedCellSizeInBlocks != FIntVector::ZeroValue;
		const FIntVector SharedCellSizeInBlocks =
			bHasStoredFrontendSharedCellSize
					? FrontendSelection.SharedCellSizeInBlocks
					: (WorldBinding != nullptr
						&& WorldBinding->BaseCellDimensionsBlocks != FIntVector::ZeroValue
						? WorldBinding->BaseCellDimensionsBlocks
						: (DerivedContentSetSharedCellSizeInBlocks != FIntVector::ZeroValue
							? DerivedContentSetSharedCellSizeInBlocks
								: FIntVector::ZeroValue));
		const int32 TemplatePlacementZOffsetBlocks =
			bHasStoredFrontendSharedCellSize
				? FrontendSelection.TemplatePlacementZOffsetBlocks
				: (WorldBinding != nullptr
					? WorldBinding->TemplatePlacementZOffsetBlocks
					: 0);
		if (!TryBuildRuntimeViewBase(
			NormalizedPlacementKind,
				NormalizedPlacementKind != ELayoutWorldBindingPlacementKind::None && !FrontendSelection.WorldBindingId.IsNone()
				? FrontendSelection.WorldBindingId
				: (bHasWorldBindingFrontendCarrier
					? ResolveWorldBindingRuntimeBindingId(WorldBinding)
					: NAME_None),
				NormalizedPlacementKind != ELayoutWorldBindingPlacementKind::None && !FrontendSelection.ContinuationFamilyCandidateId.IsNone()
				? FrontendSelection.ContinuationFamilyCandidateId
				: (bHasWorldBindingFrontendCarrier
					? FName(*PublicationMetadata.RootCandidateId.ToString(), FNAME_Find)
					: NAME_None),
			LoadedProfile,
			FGameplayTagContainer(),
			NormalizedPlacementPolicy,
				bHasWorldBindingFrontendCarrier ? FrontendSelection.ContinuationPolicy
				: FLayoutWorldBindingContinuationPolicy(),
			bHasWorldBindingFrontendCarrier && bHasStoredSolveBudget
				? FrontendSelection.SolveBudget
				: SolveBudget,
			TemplatePlacementZOffsetBlocks,
			SharedCellSizeInBlocks,
			true,
			OutView,
			OutFailureReason))
		{
			return false;
		}

		OutView.ContinuationSelection =
				bHasWorldBindingFrontendCarrier ? FrontendSelection.ResolvedContinuationSelection
				: FLayoutResolvedWorldBindingContinuationSelection();
		if (bHasWorldBindingFrontendCarrier
			&& OutView.ContinuationSelection.FamilyId.IsNone()
			&& !NormalizedContinuationFamilyId.IsNone())
		{
			OutView.ContinuationSelection.FamilyId =
				NormalizedContinuationFamilyId;
		}

		if (bHasWorldBindingFrontendCarrier
			&& OutView.ContinuationSelection.PlacementKind == ELayoutWorldBindingPlacementKind::None
			&& NormalizedPlacementKind != ELayoutWorldBindingPlacementKind::None)
		{
			OutView.ContinuationSelection.PlacementKind =
				NormalizedPlacementKind;
		}

		if (bHasWorldBindingFrontendCarrier
			&& OutView.ContinuationSelection.ResolvedEntryLevel == INDEX_NONE
			&& NormalizedResolvedEntryLevel != INDEX_NONE)
		{
			OutView.ContinuationSelection.ResolvedEntryLevel =
				NormalizedResolvedEntryLevel;
		}

		// Once the live authored continuation-family entrypoint could not be rebuilt,
		// keep the stored connector-facing carrier intact. Legacy empty-frontend
		// records still rebuild through this shared seam, but only as non-world-
		// facing direct-root requests with caller-owned solve budget.
		return true;
	}

	bool TryBuildExplicitToolRuntimeViewFromWorldBindingProfile(
		const ULayoutWorldBindingAsset* const WorldBinding,
		ULayoutProfileAsset* const LayoutProfile,
		FLayoutWorldBindingRuntimeView& OutView,
		FLayoutWorldBindingSiteFrontendSelection& OutFrontendSelection,
		FString& OutFailureReason)
	{
		OutView = FLayoutWorldBindingRuntimeView();
		OutFrontendSelection = FLayoutWorldBindingSiteFrontendSelection();
		OutFailureReason.Reset();

		if (WorldBinding == nullptr || LayoutProfile == nullptr)
		{
			OutFailureReason = FString::Printf(
				TEXT("worldBinding=%s profile=%s."),
				*GetNameSafe(WorldBinding),
				*GetNameSafe(LayoutProfile));
			return false;
		}

		TArray<int32> MatchingOrdinaryCandidateIndexes;
		for (int32 CandidateIndex = 0; CandidateIndex < WorldBinding->Candidates.Num(); ++CandidateIndex)
		{
			if (WorldBinding->Candidates[CandidateIndex].LayoutProfile == LayoutProfile)
			{
				MatchingOrdinaryCandidateIndexes.Add(CandidateIndex);
			}
		}

		struct FContinuationMatch
		{
			int32 FamilyIndex = INDEX_NONE;
			int32 CandidateIndex = INDEX_NONE;
		};
		TArray<FContinuationMatch> MatchingContinuationCandidates;
		for (int32 FamilyIndex = 0; FamilyIndex < WorldBinding->ContinuationFamilies.Num(); ++FamilyIndex)
		{
			const FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies[FamilyIndex];
			for (int32 CandidateIndex = 0; CandidateIndex < Family.Candidates.Num(); ++CandidateIndex)
			{
				if (Family.Candidates[CandidateIndex].LayoutProfile == LayoutProfile)
				{
					FContinuationMatch& Match = MatchingContinuationCandidates.AddDefaulted_GetRef();
					Match.FamilyIndex = FamilyIndex;
					Match.CandidateIndex = CandidateIndex;
				}
			}
		}

		const int32 TotalMatchCount = MatchingOrdinaryCandidateIndexes.Num() + MatchingContinuationCandidates.Num();
		if (TotalMatchCount == 0)
		{
			OutFailureReason = FString::Printf(
				TEXT("Selected profile '%s' is not referenced by world binding '%s'. For standalone/root preview, add exactly one reference in Candidates[].LayoutProfile. For continuation preview, add exactly one reference in ContinuationFamilies[].Candidates[].LayoutProfile. Child-region content entries do not make profiles directly previewable. ordinaryCandidates=0 continuationCandidates=0."),
				*GetNameSafe(LayoutProfile),
				*GetNameSafe(WorldBinding));
			return false;
		}

		if (TotalMatchCount > 1)
		{
			OutFailureReason = FString::Printf(
				TEXT("worldBinding=%s profile=%s ordinaryCandidates=%d continuationCandidates=%d. Layout Generator requires one unambiguous world-binding candidate or continuation-family candidate for the selected profile."),
				*GetNameSafe(WorldBinding),
				*GetNameSafe(LayoutProfile),
				MatchingOrdinaryCandidateIndexes.Num(),
				MatchingContinuationCandidates.Num());
			return false;
		}

		if (!MatchingOrdinaryCandidateIndexes.IsEmpty())
		{
			const int32 CandidateIndex = MatchingOrdinaryCandidateIndexes[0];
			const FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates[CandidateIndex];
			if (!TryBuildRuntimeViewBase(
				ELayoutWorldBindingPlacementKind::OrdinaryRoot,
				ResolveWorldBindingRuntimeBindingId(WorldBinding),
				Candidate.CandidateId,
				Candidate.LayoutProfile,
				ResolveEffectiveWorldBindingCandidateExportedConnectorTypeTags(Candidate),
				BuildOrdinaryRootRuntimePlacementPolicy(WorldBinding, Candidate),
				FLayoutWorldBindingContinuationPolicy(),
				WorldBinding->SolveBudget,
				WorldBinding->TemplatePlacementZOffsetBlocks,
				WorldBinding->BaseCellDimensionsBlocks,
				true,
				OutView,
				OutFailureReason))
			{
				return false;
			}

			OutFrontendSelection.WorldBindingId = ResolveWorldBindingRuntimeBindingId(WorldBinding);
			OutFrontendSelection.WorldBindingCandidateId = Candidate.CandidateId;

			// Carry biome row into the runtime view so the terrain adapter can validate terrain evidence.
			// bUseAnyActiveBiomeSurface already ensures the solver can place without biome-row filtering.
			OutView.MatchingBiomeRowName = WorldBinding->BiomeRowNames.Num() > 0
				? WorldBinding->BiomeRowNames[0]
				: NAME_None;

			return true;
		}

		const FContinuationMatch Match = MatchingContinuationCandidates[0];
		const FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies[Match.FamilyIndex];
		const FLayoutWorldBindingContinuationCandidate& Candidate = Family.Candidates[Match.CandidateIndex];
		if (Candidate.LayoutProfile == nullptr || Candidate.LayoutProfile->ContinuationEntryLevel == INDEX_NONE)
		{
			OutFailureReason = FString::Printf(
				TEXT("worldBinding=%s continuationFamilyId=%s candidateId=%s profile=%s continuationEntryLevel=%d. Binding-aware explicit continuation previews require one explicit continuation entry level on the selected profile."),
				*GetNameSafe(WorldBinding),
				*Family.FamilyId.ToString(),
				*Candidate.CandidateId.ToString(),
				*GetNameSafe(Candidate.LayoutProfile),
				Candidate.LayoutProfile != nullptr ? Candidate.LayoutProfile->ContinuationEntryLevel : INDEX_NONE);
			return false;
		}

		const FLayoutWorldBindingPlacementPolicy FamilyRuntimePlacementPolicy = BuildContinuationFamilyRuntimePlacementPolicy(WorldBinding, Family);
		const ELayoutWorldBindingPlacementKind FamilyRuntimePlacementKind = ResolveContinuationFamilyPlacementKind(Family.FamilyType);
		const FLayoutWorldBindingContinuationPolicy FamilyRuntimePolicy = BuildContinuationFamilyRuntimePolicy(WorldBinding, Family);
		if (!TryBuildRuntimeViewBase(
			FamilyRuntimePlacementKind,
			ResolveWorldBindingRuntimeBindingId(WorldBinding),
			Candidate.CandidateId,
			Candidate.LayoutProfile,
			FGameplayTagContainer(),
			FamilyRuntimePlacementPolicy,
			FamilyRuntimePolicy,
			Family.SolveBudget,
			WorldBinding->TemplatePlacementZOffsetBlocks,
			WorldBinding->BaseCellDimensionsBlocks,
			true,
			OutView,
			OutFailureReason))
		{
			return false;
		}

		OutView.ContinuationSelection.FamilyId = Family.FamilyId;
		OutView.ContinuationSelection.PlacementKind = FamilyRuntimePlacementKind;
		OutView.ContinuationSelection.ResolvedEntryLevel = Candidate.LayoutProfile->ContinuationEntryLevel;
		OutFrontendSelection.WorldBindingId = ResolveWorldBindingRuntimeBindingId(WorldBinding);
		OutFrontendSelection.WorldBindingCandidateId = Candidate.CandidateId;
		OutFrontendSelection.ResolvedContinuationSelection = OutView.ContinuationSelection;

		// Carry biome row into the runtime view so the terrain adapter can validate terrain evidence.
		OutView.MatchingBiomeRowName = WorldBinding->BiomeRowNames.Num() > 0
			? WorldBinding->BiomeRowNames[0]
			: NAME_None;

		return true;
	}
}
