// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Async/LayoutBackgroundSolveSnapshot.h"
#include "Layout/Async/LayoutWorkerSolvePacket.h"
#include "Layout/Contracts/LayoutContractManifestCache.h"

/** Worker-side request finalization helpers for Phase 1B packet migration. */
namespace LayoutWorkerSolveRequestFinalizer
{
	/**
	 * Finalizes one worker-safe solve request from the captured packet manifest.
	 * The worker owns request rehydration and then applies packet kind overrides
	 * such as certified child handoff commitments.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool FinalizeRequestFromPacket(
		const FLayoutWorkerSolvePacket& WorkerSolvePacket,
		FLayoutRegionSolveRequest& OutRequest,
		FString& OutFailureReason);

	/**
	 * Finalizes one worker-safe request and consumes the private static manifest cache.
	 * This helper is worker/finalization-side only; submit callers still only capture packet manifests.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool FinalizeRequestAndManifestFromPacket(
		const FLayoutWorkerSolvePacket& WorkerSolvePacket,
		FLayoutContractManifestCache& ManifestCache,
		FLayoutRegionSolveRequest& OutRequest,
		FLayoutContractManifestCacheEntry& OutManifestEntry,
		bool& bOutManifestCacheHit,
		FString& OutFailureReason);
}
