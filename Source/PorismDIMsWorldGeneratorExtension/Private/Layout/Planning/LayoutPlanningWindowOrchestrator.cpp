// Copyright 2026 Spotted Loaf Studio

#include "Layout/Planning/LayoutPlanningWindowOrchestrator.h"

#include "Layout/Planning/LayoutPlanningWindowStore.h"

int32 FLayoutPlanningWindowOrchestrator::BuildAcceptedRootGraphFromStore(
	const ULayoutPlanningWindowStore& Store,
	const FIntPoint& MinBlockXY,
	const FIntPoint& MaxBlockXY,
	FLayoutPlanningWindowGraph& OutGraph)
{
	OutGraph.MinBlockXY = MinBlockXY;
	OutGraph.MaxBlockXY = MaxBlockXY;
	OutGraph.ResetRecords();

	for (const FPlannedLayoutSiteRecord& Record : Store.GetPlannedLayoutSiteRecords())
	{
		const FLayoutPlannedSiteLifecycleMetadata Lifecycle =
			Record.GetPlannedSiteLifecycleMetadata();
		if (Lifecycle.State != EPlannedLayoutSiteState::Accepted
			&& Lifecycle.State != EPlannedLayoutSiteState::Realized)
		{
			continue;
		}

		const FLayoutPlannedSiteReservationSourceSelection Reservation =
			Record.GetPlannedSiteReservationSourceSelection();
		if (!OutGraph.ContainsBlockXY(FIntPoint(
				Reservation.SiteCenterBlockWorldPos.X,
				Reservation.SiteCenterBlockWorldPos.Y)))
		{
			continue;
		}

		const FLayoutPlannedSiteAcceptedSolvePayload AcceptedSolvePayload =
			Record.GetPlannedSiteAcceptedSolvePayload();
		OutGraph.AddRootNode(
			Record,
			AcceptedSolvePayload.FrozenTerrainContract.ContractId.IsNone()
				? nullptr
				: &AcceptedSolvePayload.FrozenTerrainContract);
	}

	return OutGraph.CountAcceptedRoots();
}

bool FLayoutPlanningWindowOrchestrator::PublishImmediateFallbackPendingRecord(
	ULayoutPlanningWindowStore& Store,
	const FPlannedLayoutSiteRecord& PendingRecord,
	FPlannedLayoutSiteRecord& OutStoredRecord)
{
	// Immediate fallback must publish through store lifecycle so it cannot drift
	// from normal presolve rejection, acceptance, and realization ownership.
	return Store.UpsertPendingPlannedLayoutSiteRecord(PendingRecord, OutStoredRecord);
}
