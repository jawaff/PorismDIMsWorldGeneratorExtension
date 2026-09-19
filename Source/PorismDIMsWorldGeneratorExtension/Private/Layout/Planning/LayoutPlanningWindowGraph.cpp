// Copyright 2026 Spotted Loaf Studio

#include "Layout/Planning/LayoutPlanningWindowGraph.h"

void FLayoutPlanningWindowGraph::ResetRecords()
{
	RootNodes.Reset();
	ContinuationEdges.Reset();
}

bool FLayoutPlanningWindowGraph::ContainsBlockXY(const FIntPoint& BlockXY) const
{
	return BlockXY.X >= MinBlockXY.X
		&& BlockXY.X <= MaxBlockXY.X
		&& BlockXY.Y >= MinBlockXY.Y
		&& BlockXY.Y <= MaxBlockXY.Y;
}

int32 FLayoutPlanningWindowGraph::AddRootNode(
	const FPlannedLayoutSiteRecord& SiteRecord,
	const FLayoutFrozenTerrainContract* const FrozenTerrainContract)
{
	FLayoutPlanningWindowRootNode& Node = RootNodes.AddDefaulted_GetRef();
	Node.StableRecordKey = SiteRecord.GetPlannedSiteLifecycleMetadata().StableRecordKey;
	Node.SiteRecord = SiteRecord;
	if (FrozenTerrainContract != nullptr)
	{
		Node.FrozenTerrainContract = *FrozenTerrainContract;
	}
	return RootNodes.Num() - 1;
}

int32 FLayoutPlanningWindowGraph::AddContinuationEdge(const FLayoutPlanningWindowContinuationEdge& Edge)
{
	ContinuationEdges.Add(Edge);
	return ContinuationEdges.Num() - 1;
}

int32 FLayoutPlanningWindowGraph::CountAcceptedRoots() const
{
	int32 AcceptedCount = 0;
	for (const FLayoutPlanningWindowRootNode& Node : RootNodes)
	{
		const EPlannedLayoutSiteState State = Node.SiteRecord.GetPlannedSiteLifecycleMetadata().State;
		if (State == EPlannedLayoutSiteState::Accepted || State == EPlannedLayoutSiteState::Realized)
		{
			++AcceptedCount;
		}
	}
	return AcceptedCount;
}
