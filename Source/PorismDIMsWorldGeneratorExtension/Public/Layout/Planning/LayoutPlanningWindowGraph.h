// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Contracts/LayoutContractTypes.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"

#include "LayoutPlanningWindowGraph.generated.h"

/** Accepted or rejected root node visible to one planning-window pass. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutPlanningWindowRootNode
{
	GENERATED_BODY()

	/** Stable planned-site record key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable planned-site record key."))
	FString StableRecordKey;

	/** Planned-site record currently represented by this graph node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Planned-site record currently represented by this graph node."))
	FPlannedLayoutSiteRecord SiteRecord;

	/** Frozen terrain contract associated with the accepted root when available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Frozen terrain contract associated with the accepted root when available."))
	FLayoutFrozenTerrainContract FrozenTerrainContract;
};

/** Continuation edge candidate or accepted connector between two accepted root nodes. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutPlanningWindowContinuationEdge
{
	GENERATED_BODY()

	/** Stable continuation-edge identity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable continuation-edge identity."))
	FString StableEdgeKey;

	/** Stable record key of the start accepted root. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable record key of the start accepted root."))
	FString StartRootRecordKey;

	/** Stable record key of the end accepted root. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable record key of the end accepted root."))
	FString EndRootRecordKey;

	/** Continuation family being evaluated for this edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Continuation family being evaluated for this edge."))
	ELayoutWorldBindingContinuationFamilyType FamilyType = ELayoutWorldBindingContinuationFamilyType::SurfacePath;

	/** Lifecycle state of this continuation solve candidate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Lifecycle state of this continuation solve candidate."))
	EPlannedLayoutSiteState State = EPlannedLayoutSiteState::Pending;

	/** Solved connector payload when this edge has been accepted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Solved connector payload when this edge has been accepted."))
	FResolvedLayoutConnectorRecord ConnectorRecord;

	/** Frozen terrain contract associated with this accepted continuation when available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Frozen terrain contract associated with this accepted continuation when available."))
	FLayoutFrozenTerrainContract FrozenTerrainContract;

	/** Stable rejection reason when this continuation candidate fails. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable rejection reason when this continuation candidate fails."))
	FString RejectionReason;
};

/** Roots-first planning-window graph used before chunk-gated realization. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutPlanningWindowGraph
{
	GENERATED_BODY()

	/** Inclusive minimum block XY covered by this graph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Inclusive minimum block XY covered by this graph."))
	FIntPoint MinBlockXY = FIntPoint::ZeroValue;

	/** Inclusive maximum block XY covered by this graph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Inclusive maximum block XY covered by this graph."))
	FIntPoint MaxBlockXY = FIntPoint::ZeroValue;

	/** Root nodes sampled, accepted, or rejected inside this planning window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Root nodes sampled, accepted, or rejected inside this planning window."))
	TArray<FLayoutPlanningWindowRootNode> RootNodes;

	/** Continuation edges generated only between accepted roots inside this planning window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Continuation edges generated only between accepted roots inside this planning window."))
	TArray<FLayoutPlanningWindowContinuationEdge> ContinuationEdges;

	/** Clears all nodes and edges while preserving window bounds. */
	void ResetRecords();

	/** Returns true when one block XY lies inside this planning window. */
	bool ContainsBlockXY(const FIntPoint& BlockXY) const;

	/** Adds one root node to this graph and returns the inserted index. */
	int32 AddRootNode(const FPlannedLayoutSiteRecord& SiteRecord, const FLayoutFrozenTerrainContract* FrozenTerrainContract = nullptr);

	/** Adds one continuation edge to this graph and returns the inserted index. */
	int32 AddContinuationEdge(const FLayoutPlanningWindowContinuationEdge& Edge);

	/** Counts accepted root nodes currently inside this graph. */
	int32 CountAcceptedRoots() const;
};
