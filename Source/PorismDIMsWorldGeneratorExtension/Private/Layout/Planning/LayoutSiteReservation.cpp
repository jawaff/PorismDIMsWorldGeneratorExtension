// Copyright 2026 Spotted Loaf Studio

#include "Layout/Planning/LayoutSiteReservation.h"

FIntPoint FLayoutSiteReservation::ComputeReservationKey(const FIntVector& SiteCenterBlockWorldPos)
{
	return FIntPoint(SiteCenterBlockWorldPos.X, SiteCenterBlockWorldPos.Y);
}

int32 FLayoutSiteReservation::ComputeSiteSolveSeed(const FIntVector& SiteCenterBlockWorldPos, const int32 WorldSeed)
{
	return static_cast<int32>(HashCombine(
		HashCombine(GetTypeHash(SiteCenterBlockWorldPos.X), GetTypeHash(SiteCenterBlockWorldPos.Y)),
		HashCombine(GetTypeHash(SiteCenterBlockWorldPos.Z), static_cast<uint32>(WorldSeed))));
}

FResolvedLayoutSiteRecord FLayoutSiteReservation::BuildResolvedSiteRecord(
	const FResolvedLayoutSiteLocationMetadata& LocationMetadata,
	const FLayoutSiteSolveSourceSelection& SolveSourceSelection,
	const FLayoutWorldBindingSiteFrontendSelection* FrontendSelection,
	const FLayoutRootPublicationMetadata& RootPublicationMetadata,
	const FLayoutSolveResult* SolveResult)
{
	FResolvedLayoutSiteRecord Record;
	Record.SetResolvedSiteLocationMetadata(LocationMetadata);
	Record.SetSiteSolveSourceSelection(SolveSourceSelection);
	if (FrontendSelection != nullptr)
	{
		Record.SetWorldBindingFrontendSelection(*FrontendSelection);
	}
	Record.SetRootPublicationMetadata(RootPublicationMetadata);

	if (SolveResult != nullptr)
	{
		FResolvedLayoutSiteRuntimeState RuntimeState =
			Record.GetResolvedSiteRuntimeState();
		RuntimeState.bLayoutSolved = SolveResult->bSucceeded;
		RuntimeState.CachedApplyability = SolveResult->bSucceeded
			? ELayoutCachedApplyability::Solved
			: ELayoutCachedApplyability::Rejected;
		Record.SetResolvedSiteRuntimeState(RuntimeState);
		FResolvedLayoutSiteSolvedPayload SolvedPayload =
			Record.GetResolvedSiteSolvedPayload();
		SolvedPayload.SolveResult = *SolveResult;
		SolvedPayload.ExportedEntryCells = SolveResult->ExportedEntryCells;
		SolvedPayload.ExportedConnectorTypeTags =
			SolveSourceSelection.ExportedConnectorTypeTags;
		Record.SetResolvedSiteSolvedPayload(SolvedPayload);
	}

	return Record;
}
