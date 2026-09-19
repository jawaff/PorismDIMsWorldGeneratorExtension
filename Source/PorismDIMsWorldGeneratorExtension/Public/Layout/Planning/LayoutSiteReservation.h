// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Layout/Types/LayoutTypes.h"

/**
 * Lightweight helpers for caching deterministic reserved-site records without
 * forcing immediate chunk-world realization.
 */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSiteReservation
{
public:
	/** Exact block-world XY lookup key; spacing belongs to binding footprint reservations, not a second grid.
	 * Root publication IDs retain full identity, including Z and binding, where XY alone is ambiguous. */
	static FIntPoint ComputeReservationKey(const FIntVector& SiteCenterBlockWorldPos);

	/** Computes a stable solve seed for one reserved site. */
	static int32 ComputeSiteSolveSeed(const FIntVector& SiteCenterBlockWorldPos, int32 WorldSeed);

	/** Copies producer-owned root identity and source selections into a site carrier.
	 * RootSolveId must be assigned by the producer; cache admission rejects missing identity without synthesizing a key. */
	static FResolvedLayoutSiteRecord BuildResolvedSiteRecord(
		const FResolvedLayoutSiteLocationMetadata& LocationMetadata,
		const FLayoutSiteSolveSourceSelection& SolveSourceSelection,
		const FLayoutWorldBindingSiteFrontendSelection* FrontendSelection,
		const FLayoutRootPublicationMetadata& RootPublicationMetadata,
		const FLayoutSolveResult* SolveResult);
};
