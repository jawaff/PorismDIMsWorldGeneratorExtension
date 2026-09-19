// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutWorldBindingAsset.h"

#include "ChunkWorldStructs/ChunkStructureTemplate.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Diagnostics/LayoutEditorMessageLog.h"
#include "Layout/Types/LayoutGameplayTags.h"

namespace
{
	constexpr int32 MinSupportedSharedCellDimensionBlocks = 1;
	constexpr int32 MaxSupportedSharedCellDimensionBlocks = 255;

	bool IsWorldBindingSharedCellSizeValid(const FIntVector& SharedCellSizeInBlocks)
	{
		return SharedCellSizeInBlocks.X > 0
			&& SharedCellSizeInBlocks.Y > 0
			&& SharedCellSizeInBlocks.Z > 0;
	}

	bool IsWorldBindingSharedCellSizeWithinSupportedBounds(const FIntVector& SharedCellSizeInBlocks)
	{
		return SharedCellSizeInBlocks.X >= MinSupportedSharedCellDimensionBlocks
			&& SharedCellSizeInBlocks.X <= MaxSupportedSharedCellDimensionBlocks
			&& SharedCellSizeInBlocks.Y >= MinSupportedSharedCellDimensionBlocks
			&& SharedCellSizeInBlocks.Y <= MaxSupportedSharedCellDimensionBlocks
			&& SharedCellSizeInBlocks.Z >= MinSupportedSharedCellDimensionBlocks
			&& SharedCellSizeInBlocks.Z <= MaxSupportedSharedCellDimensionBlocks;
	}

	bool IsAuthoredRootSolveBudgetValid(const FLayoutRootSolveBudgetSettings& SolveBudget)
	{
		return SolveBudget.MaxSolveDurationSeconds > 0.0f;
	}

	void ValidateTerrainTransitionPolicy(
		const FLayoutWorldBindingTerrainTransitionPolicy& TerrainTransition,
		const FString& ContextLabel,
		const ULayoutWorldBindingAsset* const WorldBinding,
		FLayoutValidationResult& Result)
	{
		if (TerrainTransition.bAllowPerimeterRampTransition
			&& TerrainTransition.MaxFoundationDepth <= 0)
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding %s enables perimeter ramp transitions but authors MaxFoundationDepth=%d.\nWorldBinding: %s\nFix: Author a positive foundation depth; perimeter ramp transitions reuse the same depth budget as shallow foundation fill."),
				*ContextLabel,
				TerrainTransition.MaxFoundationDepth,
				*WorldBinding->GetName()));
		}
	}

	FIntVector ResolveLoadedLeafModuleTemplateDimensionsBlocks(const ULayoutModuleAsset* const Module)
	{
		if (Module == nullptr)
		{
			return FIntVector::ZeroValue;
		}

		const UChunkStructureTemplate* const LoadedTemplate = Module->Template.LoadSynchronous();
		return LoadedTemplate != nullptr ? LoadedTemplate->SizeInBlocks : FIntVector::ZeroValue;
	}

	FIntVector ResolveModuleContentSettingsSharedCellSizeInBlocks(const FLayoutModuleContentSettings& Settings)
	{
		if (Settings.Module != nullptr)
		{
			return ResolveLoadedLeafModuleTemplateDimensionsBlocks(Settings.Module);
		}

		if (Settings.CompositeModule != nullptr)
		{
			return Settings.CompositeModule->GetSharedCellSizeInBlocks();
		}

		return FIntVector::ZeroValue;
	}

	void AppendWorldBindingValidationResult(
		FDataValidationContext& Context,
		const FLayoutValidationResult& ValidationResult)
	{
		for (const FLayoutValidationMessage& Message : ValidationResult.Messages)
		{
			if (Message.Severity == ELayoutValidationSeverity::Error)
			{
				Context.AddError(FText::FromString(Message.Message));
			}
			else
			{
				Context.AddWarning(FText::FromString(Message.Message));
			}
		}
	}

	FIntVector ResolveCandidateSharedCellSizeInBlocks(const FLayoutWorldBindingCandidate& Candidate)
	{
		if (Candidate.LayoutProfile == nullptr || Candidate.LayoutProfile->ContentSet == nullptr)
		{
			return FIntVector::ZeroValue;
		}

		for (const FLayoutRegionContentEntry& Entry : Candidate.LayoutProfile->ContentSet->Entries)
		{
			if (Entry.ContentKind != ELayoutRegionContentKind::Module)
			{
				continue;
			}

			const FIntVector ResolvedSharedCellSizeInBlocks =
				ResolveModuleContentSettingsSharedCellSizeInBlocks(Entry.ModuleSettings);
			if (IsWorldBindingSharedCellSizeValid(ResolvedSharedCellSizeInBlocks))
			{
				return ResolvedSharedCellSizeInBlocks;
			}
		}

		return FIntVector::ZeroValue;
	}

	FGameplayTagContainer ResolveEffectiveCandidateExportedConnectorTypeTags(
		const FLayoutWorldBindingCandidate& Candidate)
	{
		return Candidate.ExportedConnectorTypeTags;
	}

	bool CandidateHasDirectModuleContent(const FLayoutWorldBindingCandidate& Candidate)
	{
		if (Candidate.LayoutProfile == nullptr || Candidate.LayoutProfile->ContentSet == nullptr)
		{
			return false;
		}

		return Candidate.LayoutProfile->ContentSet->Entries.ContainsByPredicate(
			[](const FLayoutRegionContentEntry& Entry)
			{
				return Entry.ContentKind == ELayoutRegionContentKind::Module;
			});
	}

	void ValidateLeafModuleAgainstBindingCellDimensions(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutWorldBindingCandidate& Candidate,
		const int32 CandidateIndex,
		const TCHAR* CandidateContext,
		const FString& SourceDescription,
		const ULayoutModuleAsset* const Module,
		FLayoutValidationResult& Result)
	{
		if (WorldBinding == nullptr
			|| Module == nullptr
			|| !IsWorldBindingSharedCellSizeValid(WorldBinding->BaseCellDimensionsBlocks))
		{
			return;
		}

		const FIntVector EffectiveCellSizeInBlocks = ResolveLoadedLeafModuleTemplateDimensionsBlocks(Module);
		if (!IsWorldBindingSharedCellSizeValid(EffectiveCellSizeInBlocks))
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding %s candidate %d could not resolve a positive template SizeInBlocks value from the referenced module template.\nWorldBinding: %s\nProfile: %s\nSource: %s\nModule: %s\nResolved TemplateDimensionsBlocks: %s\nFix: Ensure the referenced template asset loads correctly and authors positive X, Y, and Z SizeInBlocks values. A resolved 0,0,0 template size means the validator could not read a usable size from the real template asset."),
				CandidateContext,
				CandidateIndex,
				*WorldBinding->GetName(),
				Candidate.LayoutProfile != nullptr ? *Candidate.LayoutProfile->GetName() : TEXT("<none>"),
				*SourceDescription,
				*Module->GetName(),
				*EffectiveCellSizeInBlocks.ToString()));
			return;
		}

		if (EffectiveCellSizeInBlocks == WorldBinding->BaseCellDimensionsBlocks)
		{
			return;
		}

		Result.AddError(FString::Printf(
			TEXT("Layout world binding %s candidate %d references a leaf module whose template dimensions violate the binding-owned cell lattice.\nWorldBinding: %s\nProfile: %s\nSource: %s\nModule: %s\nBinding BaseCellDimensionsBlocks: %s\nModule TemplateDimensionsBlocks: %s\nFix: Resize the referenced Porism template to the binding cell dimensions, or move the candidate to a world binding whose lattice matches the module template."),
			CandidateContext,
			CandidateIndex,
			*WorldBinding->GetName(),
			Candidate.LayoutProfile != nullptr ? *Candidate.LayoutProfile->GetName() : TEXT("<none>"),
			*SourceDescription,
			*Module->GetName(),
			*WorldBinding->BaseCellDimensionsBlocks.ToString(),
			*EffectiveCellSizeInBlocks.ToString()));
	}

	bool ValidateCandidateDirectContentAgainstBindingCellDimensions(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutWorldBindingCandidate& Candidate,
		const int32 CandidateIndex,
		const TCHAR* CandidateContext,
		FLayoutValidationResult& Result)
	{
		if (WorldBinding == nullptr || !IsWorldBindingSharedCellSizeValid(WorldBinding->BaseCellDimensionsBlocks))
		{
			return false;
		}

		bool bValidatedDirectContent = false;
		if (Candidate.LayoutProfile != nullptr && Candidate.LayoutProfile->ContentSet != nullptr)
		{
			for (int32 EntryIndex = 0; EntryIndex < Candidate.LayoutProfile->ContentSet->Entries.Num(); ++EntryIndex)
			{
				const FLayoutRegionContentEntry& Entry = Candidate.LayoutProfile->ContentSet->Entries[EntryIndex];
				if (Entry.ContentKind != ELayoutRegionContentKind::Module)
				{
					continue;
				}

				if (Entry.ModuleSettings.Module != nullptr)
				{
					bValidatedDirectContent = true;
					ValidateLeafModuleAgainstBindingCellDimensions(
						WorldBinding,
						Candidate,
						CandidateIndex,
						CandidateContext,
						FString::Printf(TEXT("ContentSet EntryId=%s"), Entry.EntryId.IsNone() ? TEXT("<none>") : *Entry.EntryId.ToString()),
						Entry.ModuleSettings.Module,
						Result);
					continue;
				}

				if (Entry.ModuleSettings.CompositeModule != nullptr)
				{
					bValidatedDirectContent = true;
					const FLayoutValidationResult CompositeValidation =
						Entry.ModuleSettings.CompositeModule->ValidateCompositeModuleAgainstSharedCellSize(WorldBinding->BaseCellDimensionsBlocks);
					Result.Messages.Append(CompositeValidation.Messages);
				}
			}

			return bValidatedDirectContent;
		}

		return bValidatedDirectContent;
	}

	bool CandidateHasExplicitContinuationEntryLevel(const FLayoutWorldBindingCandidate& Candidate)
	{
		return Candidate.LayoutProfile != nullptr
			&& Candidate.LayoutProfile->ContinuationEntryLevel != INDEX_NONE;
	}

	bool CandidateHasExplicitContinuationEntryLevel(const FLayoutWorldBindingContinuationCandidate& Candidate)
	{
		return Candidate.LayoutProfile != nullptr
			&& Candidate.LayoutProfile->ContinuationEntryLevel != INDEX_NONE;
	}

	const TCHAR* ResolveContinuationFamilyTypeLabel(
		const ELayoutWorldBindingContinuationFamilyType FamilyType)
	{
		switch (FamilyType)
		{
		case ELayoutWorldBindingContinuationFamilyType::SurfacePath:
			return TEXT("SurfacePath");
		case ELayoutWorldBindingContinuationFamilyType::BridgeContinuation:
			return TEXT("BridgeContinuation");
		case ELayoutWorldBindingContinuationFamilyType::TunnelContinuation:
			return TEXT("TunnelContinuation");
		default:
			return TEXT("Unknown");
		}
	}

	void ValidateWorldBindingCandidate(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutWorldBindingCandidate& Candidate,
		const int32 CandidateIndex,
		const TCHAR* CandidateContext,
		FLayoutValidationResult& Result)
	{
		if (Candidate.CandidateId.IsNone())
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding %s candidate %d is missing CandidateId.\nWorldBinding: %s\nFix: Author an explicit candidate id so runtime planning and solve requests do not depend on legacy row-name identity."),
				CandidateContext,
				CandidateIndex,
				*WorldBinding->GetName()));
		}

		if (Candidate.LayoutProfile == nullptr)
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding %s candidate %d does not reference a layout profile.\nWorldBinding: %s"),
				CandidateContext,
				CandidateIndex,
				*WorldBinding->GetName()));
			return;
		}

		if (Candidate.Weight <= 0)
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding %s candidate %d uses weight %d. Candidate weights must be greater than zero.\nWorldBinding: %s\nProfile: %s"),
				CandidateContext,
				CandidateIndex,
				Candidate.Weight,
				*WorldBinding->GetName(),
				*Candidate.LayoutProfile->GetName()));
		}

		if (Candidate.bOverrideTerrainTransitionPolicy)
		{
			ValidateTerrainTransitionPolicy(
				Candidate.TerrainTransitionPolicyOverride,
				FString::Printf(TEXT("ordinary-root candidate %d terrain-transition override"), CandidateIndex),
				WorldBinding,
				Result);
		}

		if (Candidate.LayoutProfile->ContentSet == nullptr)
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding %s candidate %d references a profile that does not own a content set.\nWorldBinding: %s\nProfile: %s\nFix: Assign a content set on the selected profile."),
				CandidateContext,
				CandidateIndex,
				*WorldBinding->GetName(),
				*Candidate.LayoutProfile->GetName()));
			return;
		}

		const bool bValidatedDirectContent =
			ValidateCandidateDirectContentAgainstBindingCellDimensions(
				WorldBinding,
				Candidate,
				CandidateIndex,
				CandidateContext,
				Result);
		if (!bValidatedDirectContent && CandidateHasDirectModuleContent(Candidate))
		{
			const FIntVector CandidateSharedCellSizeInBlocks = ResolveCandidateSharedCellSizeInBlocks(Candidate);
			if (CandidateSharedCellSizeInBlocks == FIntVector::ZeroValue)
			{
				Result.AddError(FString::Printf(
					TEXT("Layout world binding %s candidate %d could not resolve a positive shared cell size from the profile-owned content set.\nWorldBinding: %s\nProfile: %s\nFix: Ensure at least one referenced module or composite resolves positive template/cell dimensions directly from the profile-owned content set. If the resolved size falls back to 0,0,0, the validator could not read a usable size from the real referenced template/module assets."),
					CandidateContext,
					CandidateIndex,
					*WorldBinding->GetName(),
					*Candidate.LayoutProfile->GetName()));
				return;
			}

			if (IsWorldBindingSharedCellSizeValid(WorldBinding->BaseCellDimensionsBlocks)
				&& CandidateSharedCellSizeInBlocks != WorldBinding->BaseCellDimensionsBlocks)
			{
				Result.AddError(FString::Printf(
					TEXT("Layout world binding %s candidate %d violates the binding-owned shared cell size contract.\nWorldBinding: %s\nProfile: %s\nBinding BaseCellDimensionsBlocks: %s\nCandidate SharedCellSizeInBlocks: %s\nFix: Keep all candidates on the same shared one-cell dimensions, or split them into separate world bindings."),
					CandidateContext,
					CandidateIndex,
					*WorldBinding->GetName(),
					*Candidate.LayoutProfile->GetName(),
					*WorldBinding->BaseCellDimensionsBlocks.ToString(),
					*CandidateSharedCellSizeInBlocks.ToString()));
			}
		}
	}

	void ValidateWorldBindingContinuationCandidate(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutWorldBindingContinuationCandidate& Candidate,
		const int32 CandidateIndex,
		const TCHAR* CandidateContext,
		FLayoutValidationResult& Result)
	{
		if (Candidate.CandidateId.IsNone())
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding %s candidate %d is missing CandidateId.\nWorldBinding: %s\nFix: Author an explicit continuation candidate id so runtime continuation planning and request rebuilding do not depend on legacy row-name identity."),
				CandidateContext,
				CandidateIndex,
				*WorldBinding->GetName()));
		}

		if (Candidate.LayoutProfile == nullptr)
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding %s candidate %d does not reference a layout profile.\nWorldBinding: %s"),
				CandidateContext,
				CandidateIndex,
				*WorldBinding->GetName()));
			return;
		}

		if (Candidate.Weight <= 0)
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding %s candidate %d uses weight %d. Candidate weights must be greater than zero.\nWorldBinding: %s\nProfile: %s"),
				CandidateContext,
				CandidateIndex,
				Candidate.Weight,
				*WorldBinding->GetName(),
				*Candidate.LayoutProfile->GetName()));
		}

		if (Candidate.LayoutProfile->ContentSet == nullptr)
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding %s candidate %d references a profile that does not own a content set.\nWorldBinding: %s\nProfile: %s\nFix: Assign a content set on the selected profile. Continuation candidates require content sets on the final world-binding path."),
				CandidateContext,
				CandidateIndex,
				*WorldBinding->GetName(),
				*Candidate.LayoutProfile->GetName()));
			return;
		}

		if (!Candidate.LayoutProfile->bRequireAllTraversalChannelsReachable)
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding %s candidate %d references a continuation profile that does not require all traversal channels to be reachable.\nWorldBinding: %s\nProfile: %s\nFix: Enable Require All Traversal Channels Reachable on continuation profiles used by surface path, bridge, or tunnel families."),
				CandidateContext,
				CandidateIndex,
				*WorldBinding->GetName(),
				*Candidate.LayoutProfile->GetName()));
		}

		const FLayoutWorldBindingCandidate RootLikeCandidate = [](
			const FLayoutWorldBindingContinuationCandidate& ContinuationCandidate)
		{
			FLayoutWorldBindingCandidate CandidateProxy;
			CandidateProxy.CandidateId = ContinuationCandidate.CandidateId;
			CandidateProxy.LayoutProfile = ContinuationCandidate.LayoutProfile;
			CandidateProxy.Weight = ContinuationCandidate.Weight;
			return CandidateProxy;
		}(Candidate);

		const bool bValidatedDirectContent =
			ValidateCandidateDirectContentAgainstBindingCellDimensions(
				WorldBinding,
				RootLikeCandidate,
				CandidateIndex,
				CandidateContext,
				Result);
		if (!bValidatedDirectContent && CandidateHasDirectModuleContent(RootLikeCandidate))
		{
			const FIntVector CandidateSharedCellSizeInBlocks =
				ResolveCandidateSharedCellSizeInBlocks(RootLikeCandidate);
			if (CandidateSharedCellSizeInBlocks == FIntVector::ZeroValue)
			{
				Result.AddError(FString::Printf(
					TEXT("Layout world binding %s candidate %d could not resolve a positive shared cell size from the profile-owned content set.\nWorldBinding: %s\nProfile: %s\nFix: Ensure at least one referenced module or composite resolves positive template/cell dimensions directly from the profile-owned content set. If the resolved size falls back to 0,0,0, the validator could not read a usable size from the real referenced template/module assets."),
					CandidateContext,
					CandidateIndex,
					*WorldBinding->GetName(),
					*Candidate.LayoutProfile->GetName()));
				return;
			}

			if (IsWorldBindingSharedCellSizeValid(WorldBinding->BaseCellDimensionsBlocks)
				&& CandidateSharedCellSizeInBlocks != WorldBinding->BaseCellDimensionsBlocks)
			{
				Result.AddError(FString::Printf(
					TEXT("Layout world binding %s candidate %d violates the binding-owned shared cell size contract.\nWorldBinding: %s\nProfile: %s\nBinding BaseCellDimensionsBlocks: %s\nCandidate SharedCellSizeInBlocks: %s\nFix: Keep all candidates on the same shared one-cell dimensions, or split them into separate world bindings."),
					CandidateContext,
					CandidateIndex,
					*WorldBinding->GetName(),
					*Candidate.LayoutProfile->GetName(),
					*WorldBinding->BaseCellDimensionsBlocks.ToString(),
					*CandidateSharedCellSizeInBlocks.ToString()));
			}
		}
	}
}

FLayoutValidationResult ULayoutWorldBindingAsset::ValidateWorldBinding() const
{
	FLayoutValidationResult Result;

	if (!IsWorldBindingSharedCellSizeValid(BaseCellDimensionsBlocks))
	{
		Result.AddError(FString::Printf(
			TEXT("Layout world binding authors an invalid shared cell size.\nWorldBinding: %s\nBaseCellDimensionsBlocks: %s\nFix: Author positive X, Y, and Z base cell dimensions on the world binding."),
			*GetName(),
			*BaseCellDimensionsBlocks.ToString()));
	}
	else if (!IsWorldBindingSharedCellSizeWithinSupportedBounds(BaseCellDimensionsBlocks))
	{
		Result.AddError(FString::Printf(
			TEXT("Layout world binding authors shared cell dimensions outside the supported compact local-block range.\nWorldBinding: %s\nBaseCellDimensionsBlocks: %s\nSupported Range: [%d, %d] on X, Y, and Z\nFix: Author base cell dimensions that fit compact uint8 local-block evidence coordinates."),
			*GetName(),
			*BaseCellDimensionsBlocks.ToString(),
			MinSupportedSharedCellDimensionBlocks,
			MaxSupportedSharedCellDimensionBlocks));
	}

	if (!FMath::IsFinite(OccupancyProbability) || OccupancyProbability < 0.0f || OccupancyProbability > 1.0f)
	{
		Result.AddError(FString::Printf(TEXT("Layout world binding '%s' requires finite OccupancyProbability in [0, 1]."), *GetName()));
	}

	if (MinimumRootGapCells < 0)
	{
		Result.AddError(FString::Printf(
			TEXT("Layout world binding '%s' requires MinimumRootGapCells >= 0; zero retains overlap protection without extra clearance."), *GetName()));
	}

	if (!IsAuthoredRootSolveBudgetValid(SolveBudget))
	{
		Result.AddError(FString::Printf(
			TEXT("Layout world binding authors ordinary-root SolveBudget.MaxSolveDurationSeconds=%.3f, but the active world-binding ordinary-root path requires a positive root-attempt timeout.\nWorldBinding: %s\nFix: Author a positive ordinary-root solve budget on the world binding so one candidate site attempt can time out cleanly instead of running unbounded."),
			SolveBudget.MaxSolveDurationSeconds,
			*GetName()));
	}

	ValidateTerrainTransitionPolicy(
		DefaultPlacementPolicy.TerrainTransition,
		TEXT("default placement policy"),
		this,
		Result);

	if (BiomeRowNames.IsEmpty())
	{
		Result.AddError(FString::Printf(
			TEXT("Layout world binding must declare at least one compatible biome row.\nWorldBinding: %s"),
			*GetName()));
	}
	else
	{
		for (int32 BiomeIndex = 0; BiomeIndex < BiomeRowNames.Num(); ++BiomeIndex)
		{
			if (BiomeRowNames[BiomeIndex] == NAME_None)
			{
				Result.AddError(FString::Printf(
					TEXT("Layout world binding contains an empty biome row entry at index %d.\nWorldBinding: %s"),
					BiomeIndex,
					*GetName()));
			}
		}
	}

	if (Candidates.IsEmpty())
	{
		Result.AddError(FString::Printf(
			TEXT("Layout world binding must contain at least one weighted root candidate.\nWorldBinding: %s"),
			*GetName()));
		return Result;
	}

	for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
	{
		const FLayoutWorldBindingCandidate& Candidate = Candidates[CandidateIndex];
		ValidateWorldBindingCandidate(this, Candidate, CandidateIndex, TEXT("ordinary-root"), Result);
	}

	for (int32 FamilyIndex = 0; FamilyIndex < ContinuationFamilies.Num(); ++FamilyIndex)
	{
		const FLayoutWorldBindingContinuationFamily& Family = ContinuationFamilies[FamilyIndex];
		if (Family.FamilyId.IsNone())
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding continuation family %d is missing FamilyId.\nWorldBinding: %s"),
				FamilyIndex,
				*GetName()));
		}

		if (Family.Candidates.IsEmpty())
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding continuation family %s does not contain any candidates.\nWorldBinding: %s"),
				*Family.FamilyId.ToString(),
				*GetName()));
		}

		if (Family.MaxConnectionsPerSite <= 0)
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding continuation family %s uses MaxConnectionsPerSite=%d. Continuation families must allow at least one connection per site.\nWorldBinding: %s"),
				*Family.FamilyId.ToString(),
				Family.MaxConnectionsPerSite,
				*GetName()));
		}

		if (Family.MaxConnectionDistanceInCells <= 0)
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding continuation family %s uses MaxConnectionDistanceInCells=%d. Continuation families must author a positive connection distance limit.\nWorldBinding: %s"),
				*Family.FamilyId.ToString(),
				Family.MaxConnectionDistanceInCells,
				*GetName()));
		}

		if (Family.bOverrideTerrainTransitionPolicy)
		{
			ValidateTerrainTransitionPolicy(
				Family.TerrainTransitionPolicyOverride,
				FString::Printf(TEXT("continuation family %s terrain-transition override"), *Family.FamilyId.ToString()),
				this,
				Result);
		}

		if (!IsAuthoredRootSolveBudgetValid(Family.SolveBudget))
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding continuation family %s authors SolveBudget.MaxSolveDurationSeconds=%.3f, but the active continuation path requires a positive family-owned root-attempt timeout.\nWorldBinding: %s\nFix: Author a positive continuation-family solve budget so one path/bridge/tunnel attempt can time out cleanly instead of running unbounded."),
				*Family.FamilyId.ToString(),
				Family.SolveBudget.MaxSolveDurationSeconds,
				*GetName()));
		}

		if (!Family.EndpointConnectorTypeTag.IsValid())
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding continuation family %s does not author a valid EndpointConnectorTypeTag.\nWorldBinding: %s\nFix: Author one deterministic exported endpoint connector tag on every continuation family so SurfacePath, bridge, and tunnel selection stay on the world-binding continuation pipeline."),
				*Family.FamilyId.ToString(),
				*GetName()));
		}

		if (Family.FamilyType != ELayoutWorldBindingContinuationFamilyType::BridgeContinuation
			&& Family.ContinuationPolicy.MaxBridgeGapCells > 0)
		{
			Result.AddError(FString::Printf(
				TEXT("Layout world binding continuation family %s authors MaxBridgeGapCells=%d, but only BridgeContinuation families may consume bridge-gap spans on the active continuation path.\nWorldBinding: %s\nFix: Move gap-span behavior onto a BridgeContinuation family or clear MaxBridgeGapCells on this family."),
				*Family.FamilyId.ToString(),
				Family.ContinuationPolicy.MaxBridgeGapCells,
				*GetName()));
		}

		for (int32 CandidateIndex = 0; CandidateIndex < Family.Candidates.Num(); ++CandidateIndex)
		{
			const FLayoutWorldBindingContinuationCandidate& Candidate = Family.Candidates[CandidateIndex];
			const FString CandidateContext = FString::Printf(
				TEXT("continuation-family %s"),
				*Family.FamilyId.ToString());
			ValidateWorldBindingContinuationCandidate(this, Candidate, CandidateIndex, *CandidateContext, Result);

			if (!CandidateHasExplicitContinuationEntryLevel(Candidate))
			{
				Result.AddError(FString::Printf(
					TEXT("Layout world binding continuation family %s candidate %d does not author an explicit continuation-entry level on its profile.\nWorldBinding: %s\nProfile: %s\nFix: Set ContinuationEntryLevel on the candidate profile. Continuation candidates always require an explicit entry level on the final world-binding path."),
					*Family.FamilyId.ToString(),
					CandidateIndex,
					*GetName(),
					*GetNameSafe(Candidate.LayoutProfile)));
			}
		}
	}

	TMap<ELayoutWorldBindingContinuationFamilyType, TMap<FGameplayTag, FName>>
		FamilyIdsByEndpointConnectorTypeTagByType;
	for (const FLayoutWorldBindingContinuationFamily& Family : ContinuationFamilies)
	{
		if (Family.EndpointConnectorTypeTag.IsValid())
		{
			TMap<FGameplayTag, FName>& FamilyIdsByEndpointConnectorTypeTag =
				FamilyIdsByEndpointConnectorTypeTagByType.FindOrAdd(Family.FamilyType);
			if (const FName* const ExistingFamilyId =
				FamilyIdsByEndpointConnectorTypeTag.Find(Family.EndpointConnectorTypeTag))
			{
				Result.AddError(FString::Printf(
					TEXT("Layout world binding authors %s continuation families %s and %s with the same EndpointConnectorTypeTag %s.\nWorldBinding: %s\nFix: The active continuation runtime requires one deterministic continuation family per family type and exported endpoint tag."),
					ResolveContinuationFamilyTypeLabel(Family.FamilyType),
					*ExistingFamilyId->ToString(),
					*Family.FamilyId.ToString(),
					*Family.EndpointConnectorTypeTag.ToString(),
					*GetName()));
			}
			else
			{
				FamilyIdsByEndpointConnectorTypeTag.Add(Family.EndpointConnectorTypeTag, Family.FamilyId);
			}
		}
	}

	const bool bAnyOrdinaryRootCandidateExportsSurfacePathConnectorTags =
		Candidates.ContainsByPredicate([](const FLayoutWorldBindingCandidate& Candidate)
		{
			const FGameplayTagContainer ExportedConnectorTypeTags =
				ResolveEffectiveCandidateExportedConnectorTypeTags(Candidate);
			return ExportedConnectorTypeTags.HasTagExact(LayoutGameplayTags::ConnectorRoad)
				|| ExportedConnectorTypeTags.HasTagExact(LayoutGameplayTags::ConnectorTrail);
		});
	const TMap<FGameplayTag, FName>* const SurfacePathFamilyIdsByEndpointConnectorTypeTag =
		FamilyIdsByEndpointConnectorTypeTagByType.Find(
			ELayoutWorldBindingContinuationFamilyType::SurfacePath);
	if (bAnyOrdinaryRootCandidateExportsSurfacePathConnectorTags
		&& (SurfacePathFamilyIdsByEndpointConnectorTypeTag == nullptr
			|| SurfacePathFamilyIdsByEndpointConnectorTypeTag->IsEmpty()))
	{
		Result.AddError(FString::Printf(
			TEXT("Layout world binding has ordinary-root candidates that export connector tags, but it does not author any SurfacePath continuation family.\nWorldBinding: %s\nFix: Author one SurfacePath continuation family on the binding so exported road/path endpoints stay active on the continuation runtime path."),
			*GetName()));
	}

	for (const FLayoutWorldBindingCandidate& Candidate : Candidates)
	{
		if (Candidate.LayoutProfile == nullptr)
		{
			continue;
		}

		const FGameplayTagContainer ExportedConnectorTypeTags =
			ResolveEffectiveCandidateExportedConnectorTypeTags(Candidate);
		for (const FGameplayTag& ExportedConnectorTypeTag : ExportedConnectorTypeTags)
		{
			bool bFoundMatchingFamily = false;
			for (const TPair<ELayoutWorldBindingContinuationFamilyType, TMap<FGameplayTag, FName>>& Pair :
				FamilyIdsByEndpointConnectorTypeTagByType)
			{
				if (Pair.Value.Contains(ExportedConnectorTypeTag))
				{
					bFoundMatchingFamily = true;
					break;
				}
			}
			if (!bFoundMatchingFamily)
			{
				Result.AddError(FString::Printf(
					TEXT("Layout world binding candidate %s exports connector tag %s, but no continuation family on the binding authors that EndpointConnectorTypeTag.\nWorldBinding: %s\nFix: Author one continuation family with a matching EndpointConnectorTypeTag so exported endpoints stay active on the world-binding continuation runtime path."),
					*Candidate.CandidateId.ToString(),
					*ExportedConnectorTypeTag.ToString(),
					*GetName()));
			}

			const bool bSurfacePathConnectorTag =
				ExportedConnectorTypeTag == LayoutGameplayTags::ConnectorRoad
				|| ExportedConnectorTypeTag == LayoutGameplayTags::ConnectorTrail;
			if (bSurfacePathConnectorTag
				&& (SurfacePathFamilyIdsByEndpointConnectorTypeTag == nullptr
					|| !SurfacePathFamilyIdsByEndpointConnectorTypeTag->Contains(
						ExportedConnectorTypeTag)))
			{
				Result.AddError(FString::Printf(
					TEXT("Layout world binding candidate %s exports surface-path connector tag %s, but no SurfacePath continuation family on the binding authors that EndpointConnectorTypeTag.\nWorldBinding: %s\nFix: Author one SurfacePath continuation family with a matching EndpointConnectorTypeTag for each exported road/path root connector tag so ordinary-root endpoints stay on the active SurfacePath runtime path."),
					*Candidate.CandidateId.ToString(),
					*ExportedConnectorTypeTag.ToString(),
					*GetName()));
			}
		}
	}

	return Result;
}

#if WITH_EDITOR
void ULayoutWorldBindingAsset::ValidateLayoutWorldBindingInEditor() const
{
	const FLayoutValidationResult ValidationResult = ValidateWorldBinding();
	PorismLayoutEditorMessageLog::ReportValidationResult(
		this,
		ValidationResult,
		FString::Printf(TEXT("Layout world binding '%s' validation passed."), *GetName()));
}

EDataValidationResult ULayoutWorldBindingAsset::IsDataValid(FDataValidationContext& Context) const
{
	const FLayoutValidationResult ValidationResult = ValidateWorldBinding();
	AppendWorldBindingValidationResult(Context, ValidationResult);
	return ValidationResult.IsValid()
		? EDataValidationResult::Valid
		: EDataValidationResult::Invalid;
}
#endif
