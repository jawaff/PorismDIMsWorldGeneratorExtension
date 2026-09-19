// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutProfileAsset.h"

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Diagnostics/LayoutEditorMessageLog.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/DataValidation.h"

namespace
{
	int32 CountBoundaryCells(const FIntPoint& Footprint)
	{
		if (Footprint.X <= 0 || Footprint.Y <= 0)
		{
			return 0;
		}

		if (Footprint.X == 1 && Footprint.Y == 1)
		{
			return 1;
		}

		if (Footprint.X == 1)
		{
			return Footprint.Y;
		}

		if (Footprint.Y == 1)
		{
			return Footprint.X;
		}

		return (Footprint.X * 2) + (Footprint.Y * 2) - 4;
	}

	int32 GetExactOrRangeMinimum(
		const ELayoutCountConstraintMode Mode,
		const int32 ExactCount,
		const int32 MinCount)
	{
		switch (Mode)
		{
		case ELayoutCountConstraintMode::Exact:
			return ExactCount;
		case ELayoutCountConstraintMode::Range:
			return MinCount;
		case ELayoutCountConstraintMode::None:
		default:
			return 0;
		}
	}

	int32 GetExactOrRangeMaximum(
		const ELayoutCountConstraintMode Mode,
		const int32 ExactCount,
		const int32 MaxCount)
	{
		switch (Mode)
		{
		case ELayoutCountConstraintMode::Exact:
			return ExactCount;
		case ELayoutCountConstraintMode::Range:
			return MaxCount;
		case ELayoutCountConstraintMode::None:
		default:
			return 0;
		}
	}

	void AppendProfileValidationResult(
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

	bool IsBoundaryLikePlacementZone(const ELayoutPlacementZone PlacementZone)
	{
		return PlacementZone == ELayoutPlacementZone::Perimeter
			|| PlacementZone == ELayoutPlacementZone::Edge
			|| PlacementZone == ELayoutPlacementZone::Corner;
	}

	bool DoesPotentialPlacementZoneOverlapRequirementZone(
		const ELayoutPlacementZone EntryPlacementZone,
		const ELayoutPlacementZone RequirementZone);

	bool ProfileModuleContentSettingsReferenceExactlyOneSource(const FLayoutModuleContentSettings& Settings)
	{
		const bool bHasLeafModule = Settings.Module != nullptr;
		const bool bHasCompositeModule = Settings.CompositeModule != nullptr;
		return bHasLeafModule != bHasCompositeModule;
	}

	bool ProfileModuleContentSettingsSupportsIntent(
		const FLayoutModuleContentSettings& Settings,
		const ELayoutCellIntent Intent)
	{
		if (Settings.Module != nullptr)
		{
			return Settings.Module->SupportsIntent(Intent);
		}
		if (Settings.CompositeModule != nullptr)
		{
			return Settings.CompositeModule->SupportsIntent(Intent);
		}

		return false;
	}

	bool ContentSetHasModuleEntrySupportingIntent(
		const ULayoutRegionContentSetAsset* ContentSet,
		const ELayoutCellIntent Intent,
		const ELayoutPlacementZone RequiredPlacementZone = ELayoutPlacementZone::Any)
	{
		if (ContentSet == nullptr)
		{
			return false;
		}

		for (const FLayoutRegionContentEntry& Entry : ContentSet->Entries)
		{
			if (Entry.ContentKind != ELayoutRegionContentKind::Module
				|| !ProfileModuleContentSettingsReferenceExactlyOneSource(Entry.ModuleSettings))
			{
				continue;
			}

			if (ProfileModuleContentSettingsSupportsIntent(Entry.ModuleSettings, Intent)
				&& DoesPotentialPlacementZoneOverlapRequirementZone(Entry.ModuleSettings.PlacementZone, RequiredPlacementZone))
			{
				return true;
			}
		}

		return false;
	}

	/** Returns true when the content set has at least one authored module/composite entry for pseudo-joining CSP placement. */
	bool ContentSetHasDirectModuleOrCompositeEntry(const ULayoutRegionContentSetAsset* ContentSet)
	{
		if (ContentSet == nullptr)
		{
			return false;
		}

		for (const FLayoutRegionContentEntry& Entry : ContentSet->Entries)
		{
			if (Entry.ContentKind == ELayoutRegionContentKind::Module
				&& ProfileModuleContentSettingsReferenceExactlyOneSource(Entry.ModuleSettings))
			{
				return true;
			}
		}

		return false;
	}

	bool ContentSetHasModuleEntrySatisfyingPredicate(
		const ULayoutRegionContentSetAsset* ContentSet,
		const ELayoutCellIntent Intent,
		const TFunctionRef<bool(const ULayoutModuleAsset*)>& Predicate,
		const ELayoutPlacementZone RequiredPlacementZone = ELayoutPlacementZone::Any)
	{
		if (ContentSet == nullptr)
		{
			return false;
		}

		for (const FLayoutRegionContentEntry& Entry : ContentSet->Entries)
		{
			if (Entry.ContentKind != ELayoutRegionContentKind::Module
				|| Entry.ModuleSettings.Module == nullptr
				|| !ProfileModuleContentSettingsReferenceExactlyOneSource(Entry.ModuleSettings)
				|| !Entry.ModuleSettings.Module->SupportsIntent(Intent)
				|| !DoesPotentialPlacementZoneOverlapRequirementZone(Entry.ModuleSettings.PlacementZone, RequiredPlacementZone))
			{
				continue;
			}

			if (Predicate(Entry.ModuleSettings.Module))
			{
				return true;
			}
		}

		return false;
	}

	FString FormatZoneFeatureTags(const FGameplayTagContainer& Tags)
	{
		TArray<FGameplayTag> SortedTags;
		Tags.GetGameplayTagArray(SortedTags);
		SortedTags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
		{
			return Left.ToString() < Right.ToString();
		});

		if (SortedTags.IsEmpty())
		{
			return TEXT("<none>");
		}

		TArray<FString> TagStrings;
		TagStrings.Reserve(SortedTags.Num());
		for (const FGameplayTag& Tag : SortedTags)
		{
			TagStrings.Add(Tag.ToString());
		}

		return FString::Printf(TEXT("[%s]"), *FString::Join(TagStrings, TEXT(", ")));
	}

	bool DoesProvidedZoneFeatureSetMatchRequirement(
		const FGameplayTagContainer& ProvidedFeatures,
		const FLayoutZoneFeatureRequirement& Requirement)
	{
		if (Requirement.RequiredFeatures.IsEmpty())
		{
			return false;
		}

		switch (Requirement.MatchMode)
		{
		case ELayoutZoneFeatureMatchMode::All:
			return ProvidedFeatures.HasAllExact(Requirement.RequiredFeatures);
		case ELayoutZoneFeatureMatchMode::Any:
		default:
			return ProvidedFeatures.HasAnyExact(Requirement.RequiredFeatures);
		}
	}

	bool DoesPotentialPlacementZoneOverlapRequirementZone(
		const ELayoutPlacementZone EntryPlacementZone,
		const ELayoutPlacementZone RequirementZone)
	{
		if (RequirementZone == ELayoutPlacementZone::Any || EntryPlacementZone == ELayoutPlacementZone::Any)
		{
			return true;
		}

		if (RequirementZone == EntryPlacementZone)
		{
			return true;
		}

		switch (RequirementZone)
		{
		case ELayoutPlacementZone::Perimeter:
			return EntryPlacementZone == ELayoutPlacementZone::Edge
				|| EntryPlacementZone == ELayoutPlacementZone::Corner;
		case ELayoutPlacementZone::Edge:
		case ELayoutPlacementZone::Corner:
			return EntryPlacementZone == ELayoutPlacementZone::Perimeter;
		case ELayoutPlacementZone::Interior:
			return EntryPlacementZone == ELayoutPlacementZone::Core;
		case ELayoutPlacementZone::Core:
			return EntryPlacementZone == ELayoutPlacementZone::Interior;
		case ELayoutPlacementZone::Any:
		default:
			return false;
		}
	}

	FString DescribeZoneFeatureCountExpectation(const FLayoutZoneFeatureRequirement& Requirement)
	{
		if (Requirement.MaxCount > 0)
		{
			if (Requirement.MinCount == Requirement.MaxCount)
			{
				return FString::Printf(TEXT("exactly %d"), Requirement.MinCount);
			}

			return FString::Printf(TEXT("between %d and %d"), Requirement.MinCount, Requirement.MaxCount);
		}

		if (Requirement.MinCount > 0)
		{
			return FString::Printf(TEXT("at least %d"), Requirement.MinCount);
		}

		return TEXT("any count");
	}

	FString BuildProfileValidationMessage(
		const ULayoutProfileAsset* Profile,
		const FString& Summary,
		const TArray<FString>& DetailLines)
	{
		FString Message = Summary;
		Message += FString::Printf(TEXT("\nProfile: %s"), Profile != nullptr ? *Profile->GetName() : TEXT("<none>"));
		for (const FString& DetailLine : DetailLines)
		{
			if (!DetailLine.IsEmpty())
			{
				Message += TEXT("\n");
				Message += DetailLine;
			}
		}

		return Message;
	}

	void AddDisabledTerrainSeamModuleWarnings(
		const ULayoutProfileAsset* Profile,
		FLayoutValidationResult& InOutResult)
	{
		if (Profile == nullptr
			|| !Profile->bSupportsSteppedTerrainSolve
			|| Profile->bEnableTerrainSeams
			|| Profile->ContentSet == nullptr)
		{
			return;
		}

		TSet<const ULayoutModuleAsset*> WarnedModules;
		TSet<const ULayoutRegionContentSetAsset*> VisitedContentSets;
		auto InspectModule = [&InOutResult, Profile, &WarnedModules](
			const ULayoutModuleAsset* Module,
			const ULayoutRegionContentSetAsset* SourceContentSet)
		{
			if (Module == nullptr || WarnedModules.Contains(Module))
			{
				return;
			}

			TArray<FString> SeamFaces;
			for (const FLayoutFaceRule& FaceRule : Module->GetEffectiveFaceRules().ToArray())
			{
				if (FaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceTerrainSeam)
				{
					SeamFaces.Add(StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(FaceRule.Direction)));
				}
			}
			if (SeamFaces.IsEmpty())
			{
				return;
			}

			WarnedModules.Add(Module);
		InOutResult.AddWarning(BuildProfileValidationMessage(
				Profile,
				FString::Printf(
					TEXT("Terrain seams are disabled, but module '%s' contains MustFaceTerrainSeam rules."),
					*Module->GetName()),
				{
					FString::Printf(TEXT("ContentSet: %s"), SourceContentSet != nullptr ? *SourceContentSet->GetPathName() : TEXT("<none>")),
					FString::Printf(TEXT("Module: %s"), *Module->GetPathName()),
					FString::Printf(TEXT("Faces: %s"), *FString::Join(SeamFaces, TEXT(", "))),
					TEXT("Impact: This profile never authors terrain-seam faces, so these orientations may be unavailable."),
					TEXT("Fix: Enable terrain seams for this profile or change these faces to an ordinary boundary rule." )
				}));
		};

		TFunction<void(const ULayoutRegionContentSetAsset*)> InspectContentSet;
		InspectContentSet = [&InspectContentSet, &InspectModule, &VisitedContentSets](const ULayoutRegionContentSetAsset* ContentSet)
		{
			if (ContentSet == nullptr || VisitedContentSets.Contains(ContentSet))
			{
				return;
			}
			VisitedContentSets.Add(ContentSet);
			for (const FLayoutRegionContentEntry& Entry : ContentSet->Entries)
			{
				if (Entry.ContentKind == ELayoutRegionContentKind::Module)
				{
					if (Entry.ModuleSettings.Module != nullptr)
					{
						InspectModule(Entry.ModuleSettings.Module, ContentSet);
					}
					if (Entry.ModuleSettings.CompositeModule != nullptr)
					{
						for (const FLayoutCompositeModuleCell& Cell : Entry.ModuleSettings.CompositeModule->Cells)
						{
							InspectModule(Cell.Module, ContentSet);
						}
					}
				}
				else if (Entry.ChildRegionSettings.RegionProfile != nullptr)
				{
					InspectContentSet(Entry.ChildRegionSettings.RegionProfile->ContentSet);
				}
			}
		};
		InspectContentSet(Profile->ContentSet);
	}

	bool IsLayoutFeatureTag(const FGameplayTag& Tag)
	{
		return Tag.IsValid() && Tag.ToString().StartsWith(TEXT("Layout.Feature."));
	}

	struct FResolvedSparseRuleAuthoringView
	{
		const FLayoutSparsePlacementRuleBase* Rule = nullptr;
		const FLayoutSparseContentRuleBase* ContentRule = nullptr;
		int32 MaximumCount = 0;
	};

	/** Resolves one supported reflected sparse-rule variant without retaining polymorphic data in solver snapshots. */
	bool TryResolveSparseRuleAuthoringView(
		const FInstancedStruct& InstancedRule,
		FResolvedSparseRuleAuthoringView& OutView)
	{
		OutView = FResolvedSparseRuleAuthoringView();
		OutView.Rule = InstancedRule.GetPtr<FLayoutSparsePlacementRuleBase>();
		if (OutView.Rule == nullptr)
		{
			return false;
		}
		if (InstancedRule.GetPtr<FLayoutSparsePreserveTerrainRule>() != nullptr)
		{
			return true;
		}
		if (const FLayoutSparseExactPlacementRule* ExactRule =
			InstancedRule.GetPtr<FLayoutSparseExactPlacementRule>())
		{
			OutView.ContentRule = ExactRule;
			OutView.MaximumCount = ExactRule->Count;
			return true;
		}
		if (const FLayoutSparseRangePlacementRule* RangeRule =
			InstancedRule.GetPtr<FLayoutSparseRangePlacementRule>())
		{
			OutView.ContentRule = RangeRule;
			OutView.MaximumCount = RangeRule->MaxCount;
			return true;
		}
		if (const FLayoutSparseFillAvailablePlacementRule* FillRule =
			InstancedRule.GetPtr<FLayoutSparseFillAvailablePlacementRule>())
		{
			OutView.ContentRule = FillRule;
			OutView.MaximumCount = MAX_int32;
			return true;
		}
		return false;
	}

	void ValidateBoundsPolicy(
		const FLayoutBoundsPolicy& BoundsPolicy,
		const FIntPoint& MaximumFootprintInCells,
		const int32 LevelCount,
		FLayoutValidationResult& Result,
		const FString& FieldPrefix)
	{
		if (BoundsPolicy.Mode == ELayoutBoundsPolicyMode::SolvedFootprint)
		{
			if (BoundsPolicy.InsetCells < 0)
			{
				Result.AddError(FString::Printf(TEXT("%s InsetCells cannot be negative."), *FieldPrefix));
			}

			if (BoundsPolicy.MinLevel < 0)
			{
				Result.AddError(FString::Printf(TEXT("%s MinLevel cannot be negative."), *FieldPrefix));
			}

			if (BoundsPolicy.MaxLevel < BoundsPolicy.MinLevel)
			{
				Result.AddError(FString::Printf(TEXT("%s MaxLevel cannot be less than MinLevel."), *FieldPrefix));
			}

			if (BoundsPolicy.MaxLevel >= LevelCount)
			{
				Result.AddError(FString::Printf(
					TEXT("%s MaxLevel=%d exceeds LevelCount=%d."),
					*FieldPrefix,
					BoundsPolicy.MaxLevel,
					LevelCount));
			}

			if (BoundsPolicy.InsetCells * 2 >= MaximumFootprintInCells.X
				|| BoundsPolicy.InsetCells * 2 >= MaximumFootprintInCells.Y)
			{
				Result.AddError(FString::Printf(
					TEXT("%s InsetCells=%d collapses the solved-footprint bounds for maximum footprint %dx%d."),
					*FieldPrefix,
					BoundsPolicy.InsetCells,
					MaximumFootprintInCells.X,
					MaximumFootprintInCells.Y));
			}

			if (BoundsPolicy.FootprintMask.Num() > 0)
			{
				Result.AddError(FString::Printf(
					TEXT("%s FootprintMask is only supported when BoundsPolicy Mode is ExplicitLocalBounds."),
					*FieldPrefix));
			}

			return;
		}

		if (BoundsPolicy.MinCells.X < 0 || BoundsPolicy.MinCells.Y < 0 || BoundsPolicy.MinCells.Z < 0)
		{
			Result.AddError(FString::Printf(TEXT("%s MinCells cannot contain negative coordinates."), *FieldPrefix));
		}

		if (BoundsPolicy.MaxCells.X <= BoundsPolicy.MinCells.X
			|| BoundsPolicy.MaxCells.Y <= BoundsPolicy.MinCells.Y
			|| BoundsPolicy.MaxCells.Z <= BoundsPolicy.MinCells.Z)
		{
			Result.AddError(FString::Printf(
				TEXT("%s Explicit local bounds must use exclusive MaxCells greater than MinCells on every axis."),
				*FieldPrefix));
		}

		if (BoundsPolicy.MaxCells.X > MaximumFootprintInCells.X || BoundsPolicy.MaxCells.Y > MaximumFootprintInCells.Y)
		{
			Result.AddError(FString::Printf(
				TEXT("%s Explicit local bounds exceed the profile maximum footprint of %dx%d cells."),
				*FieldPrefix,
				MaximumFootprintInCells.X,
				MaximumFootprintInCells.Y));
		}

		if (BoundsPolicy.MaxCells.Z > LevelCount)
		{
			Result.AddError(FString::Printf(
				TEXT("%s Explicit local bounds exceed LevelCount=%d."),
				*FieldPrefix,
				LevelCount));
		}

		for (int32 MaskIndex = 0; MaskIndex < BoundsPolicy.FootprintMask.Num(); ++MaskIndex)
		{
			const FIntVector& MaskCell = BoundsPolicy.FootprintMask[MaskIndex];
			const bool bInsideBounds = MaskCell.X >= BoundsPolicy.MinCells.X
				&& MaskCell.X < BoundsPolicy.MaxCells.X
				&& MaskCell.Y >= BoundsPolicy.MinCells.Y
				&& MaskCell.Y < BoundsPolicy.MaxCells.Y
				&& MaskCell.Z >= BoundsPolicy.MinCells.Z
				&& MaskCell.Z < BoundsPolicy.MaxCells.Z;
			if (!bInsideBounds)
			{
				Result.AddError(FString::Printf(
					TEXT("%s FootprintMask[%d]=(%d,%d,%d) lies outside the explicit local bounds."),
					*FieldPrefix,
					MaskIndex,
					MaskCell.X,
					MaskCell.Y,
					MaskCell.Z));
			}
		}
	}
}

FLayoutValidationResult ULayoutProfileAsset::ValidateProfile() const
{
	FLayoutValidationResult Result;

	if (ContentSet != nullptr)
	{
		Result.Messages.Append(ContentSet->ValidateContentSet().Messages);
	}
	AddDisabledTerrainSeamModuleWarnings(this, Result);

	if (MinimumFootprintInCells.X <= 0 || MinimumFootprintInCells.Y <= 0)
	{
		Result.AddError(TEXT("MinimumFootprintInCells must be positive on both axes."));
	}

	if (MaximumFootprintInCells.X <= 0 || MaximumFootprintInCells.Y <= 0)
	{
		Result.AddError(TEXT("MaximumFootprintInCells must be positive on both axes."));
	}

	if (MinimumFootprintInCells.X > MaximumFootprintInCells.X || MinimumFootprintInCells.Y > MaximumFootprintInCells.Y)
	{
		Result.AddError(TEXT("MinimumFootprintInCells cannot exceed MaximumFootprintInCells."));
	}

	if (LevelCount <= 0)
	{
		Result.AddError(TEXT("LevelCount must be greater than zero."));
	}

	const int32 MinResolvedEntryCount = GetExactOrRangeMinimum(EntryCountMode, EntryCount, MinEntryCount);
	const int32 MaxResolvedEntryCount = GetExactOrRangeMaximum(EntryCountMode, EntryCount, MaxEntryCount);
	if (EntryCountMode == ELayoutCountConstraintMode::Exact && EntryCount <= 0)
	{
		Result.AddError(TEXT("EntryCount must be greater than zero when EntryCountMode is Exact."));
	}
	else if (EntryCountMode == ELayoutCountConstraintMode::Range && (MinEntryCount < 0 || MaxEntryCount < 0 || MinEntryCount > MaxEntryCount))
	{
		Result.AddError(TEXT("Entry count range must be non-negative and MinEntryCount cannot exceed MaxEntryCount."));
	}

	if (MaxResolvedEntryCount > CountBoundaryCells(MaximumFootprintInCells))
	{
		Result.AddError(FString::Printf(
			TEXT("Entry count maximum=%d exceeds the maximum footprint boundary capacity of %d cells."),
			MaxResolvedEntryCount,
			CountBoundaryCells(MaximumFootprintInCells)));
	}

	if (ContentSet != nullptr
		&& MaxResolvedEntryCount > 0
		&& !ContentSetHasModuleEntrySupportingIntent(ContentSet, ELayoutCellIntent::Entry, ELayoutPlacementZone::Perimeter))
	{
		Result.AddError(BuildProfileValidationMessage(
			this,
			TEXT("Profile can reserve entry cells, but the content set has no entry-capable module entries."),
			{
				FString::Printf(TEXT("ContentSet: %s"), *ContentSet->GetName()),
				FString::Printf(TEXT("Reserved entry maximum: %d"), MaxResolvedEntryCount),
				TEXT("Problem: The planner may reserve Entry cells, but no module entries support Entry intent."),
				TEXT("Fix: Add at least one Entry-capable module entry, or reduce the profile's entry count requirement.")
			}));
	}

	const int32 MaxResolvedVerticalAccessCount = GetExactOrRangeMaximum(VerticalAccessCountMode, VerticalAccessCount, MaxVerticalAccessCount);
	if (VerticalAccessCountMode == ELayoutCountConstraintMode::Exact && VerticalAccessCount <= 0)
	{
		Result.AddError(TEXT("VerticalAccessCount must be greater than zero when VerticalAccessCountMode is Exact."));
	}
	else if (VerticalAccessCountMode == ELayoutCountConstraintMode::Range && (MinVerticalAccessCount < 0 || MaxVerticalAccessCount < 0 || MinVerticalAccessCount > MaxVerticalAccessCount))
	{
		Result.AddError(TEXT("Vertical access count range must be non-negative and MinVerticalAccessCount cannot exceed MaxVerticalAccessCount."));
	}

	// Authored counts and strict reachability are separate contracts. Actual prepared
	// topology decides whether implicit ascent is needed; capability alone must not
	// force a positive authored minimum or reject flat/side-deck continuations.

	if (LevelCount <= 1
		&& !bSupportsSteppedTerrainSolve
		&& MaxResolvedVerticalAccessCount > 0)
	{
		Result.AddWarning(TEXT("Vertical access cells are reserved for a single-level profile. This is allowed, but stairs/ramps usually become useful when LevelCount is greater than one."));
	}

	if (ContentSet != nullptr
		&& MaxResolvedVerticalAccessCount > 0
		&& !ContentSetHasModuleEntrySupportingIntent(ContentSet, ELayoutCellIntent::VerticalAccess))
	{
		Result.AddError(BuildProfileValidationMessage(
			this,
			TEXT("Profile can reserve vertical-access cells, but the content set has no VerticalAccess-capable module entries."),
			{
				FString::Printf(TEXT("ContentSet: %s"), *ContentSet->GetName()),
				FString::Printf(TEXT("Reserved vertical-access maximum: %d"), MaxResolvedVerticalAccessCount),
				TEXT("Fix: Add at least one VerticalAccess-capable module entry, or reduce the profile's vertical-access count requirement.")
			}));
	}

	const int32 MaxInteriorCellCount = FMath::Max(0, (MaximumFootprintInCells.X - 2) * (MaximumFootprintInCells.Y - 2));
	if (MaxResolvedVerticalAccessCount > MaxInteriorCellCount)
	{
		Result.AddError(FString::Printf(
			TEXT("Vertical access count maximum=%d exceeds the maximum interior footprint capacity of %d cells."),
			MaxResolvedVerticalAccessCount,
			MaxInteriorCellCount));
	}

	if (bSupportsSteppedTerrainSolve)
	{
		if (ContentSet == nullptr)
		{
			Result.AddError(BuildProfileValidationMessage(
				this,
				TEXT("Stepped-terrain-capable profiles require a content set."),
				{
					TEXT("Problem: Stepped-terrain capability is validated against unified content-set entries."),
					TEXT("Fix: Assign a ContentSet to this profile, or disable stepped-terrain capability.")
				}));
		}
		else
		{
			if (LevelCount <= 1 && !ContentSetHasDirectModuleOrCompositeEntry(ContentSet))
			{
				Result.AddError(BuildProfileValidationMessage(
					this,
					TEXT("Single-level stepped-terrain-capable profiles require module or composite content for bridge cells."),
					{
						FString::Printf(TEXT("ContentSet: %s"), *ContentSet->GetName()),
						TEXT("Problem: Single-level stepped terrain injects bridge planned cells at the higher terrain stage Z level that must be filled by normal CSP module/composite content."),
						TEXT("Fix: Add at least one module or composite content entry that can satisfy shifted-face continuity, or disable stepped-terrain capability on this profile.")
					}));
			}

			const bool bRequiresVerticalAccessCapableContent =
				MaxResolvedVerticalAccessCount > 0
				|| bRequireAllTraversalChannelsReachable;
			if (bRequiresVerticalAccessCapableContent
				&& !ContentSetHasModuleEntrySupportingIntent(ContentSet, ELayoutCellIntent::VerticalAccess))
			{
				Result.AddError(BuildProfileValidationMessage(
					this,
					TEXT("Stepped-terrain-capable profiles require VerticalAccess-capable content when traversal or authored vertical-access counts require it."),
					{
						FString::Printf(TEXT("ContentSet: %s"), *ContentSet->GetName()),
						FString::Printf(TEXT("LevelCount: %d"), LevelCount),
						FString::Printf(TEXT("Required traversal reachability: %s"), bRequireAllTraversalChannelsReachable ? TEXT("true") : TEXT("false")),
						FString::Printf(TEXT("Reserved vertical-access maximum: %d"), MaxResolvedVerticalAccessCount),
						TEXT("Fix: Add at least one VerticalAccess-capable module/composite entry, disable required traversal reachability, or reduce the authored vertical-access count requirement.")
					}));
			}
		}
	}

	if (ContinuationEntryLevel != INDEX_NONE)
	{
		const int32 ContinuationCorridorWidthInCells = FMath::Max(
			MinimumFootprintInCells.X,
			MinimumFootprintInCells.Y);
		if (ContinuationCorridorWidthInCells <= 0 || ContinuationCorridorWidthInCells % 2 == 0)
		{
			Result.AddError(BuildProfileValidationMessage(
				this,
				TEXT("Continuation profiles require an odd minimum-footprint corridor width."),
				{
					FString::Printf(TEXT("MinimumFootprintInCells: (%d,%d)"), MinimumFootprintInCells.X, MinimumFootprintInCells.Y),
					FString::Printf(TEXT("Resolved corridor width: %d"), ContinuationCorridorWidthInCells),
					TEXT("Problem: Continuation centerlines need a single centered Entry cell at both endpoints."),
					TEXT("Fix: Use an odd maximum minimum-footprint dimension, such as 1 or 3.")
				}));
		}
		if (!bRequireAllTraversalChannelsReachable)
		{
			Result.AddError(BuildProfileValidationMessage(
				this,
				TEXT("Profiles with an explicit continuation-entry level require all traversal channels to be reachable."),
				{
					FString::Printf(TEXT("ContinuationEntryLevel: %d"), ContinuationEntryLevel),
					TEXT("Problem: Continuation layouts are roads, tunnels, or bridges and must prove traversal through the profile."),
					TEXT("Fix: Enable Require All Traversal Channels Reachable or clear ContinuationEntryLevel back to -1.")
				}));
		}

		if (ContentSet == nullptr)
		{
			Result.AddError(BuildProfileValidationMessage(
				this,
				TEXT("Profiles with an explicit continuation-entry level require a content set."),
				{
					TEXT("Problem: Deterministic continuation-entry alignment is validated against unified content-set entries."),
					TEXT("Fix: Assign a ContentSet to this profile, or clear ContinuationEntryLevel back to -1.")
				}));
		}
		else if (ContinuationEntryLevel < 0 || ContinuationEntryLevel >= LevelCount)
		{
			Result.AddError(BuildProfileValidationMessage(
				this,
				TEXT("Explicit continuation-entry level must fall within the profile's solved level range."),
				{
					FString::Printf(TEXT("ContinuationEntryLevel: %d"), ContinuationEntryLevel),
					FString::Printf(TEXT("LevelCount: %d"), LevelCount),
					TEXT("Problem: World-facing continuation alignment cannot target a local level that the profile does not solve."),
					TEXT("Fix: Keep ContinuationEntryLevel within [0, LevelCount - 1], or clear it back to -1.")
				}));
		}
		else if (!ContentSetHasModuleEntrySupportingIntent(ContentSet, ELayoutCellIntent::Entry))
		{
			Result.AddError(BuildProfileValidationMessage(
				this,
				TEXT("Profiles with an explicit continuation-entry level require Entry-capable content."),
				{
					FString::Printf(TEXT("ContentSet: %s"), *ContentSet->GetName()),
					FString::Printf(TEXT("ContinuationEntryLevel: %d"), ContinuationEntryLevel),
					TEXT("Problem: Deterministic continuation-entry alignment needs at least one Entry-capable module entry in the content set."),
					TEXT("Fix: Add Entry-capable continuation content, or clear ContinuationEntryLevel back to -1.")
				}));
		}
	}

	if (!ZoneFeatureRequirements.IsEmpty() && ContentSet == nullptr)
	{
		Result.AddError(BuildProfileValidationMessage(
			this,
			TEXT("Zone feature requirements require a content set."),
			{
				TEXT("Problem: Counted zone features are provided by unified content entries, not by the bare profile."),
				TEXT("Fix: Assign a ContentSet to this profile before authoring zone feature requirements.")
			}));
	}

	TSet<FName> SeenZoneFeatureRequirementIds;
	for (int32 RequirementIndex = 0; RequirementIndex < ZoneFeatureRequirements.Num(); ++RequirementIndex)
	{
		const FLayoutZoneFeatureRequirement& Requirement = ZoneFeatureRequirements[RequirementIndex];
		const FString FieldPrefix = FString::Printf(TEXT("ZoneFeatureRequirements[%d]"), RequirementIndex);
		const FString ZoneName = StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(Requirement.Zone));
		const FString MatchModeName = StaticEnum<ELayoutZoneFeatureMatchMode>()->GetNameStringByValue(static_cast<int64>(Requirement.MatchMode));

		if (Requirement.RequirementId.IsNone())
		{
			Result.AddError(FString::Printf(TEXT("%s must define a non-empty RequirementId."), *FieldPrefix));
		}
		else if (SeenZoneFeatureRequirementIds.Contains(Requirement.RequirementId))
		{
			Result.AddError(FString::Printf(
				TEXT("%s duplicates RequirementId '%s'."),
				*FieldPrefix,
				*Requirement.RequirementId.ToString()));
		}
		else
		{
			SeenZoneFeatureRequirementIds.Add(Requirement.RequirementId);
		}

		if (Requirement.RequiredFeatures.IsEmpty())
		{
			Result.AddError(FString::Printf(TEXT("%s must list at least one RequiredFeatures tag."), *FieldPrefix));
		}
		else
		{
			TArray<FGameplayTag> RequiredFeatureTags;
			Requirement.RequiredFeatures.GetGameplayTagArray(RequiredFeatureTags);
			for (const FGameplayTag& FeatureTag : RequiredFeatureTags)
			{
				if (IsLayoutFeatureTag(FeatureTag))
				{
					continue;
				}

				Result.AddError(BuildProfileValidationMessage(
					this,
					FString::Printf(TEXT("%s uses a non-layout feature tag."), *FieldPrefix),
					{
						FString::Printf(TEXT("RequirementId: %s"), Requirement.RequirementId.IsNone() ? TEXT("<none>") : *Requirement.RequirementId.ToString()),
						FString::Printf(TEXT("Feature tag: %s"), *FeatureTag.ToString()),
						TEXT("Problem: Zone feature requirements only accept tags under the Layout.Feature.* hierarchy."),
						TEXT("Fix: Rename this feature tag so it uses the Layout.Feature.* prefix, or remove it from RequiredFeatures.")
					}));
			}
		}

		if (Requirement.MinCount < 0)
		{
			Result.AddError(FString::Printf(TEXT("%s MinCount cannot be negative."), *FieldPrefix));
		}

		if (Requirement.MaxCount < 0)
		{
			Result.AddError(FString::Printf(TEXT("%s MaxCount cannot be negative."), *FieldPrefix));
		}

		if (Requirement.MaxCount > 0 && Requirement.MaxCount < Requirement.MinCount)
		{
			Result.AddError(FString::Printf(
				TEXT("%s MaxCount=%d cannot be less than MinCount=%d."),
				*FieldPrefix,
				Requirement.MaxCount,
				Requirement.MinCount));
		}

		if (Requirement.MinCount == 0 && Requirement.MaxCount == 0)
		{
			Result.AddWarning(FString::Printf(
				TEXT("%s currently accepts any count. This requirement will only report matches when later solve-time diagnostics are added for satisfied soft rules."),
				*FieldPrefix));
		}

		if (ContentSet == nullptr || Requirement.RequiredFeatures.IsEmpty())
		{
			continue;
		}

		TArray<FString> FeatureMatchingEntries;
		TArray<FString> ZoneCompatibleEntries;
		for (const FLayoutRegionContentEntry& Entry : ContentSet->Entries)
		{
			if (!DoesProvidedZoneFeatureSetMatchRequirement(Entry.ProvidedZoneFeatures, Requirement))
			{
				continue;
			}

			FeatureMatchingEntries.Add(Entry.EntryId.IsNone() ? TEXT("<none>") : Entry.EntryId.ToString());

			const bool bZoneCompatible = Entry.ContentKind == ELayoutRegionContentKind::Module
				? DoesPotentialPlacementZoneOverlapRequirementZone(
					Entry.ModuleSettings.PlacementZone,
					Requirement.Zone)
				: DoesPotentialPlacementZoneOverlapRequirementZone(
					Entry.ChildRegionSettings.PlacementZone,
					Requirement.Zone);
			if (bZoneCompatible)
			{
				ZoneCompatibleEntries.Add(Entry.EntryId.IsNone() ? TEXT("<none>") : Entry.EntryId.ToString());
			}
		}

		FeatureMatchingEntries.Sort();
		ZoneCompatibleEntries.Sort();
		const bool bCanEverRequireMatch = Requirement.MinCount > 0 || Requirement.MaxCount > 0;
		if (bCanEverRequireMatch && ZoneCompatibleEntries.IsEmpty())
		{
			const FString Summary = FString::Printf(
				TEXT("Zone feature requirement '%s' cannot be satisfied by ContentSet '%s'."),
				*Requirement.RequirementId.ToString(),
				*ContentSet->GetName());
			const FString Detail = FString::Printf(
				TEXT("Profile: %s\nZone: %s\nMatch mode: %s\nRequired features: %s\nExpected count: %s\nFeature-matching entries: %s\nZone-compatible entries: %s\nProblem: %s"),
				*GetName(),
				*ZoneName,
				*MatchModeName,
				*FormatZoneFeatureTags(Requirement.RequiredFeatures),
				*DescribeZoneFeatureCountExpectation(Requirement),
				FeatureMatchingEntries.IsEmpty() ? TEXT("<none>") : *FString::Join(FeatureMatchingEntries, TEXT(", ")),
				TEXT("<none>"),
				FeatureMatchingEntries.IsEmpty()
					? TEXT("No content entries provide the required zone feature tags.")
					: TEXT("Matching child-region entries only advertise placement zones that cannot plausibly satisfy this requirement zone."));
			Result.AddError(Summary + TEXT("\n") + Detail);
		}
	}

	TSet<FName> SeenClosureIds;
	for (int32 ClosureIndex = 0; ClosureIndex < ClosureRequirements.Num(); ++ClosureIndex)
	{
		const FLayoutClosureRequirement& ClosureRequirement = ClosureRequirements[ClosureIndex];
		const FString FieldPrefix = FString::Printf(TEXT("ClosureRequirements[%d]"), ClosureIndex);
		if (ClosureRequirement.ClosureId.IsNone())
		{
			Result.AddError(FString::Printf(TEXT("%s must define a non-empty ClosureId."), *FieldPrefix));
		}
		else if (SeenClosureIds.Contains(ClosureRequirement.ClosureId))
		{
			Result.AddError(FString::Printf(
				TEXT("%s duplicates ClosureId '%s'."),
				*FieldPrefix,
				*ClosureRequirement.ClosureId.ToString()));
		}
		else
		{
			SeenClosureIds.Add(ClosureRequirement.ClosureId);
		}

		if (!IsBoundaryLikePlacementZone(ClosureRequirement.Zone))
		{
			Result.AddWarning(FString::Printf(
				TEXT("%s uses Zone=%s. Perimeter, Edge, or Corner is usually expected for perimeter closure authoring."),
				*FieldPrefix,
				*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(ClosureRequirement.Zone))));
		}

		if (ClosureRequirement.MinThicknessCells <= 0)
		{
			Result.AddError(FString::Printf(TEXT("%s MinThicknessCells must be greater than zero."), *FieldPrefix));
		}

		ValidateBoundsPolicy(
			ClosureRequirement.BoundsPolicy,
			MaximumFootprintInCells,
			LevelCount,
			Result,
			FieldPrefix + TEXT(".BoundsPolicy"));
	}

	TSet<FName> SeenSparseRuleIds;
	TSet<FString> SeenPreserveScopeKeys;
	for (int32 SparseRuleIndex = 0; SparseRuleIndex < SparsePlacementRules.Num(); ++SparseRuleIndex)
	{
		const FInstancedStruct& InstancedRule = SparsePlacementRules[SparseRuleIndex];
		const FString FieldPrefix = FString::Printf(TEXT("SparsePlacementRules[%d]"), SparseRuleIndex);
		FResolvedSparseRuleAuthoringView SparseRule;
		if (!TryResolveSparseRuleAuthoringView(InstancedRule, SparseRule))
		{
			Result.AddError(FString::Printf(TEXT("%s must use a supported non-empty sparse rule type."), *FieldPrefix));
			continue;
		}

		if (SparseRule.Rule->RuleId.IsNone())
		{
			Result.AddError(FString::Printf(TEXT("%s must define a non-empty RuleId."), *FieldPrefix));
		}
		else if (SeenSparseRuleIds.Contains(SparseRule.Rule->RuleId))
		{
			Result.AddError(FString::Printf(TEXT("%s duplicates RuleId '%s'."), *FieldPrefix, *SparseRule.Rule->RuleId.ToString()));
		}
		else
		{
			SeenSparseRuleIds.Add(SparseRule.Rule->RuleId);
		}

		if (SparseRule.Rule->LevelPlacementPolicy == ELayoutLevelPlacementPolicy::SpecificLevel
			&& (SparseRule.Rule->SpecificLevel < 0 || SparseRule.Rule->SpecificLevel >= LevelCount))
		{
			Result.AddError(FString::Printf(TEXT("%s SpecificLevel must be between 0 and %d."), *FieldPrefix, FMath::Max(0, LevelCount - 1)));
		}

		const bool bPreservesTerrain = SparseRule.ContentRule == nullptr
			|| SparseRule.ContentRule->CandidateSource == ELayoutSparseCandidateSource::PreserveSupportedTerrain;
		if (bPreservesTerrain)
		{
			const FString ScopeKey = FString::Printf(
				TEXT("%d|%d|%d"),
				static_cast<int32>(SparseRule.Rule->PlacementZone),
				static_cast<int32>(SparseRule.Rule->LevelPlacementPolicy),
				SparseRule.Rule->SpecificLevel);
			if (SeenPreserveScopeKeys.Contains(ScopeKey))
			{
				Result.AddError(FString::Printf(TEXT("%s is fully shadowed by an earlier preserve-producing rule with the same zone and level scope."), *FieldPrefix));
			}
			else
			{
				SeenPreserveScopeKeys.Add(ScopeKey);
			}
		}

		if (const FLayoutSparseExactPlacementRule* ExactRule =
			InstancedRule.GetPtr<FLayoutSparseExactPlacementRule>();
			ExactRule != nullptr && ExactRule->Count <= 0)
		{
			Result.AddError(FString::Printf(TEXT("%s Count must be greater than zero."), *FieldPrefix));
		}
		if (const FLayoutSparseRangePlacementRule* RangeRule =
			InstancedRule.GetPtr<FLayoutSparseRangePlacementRule>();
			RangeRule != nullptr
			&& (RangeRule->MinCount < 0 || RangeRule->MaxCount < 0 || RangeRule->MinCount > RangeRule->MaxCount))
		{
			Result.AddError(FString::Printf(TEXT("%s range count must be non-negative and MinCount cannot exceed MaxCount."), *FieldPrefix));
		}

		if (SparseRule.ContentRule == nullptr)
		{
			continue;
		}
		if (SparseRule.ContentRule->ContentSet == nullptr)
		{
			Result.AddError(FString::Printf(TEXT("%s must reference a ContentSet."), *FieldPrefix));
			continue;
		}
		Result.Messages.Append(SparseRule.ContentRule->ContentSet->ValidateContentSet().Messages);
		if (SparseRule.ContentRule->MinSpacingCells < 0)
		{
			Result.AddError(FString::Printf(TEXT("%s MinSpacingCells cannot be negative."), *FieldPrefix));
		}

		const bool bHasModuleEntries = SparseRule.ContentRule->ContentSet->Entries.ContainsByPredicate([](const FLayoutRegionContentEntry& Entry)
		{
			return Entry.ContentKind == ELayoutRegionContentKind::Module
				&& ProfileModuleContentSettingsReferenceExactlyOneSource(Entry.ModuleSettings);
		});
		if (!bHasModuleEntries && SparseRule.MaximumCount > 0)
		{
			Result.AddError(FString::Printf(
				TEXT("%s requires at least one module-backed content entry because sparse placement only places modules."),
				*FieldPrefix));
		}
	}

	TSet<FName> SeenLevelFillRuleIds;
	for (int32 RuleIndex = 0; RuleIndex < LevelFillRules.Num(); ++RuleIndex)
	{
		const FLayoutLevelFillRule& Rule = LevelFillRules[RuleIndex];
		const FString FieldPrefix = FString::Printf(TEXT("LevelFillRules[%d]"), RuleIndex);
		if (Rule.RuleId.IsNone())
		{
			Result.AddError(FString::Printf(TEXT("%s must define a non-empty RuleId."), *FieldPrefix));
		}
		else if (SeenLevelFillRuleIds.Contains(Rule.RuleId))
		{
			Result.AddError(FString::Printf(TEXT("%s duplicates RuleId '%s'."), *FieldPrefix, *Rule.RuleId.ToString()));
		}
		else
		{
			SeenLevelFillRuleIds.Add(Rule.RuleId);
		}

		if (Rule.LevelPlacementPolicy == ELayoutLevelPlacementPolicy::SpecificLevel
			&& (Rule.SpecificLevel < 0 || Rule.SpecificLevel >= LevelCount))
		{
			Result.AddError(FString::Printf(TEXT("%s SpecificLevel must be between 0 and %d."), *FieldPrefix, FMath::Max(0, LevelCount - 1)));
		}
	}

	TSet<FName> SeenReservedOpenRuleIds;
	for (int32 RuleIndex = 0; RuleIndex < ReservedOpenSpaceRules.Num(); ++RuleIndex)
	{
		const FLayoutReservedOpenSpaceRule& Rule = ReservedOpenSpaceRules[RuleIndex];
		const FString FieldPrefix = FString::Printf(TEXT("ReservedOpenSpaceRules[%d]"), RuleIndex);
		if (Rule.RuleId.IsNone())
		{
			Result.AddError(FString::Printf(TEXT("%s must define a non-empty RuleId."), *FieldPrefix));
		}
		else if (SeenReservedOpenRuleIds.Contains(Rule.RuleId))
		{
			Result.AddError(FString::Printf(TEXT("%s duplicates RuleId '%s'."), *FieldPrefix, *Rule.RuleId.ToString()));
		}
		else
		{
			SeenReservedOpenRuleIds.Add(Rule.RuleId);
		}

		if (Rule.LevelPlacementPolicy == ELayoutLevelPlacementPolicy::SpecificLevel
			&& (Rule.SpecificLevel < 0 || Rule.SpecificLevel >= LevelCount))
		{
			Result.AddError(FString::Printf(TEXT("%s SpecificLevel must be between 0 and %d."), *FieldPrefix, FMath::Max(0, LevelCount - 1)));
		}
		if (Rule.ReservedPercent < 0.0f || Rule.ReservedPercent > 100.0f)
		{
			Result.AddError(FString::Printf(TEXT("%s ReservedPercent must stay within 0 to 100."), *FieldPrefix));
		}
		if (Rule.MinReservedCells < 0)
		{
			Result.AddError(FString::Printf(TEXT("%s MinReservedCells cannot be negative."), *FieldPrefix));
		}
		if (Rule.MaxReservedCells < 0)
		{
			Result.AddError(FString::Printf(TEXT("%s MaxReservedCells cannot be negative."), *FieldPrefix));
		}
		if (Rule.MaxReservedCells > 0 && Rule.MinReservedCells > Rule.MaxReservedCells)
		{
			Result.AddError(FString::Printf(TEXT("%s MinReservedCells cannot exceed MaxReservedCells when a maximum is authored."), *FieldPrefix));
		}
	}


	return Result;
}

#if WITH_EDITOR
void ULayoutProfileAsset::GeneratePerimeterClosureDraftInEditor()
{
	Modify();

	FLayoutClosureRequirement DraftRequirement;
	DraftRequirement.ClosureId = TEXT("OuterPerimeter");
	DraftRequirement.Zone = ELayoutPlacementZone::Perimeter;
	DraftRequirement.BoundsPolicy.Mode = ELayoutBoundsPolicyMode::SolvedFootprint;
	DraftRequirement.BoundsPolicy.InsetCells = 0;
	DraftRequirement.BoundsPolicy.MinLevel = 0;
	DraftRequirement.BoundsPolicy.MaxLevel = FMath::Max(0, LevelCount - 1);
	DraftRequirement.MinThicknessCells = 1;

	const int32 ExistingIndex = ClosureRequirements.IndexOfByPredicate([](const FLayoutClosureRequirement& Requirement)
	{
		return Requirement.ClosureId == TEXT("OuterPerimeter");
	});
	if (ExistingIndex != INDEX_NONE)
	{
		ClosureRequirements[ExistingIndex] = DraftRequirement;
	}
	else
	{
		ClosureRequirements.Add(DraftRequirement);
	}

	MarkPackageDirty();
}

void ULayoutProfileAsset::ValidateLayoutProfileInEditor() const
{
	PorismLayoutEditorMessageLog::ReportValidationResult(
		this,
		ValidateProfile(),
		TEXT("Layout profile validation passed."));
}

EDataValidationResult ULayoutProfileAsset::IsDataValid(FDataValidationContext& Context) const
{
	const FLayoutValidationResult ValidationResult = ValidateProfile();
	AppendProfileValidationResult(Context, ValidationResult);
	return ValidationResult.IsValid() ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
