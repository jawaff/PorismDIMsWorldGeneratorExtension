// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Planning/LayoutPlanningWindowGraph.h"

class ULayoutPlanningWindowStore;

/** Thin orchestration seam for roots-first planning-window graph assembly. */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutPlanningWindowOrchestrator
{
public:
	/** Builds a graph from store records that already live inside the requested planning window. */
	static int32 BuildAcceptedRootGraphFromStore(
		const ULayoutPlanningWindowStore& Store,
		const FIntPoint& MinBlockXY,
		const FIntPoint& MaxBlockXY,
		FLayoutPlanningWindowGraph& OutGraph);

	/** Records one immediate fallback candidate through the same planning-store lifecycle. */
	static bool PublishImmediateFallbackPendingRecord(
		ULayoutPlanningWindowStore& Store,
		const FPlannedLayoutSiteRecord& PendingRecord,
		FPlannedLayoutSiteRecord& OutStoredRecord);
};
