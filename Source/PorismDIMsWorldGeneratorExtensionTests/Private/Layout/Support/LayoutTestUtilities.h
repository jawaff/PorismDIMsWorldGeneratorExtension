// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "ChunkWorldStructs/ChunkStructureTemplate.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"

namespace PorismLayoutTestUtilities
{
	inline ELayoutModuleRole ConvertIntentToRole(const ELayoutCellIntent Intent)
	{
		switch (Intent)
		{
		case ELayoutCellIntent::Boundary:
			return ELayoutModuleRole::Boundary;
		case ELayoutCellIntent::Entry:
			return ELayoutModuleRole::Entry;
		case ELayoutCellIntent::Core:
			return ELayoutModuleRole::Interior;
		case ELayoutCellIntent::Interior:
			return ELayoutModuleRole::Interior;
		case ELayoutCellIntent::Connector:
			return ELayoutModuleRole::Interior;
		case ELayoutCellIntent::VerticalAccess:
			return ELayoutModuleRole::VerticalAccess;
		default:
			return ELayoutModuleRole::Interior;
		}
	}

	inline FGameplayTagContainer MakeTags(std::initializer_list<FGameplayTag> Tags)
	{
		FGameplayTagContainer Container;
		for (const FGameplayTag& Tag : Tags)
		{
			Container.AddTag(Tag);
		}

		return Container;
	}

	/** Returns true when any validation message contains the expected diagnostic substring. */
	inline bool ContainsValidationMessageSubstring(
		const TArray<FLayoutValidationMessage>& Messages,
		const FString& ExpectedSubstring)
	{
		return Messages.ContainsByPredicate(
			[&ExpectedSubstring](const FLayoutValidationMessage& Message)
			{
				return Message.Message.Contains(ExpectedSubstring);
			});
	}

	inline FLayoutFaceRule MakeFaceRule(
		const ELayoutFaceDirection Direction,
		const FGameplayTagContainer& ConnectionTags,
		const FGameplayTagContainer& AllowedConnectionTags,
		const ELayoutFaceOccupancyPolicy OccupancyPolicy,
		const FGameplayTagContainer& ConnectedTraversalChannels = FGameplayTagContainer())
	{
		FLayoutFaceRule Rule;
		Rule.Direction = Direction;
		TArray<FGameplayTag> SortedConnectionTags;
		ConnectionTags.GetGameplayTagArray(SortedConnectionTags);
		SortedConnectionTags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
		{
			return Left.ToString() < Right.ToString();
		});
		Rule.ConnectionTag = SortedConnectionTags.IsEmpty() ? FGameplayTag() : SortedConnectionTags[0];
		Rule.AllowedConnectionTags = AllowedConnectionTags;
		Rule.OccupancyPolicy = OccupancyPolicy;
		Rule.ConnectedTraversalChannels = ConnectedTraversalChannels;
		// BoundaryRequirement is NOT auto-set. Tests and production modules must
		// explicitly set it on faces that require region-boundary placement.
		// Use occupancy policy (AllowsEmptyOrFilledNeighbor, AllowsAnyNeighbor, etc.)
		// for optional boundary support instead.
		return Rule;
	}

	inline FLayoutFaceRule MakeConnectionFaceRule(
		const ELayoutFaceDirection Direction,
		const FGameplayTag& ConnectionTag,
		const FGameplayTagContainer& AllowedConnectionTags,
		const ELayoutFaceOccupancyPolicy OccupancyPolicy,
		const FGameplayTagContainer& ConnectedTraversalChannels = FGameplayTagContainer())
	{
		FLayoutFaceRule Rule;
		Rule.Direction = Direction;
		Rule.ConnectionTag = ConnectionTag;
		Rule.AllowedConnectionTags = AllowedConnectionTags;
		Rule.OccupancyPolicy = OccupancyPolicy;
		Rule.ConnectedTraversalChannels = ConnectedTraversalChannels;
		// BoundaryRequirement is NOT auto-set. Tests and production modules must
		// explicitly set it on faces that require region-boundary placement.
		// Use occupancy policy for optional boundary support instead.
		return Rule;
	}

	inline UChunkStructureTemplate* CreateTemplate(UObject* Outer, const TCHAR* Name, const FIntVector SizeInBlocks)
	{
		UChunkStructureTemplate* Template = NewObject<UChunkStructureTemplate>(Outer, FName(Name));
		Template->SizeInBlocks = SizeInBlocks;
		return Template;
	}

	inline void SetModuleTemplateSize(
		ULayoutModuleAsset* Module,
		const FIntVector& TemplateSizeInBlocks)
	{
		if (Module == nullptr)
		{
			return;
		}

		FIntVector TemplateSize = TemplateSizeInBlocks;
		if (TemplateSize.X <= 0 || TemplateSize.Y <= 0 || TemplateSize.Z <= 0)
		{
			TemplateSize = FIntVector(16, 16, 16);
		}

		UChunkStructureTemplate* Template = Module->Template.Get();
		if (Template == nullptr)
		{
			Template = NewObject<UChunkStructureTemplate>(
				Module,
				MakeUniqueObjectName(Module, UChunkStructureTemplate::StaticClass(), FName(*(Module->GetName() + TEXT("_Template")))));
			Module->Template = Template;
		}

		Template->SizeInBlocks = TemplateSize;
	}

	inline ULayoutModuleAsset* CreateModule(
		UObject* Outer,
		const TCHAR* Name,
		UChunkStructureTemplate* Template,
		const TArray<ELayoutCellIntent>& SupportedIntents,
		const TArray<FLayoutFaceRule>& FaceRules,
		const FGameplayTagContainer& TraversalChannels = FGameplayTagContainer(),
		const TArray<FLayoutInternalAccessLink>& InternalAccessLinks = {},
		const FGameplayTagContainer& RoleTags = FGameplayTagContainer())
	{
		(void)TraversalChannels;
		(void)RoleTags;
		ULayoutModuleAsset* Module = NewObject<ULayoutModuleAsset>(Outer, FName(Name));
		Module->Template = Template;
		SetModuleTemplateSize(
			Module,
			Template != nullptr ? Template->SizeInBlocks : FIntVector(16, 16, 16));
		for (const ELayoutCellIntent Intent : SupportedIntents)
		{
			Module->Roles.AddUnique(ConvertIntentToRole(Intent));
		}
		for (const FLayoutFaceRule& FaceRule : FaceRules)
		{
			Module->FaceRules.SetRule(FaceRule);
		}
		if (FLayoutFaceRule* PosZRule = Module->FaceRules.FindRule(ELayoutFaceDirection::PosZ))
		{
			if (Module->Roles.Contains(ELayoutModuleRole::VerticalAccess) && PosZRule->ConnectedTraversalChannels.IsEmpty())
			{
				PosZRule->ConnectedTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
			}
		}
		Module->InternalAccessLinks = InternalAccessLinks;
		return Module;
	}

	inline ULayoutRegionContentSetAsset* CreateRegionContentSet(
		UObject* Outer,
		const TCHAR* Name,
		const TArray<FLayoutRegionContentEntry>& Entries)
	{
		ULayoutRegionContentSetAsset* ContentSet = NewObject<ULayoutRegionContentSetAsset>(Outer, FName(Name));
		ContentSet->Entries = Entries;
		return ContentSet;
	}

	inline ULayoutProfileAsset* CreateProfile(
		UObject* Outer,
		const TCHAR* Name,
		const FIntPoint MinimumFootprint,
		const FIntPoint MaximumFootprint,
		const int32 LevelCount,
		const int32 InEntryCount,
		const bool bUpperLevelsUseBoundaryOnly,
		const bool bAllowEmptyInteriorCells = false,
		const int32 MaxEmptyCellCount = 0)
	{
		ULayoutProfileAsset* Profile = NewObject<ULayoutProfileAsset>(Outer, FName(Name));
		Profile->MinimumFootprintInCells = MinimumFootprint;
		Profile->MaximumFootprintInCells = MaximumFootprint;
		Profile->LevelCount = LevelCount;
		Profile->EntryCountMode = InEntryCount > 0 ? ELayoutCountConstraintMode::Exact : ELayoutCountConstraintMode::None;
		Profile->EntryCount = FMath::Max(1, InEntryCount);
		Profile->MinEntryCount = FMath::Max(0, InEntryCount);
		Profile->MaxEntryCount = FMath::Max(0, InEntryCount);
		if (bUpperLevelsUseBoundaryOnly)
		{
			FLayoutLevelFillRule& Rule = Profile->LevelFillRules.AddDefaulted_GetRef();
			Rule.RuleId = TEXT("UpperBoundaryOnly");
			Rule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AboveGroundLevel;
			Rule.FillMode = ELayoutLevelFillMode::BoundaryOnly;
		}
		return Profile;
	}


	inline FLayoutSolverExecutionSettings MakeSolverExecutionSettings(
		const int32 MaxCandidateAttempts = 50000,
		const float MaxSolveDurationSeconds = 2.0f,
		const int32 MaxFailureDetails = 24,
		const ELayoutSolverTraceMode TraceMode = ELayoutSolverTraceMode::Disabled,
		const int32 MaxTraceEvents = 512,
		const bool bIncludeTraceCandidateDetails = false)
	{
		FLayoutSolverExecutionSettings Settings;
		Settings.MaxCandidateAttempts = MaxCandidateAttempts;
		Settings.MaxSolveDurationSeconds = MaxSolveDurationSeconds;
		Settings.MaxFailureDetails = MaxFailureDetails;
		Settings.TraceMode = TraceMode;
		Settings.MaxTraceEvents = MaxTraceEvents;
		Settings.bIncludeTraceCandidateDetails = bIncludeTraceCandidateDetails;
		return Settings;
	}

	inline TArray<FLayoutFaceRule> BuildFilledCubeFaces(
		const FGameplayTagContainer& SideConnectionTags,
		const FGameplayTagContainer& SideAllowedTags,
		const ELayoutFaceOccupancyPolicy SideOccupancyPolicy,
		const FGameplayTagContainer& VerticalConnectionTags,
		const FGameplayTagContainer& VerticalAllowedTags,
		const ELayoutFaceOccupancyPolicy VerticalOccupancyPolicy,
		const FGameplayTagContainer& HorizontalTraversalChannels = FGameplayTagContainer(),
		const FGameplayTagContainer& BottomTraversalChannels = FGameplayTagContainer(),
		const FGameplayTagContainer& TopTraversalChannels = FGameplayTagContainer())
	{
		return {
			MakeFaceRule(ELayoutFaceDirection::PosX, SideConnectionTags, SideAllowedTags, SideOccupancyPolicy, HorizontalTraversalChannels),
			MakeFaceRule(ELayoutFaceDirection::NegX, SideConnectionTags, SideAllowedTags, SideOccupancyPolicy, HorizontalTraversalChannels),
			MakeFaceRule(ELayoutFaceDirection::PosY, SideConnectionTags, SideAllowedTags, SideOccupancyPolicy, HorizontalTraversalChannels),
			MakeFaceRule(ELayoutFaceDirection::NegY, SideConnectionTags, SideAllowedTags, SideOccupancyPolicy, HorizontalTraversalChannels),
			MakeFaceRule(ELayoutFaceDirection::PosZ, VerticalConnectionTags, VerticalAllowedTags, VerticalOccupancyPolicy, TopTraversalChannels),
			MakeFaceRule(ELayoutFaceDirection::NegZ, VerticalConnectionTags, VerticalAllowedTags, VerticalOccupancyPolicy, BottomTraversalChannels)
		};
	}

	/** Attaches generic, Entry, and vertical-access entries so legacy profile-solve fixtures use profile-owned content authority. */
	inline ULayoutProfileAsset* CreateProfileWithUniversalContentSet(
		UObject* Outer,
		const TCHAR* Name,
		const FIntPoint MinimumFootprint,
		const FIntPoint MaximumFootprint,
		const int32 LevelCount,
		const int32 InEntryCount,
		const bool bUpperLevelsUseBoundaryOnly,
		const bool bAllowEmptyInteriorCells = false,
		const int32 MaxEmptyCellCount = 0)
	{
		ULayoutProfileAsset* Profile = CreateProfile(
			Outer,
			Name,
			MinimumFootprint,
			MaximumFootprint,
			LevelCount,
			InEntryCount,
			bUpperLevelsUseBoundaryOnly,
			bAllowEmptyInteriorCells,
			MaxEmptyCellCount);
		const FString NamePrefix(Name);
		UChunkStructureTemplate* Template = CreateTemplate(
			Outer,
			*(NamePrefix + TEXT("_Template")),
			FIntVector(16, 16, 16));
		ULayoutModuleAsset* GenericModule = CreateModule(
			Outer,
			*(NamePrefix + TEXT("_GenericModule")),
			Template,
			{
				ELayoutCellIntent::Boundary,
				ELayoutCellIntent::Entry,
				ELayoutCellIntent::Core,
				ELayoutCellIntent::Interior
			},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})));
		ULayoutModuleAsset* EntryModule = CreateModule(
			Outer,
			*(NamePrefix + TEXT("_EntryModule")),
			Template,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Entry},
			{
				MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
				MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
			});
		ULayoutModuleAsset* VerticalAccessModule = CreateModule(
			Outer,
			*(NamePrefix + TEXT("_VerticalAccessModule")),
			Template,
			{ELayoutCellIntent::VerticalAccess},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary}),
				MakeTags({LayoutGameplayTags::TraversalPrimary}),
				MakeTags({LayoutGameplayTags::TraversalPrimary})));
		FLayoutRegionContentEntry GenericEntry;
		GenericEntry.EntryId = FName(*(NamePrefix + TEXT("_GenericEntry")));
		GenericEntry.ContentKind = ELayoutRegionContentKind::Module;
		GenericEntry.ModuleSettings.Module = GenericModule;
		FLayoutRegionContentEntry EntryEntry;
		EntryEntry.EntryId = FName(*(NamePrefix + TEXT("_EntryEntry")));
		EntryEntry.ContentKind = ELayoutRegionContentKind::Module;
		EntryEntry.ModuleSettings.Module = EntryModule;
		FLayoutRegionContentEntry VerticalAccessEntry;
		VerticalAccessEntry.EntryId = FName(*(NamePrefix + TEXT("_VerticalAccessEntry")));
		VerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
		VerticalAccessEntry.ModuleSettings.Module = VerticalAccessModule;
		Profile->ContentSet = CreateRegionContentSet(
			Outer,
			*(NamePrefix + TEXT("_ContentSet")),
			{GenericEntry, EntryEntry, VerticalAccessEntry});
		return Profile;
	}
}
