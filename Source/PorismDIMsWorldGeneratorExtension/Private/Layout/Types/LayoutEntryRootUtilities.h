// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutGameplayTags.h"
#include "Layout/Types/LayoutTypes.h"

/** Shared helpers for validating and solving authored entry-root contracts. */
namespace LayoutEntryRootUtilities
{
	inline bool FaceAllowsEmptyNeighbor(const FLayoutFaceRule& FaceRule)
	{
		return FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor;
	}

	inline bool FaceConnectsWalkableArea(const FLayoutFaceRule& FaceRule, const FGameplayTag& WalkableArea)
	{
		return WalkableArea.IsValid() && FaceRule.ConnectedTraversalChannels.HasTagExact(WalkableArea);
	}

	inline bool FaceProvidesExplicitEntry(const FLayoutFaceRule& FaceRule)
	{
		return FaceRule.GetEffectiveConnectionTags().HasTagExact(LayoutGameplayTags::FaceEntry)
			&& !FaceRule.ConnectedTraversalChannels.IsEmpty();
	}

	inline bool CanTraversalChannelReachTarget(
		const FGameplayTag& SourceTraversalChannel,
		const FGameplayTag& TargetTraversalChannel,
		const TArray<FLayoutInternalAccessLink>& InternalAccessLinks)
	{
		if (!SourceTraversalChannel.IsValid() || !TargetTraversalChannel.IsValid())
		{
			return false;
		}

		if (SourceTraversalChannel == TargetTraversalChannel)
		{
			return true;
		}

		TSet<FGameplayTag> VisitedChannels;
		TArray<FGameplayTag> PendingChannels;
		PendingChannels.Add(SourceTraversalChannel);

		while (!PendingChannels.IsEmpty())
		{
			const FGameplayTag CurrentTraversalChannel = PendingChannels.Pop(EAllowShrinking::No);
			if (CurrentTraversalChannel == TargetTraversalChannel)
			{
				return true;
			}

			if (VisitedChannels.Contains(CurrentTraversalChannel))
			{
				continue;
			}

			VisitedChannels.Add(CurrentTraversalChannel);
			for (const FLayoutInternalAccessLink& Link : InternalAccessLinks)
			{
				if (Link.FromTraversalChannel == CurrentTraversalChannel && Link.ToTraversalChannel.IsValid())
				{
					PendingChannels.Add(Link.ToTraversalChannel);
				}

				if (Link.bBidirectional
					&& Link.ToTraversalChannel == CurrentTraversalChannel
					&& Link.FromTraversalChannel.IsValid())
				{
					PendingChannels.Add(Link.FromTraversalChannel);
				}
			}
		}

		return false;
	}

	inline bool HasAuthoredEntryRoot(
		const TArray<FLayoutFaceRule>& FaceRules,
		const TArray<FLayoutInternalAccessLink>& InternalAccessLinks)
	{
		TArray<const FLayoutFaceRule*> ExplicitEntryFaces;
		for (const FLayoutFaceRule& FaceRule : FaceRules)
		{
			if (FaceProvidesExplicitEntry(FaceRule))
			{
				ExplicitEntryFaces.Add(&FaceRule);
			}
		}

		if (ExplicitEntryFaces.IsEmpty())
		{
			return false;
		}

		for (const FLayoutFaceRule& FaceRule : FaceRules)
		{
			if (!FaceAllowsEmptyNeighbor(FaceRule) || FaceRule.ConnectedTraversalChannels.IsEmpty())
			{
				continue;
			}

			if (FaceProvidesExplicitEntry(FaceRule))
			{
				return true;
			}

			for (const FGameplayTag& ExteriorTraversalChannel : FaceRule.ConnectedTraversalChannels)
			{
				for (const FLayoutFaceRule* ExplicitEntryFace : ExplicitEntryFaces)
				{
					for (const FGameplayTag& EntryTraversalChannel : ExplicitEntryFace->ConnectedTraversalChannels)
					{
						if (CanTraversalChannelReachTarget(
							ExteriorTraversalChannel,
							EntryTraversalChannel,
							InternalAccessLinks))
						{
							return true;
						}
					}
				}
			}
		}

		return false;
	}

	inline bool CanReachWalkableAreaFromEntryRoot(
		const TArray<FLayoutFaceRule>& FaceRules,
		const TArray<FLayoutInternalAccessLink>& InternalAccessLinks,
		const TArray<ELayoutFaceDirection>& ExteriorDirections,
		const FGameplayTag& WalkableArea)
	{
		if (!WalkableArea.IsValid())
		{
			return false;
		}

		TArray<const FLayoutFaceRule*> ExplicitEntryFacesForWalkableArea;
		for (const FLayoutFaceRule& FaceRule : FaceRules)
		{
			if (FaceProvidesExplicitEntry(FaceRule)
				&& FaceConnectsWalkableArea(FaceRule, WalkableArea))
			{
				ExplicitEntryFacesForWalkableArea.Add(&FaceRule);
			}
		}

		if (ExplicitEntryFacesForWalkableArea.IsEmpty())
		{
			return false;
		}

		for (const ELayoutFaceDirection ExteriorDirection : ExteriorDirections)
		{
			const FLayoutFaceRule* ExteriorFaceRule = FaceRules.FindByPredicate([ExteriorDirection](const FLayoutFaceRule& FaceRule)
			{
				return FaceRule.Direction == ExteriorDirection;
			});
			if (ExteriorFaceRule == nullptr
				|| !FaceAllowsEmptyNeighbor(*ExteriorFaceRule)
				|| ExteriorFaceRule->ConnectedTraversalChannels.IsEmpty())
			{
				continue;
			}

			if (FaceProvidesExplicitEntry(*ExteriorFaceRule)
				&& FaceConnectsWalkableArea(*ExteriorFaceRule, WalkableArea))
			{
				return true;
			}

			for (const FGameplayTag& ExteriorTraversalChannel : ExteriorFaceRule->ConnectedTraversalChannels)
			{
				if (CanTraversalChannelReachTarget(
					ExteriorTraversalChannel,
					WalkableArea,
					InternalAccessLinks))
				{
					return true;
				}
			}
		}

		return false;
	}
}
