// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChunkWorld/ChunkWorldBlockSwapTypes.h"
#include "ChunkWorldBlockSwapComponent.generated.h"

class AChunkWorldExtended;
class UBlockTypeSchemaComponent;
class UInstancedStaticMeshComponent;

/**
 * Shared replicated host for chunk-world block swap hide/restore transitions.
 * This lives in the plugin so projects can reuse the replicated block swap presentation path
 * together with the plugin-owned swap scanner/proximity runtime or with a custom orchestration layer.
 */
UCLASS(ClassGroup = (Block), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "ChunkWorld Block Swap Component"))
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldBlockSwapComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UChunkWorldBlockSwapComponent();

	/**
	 * Adds or removes one authoritative swap entry and applies the corresponding local parking or restore presentation state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Swap")
	bool TryApplySwapRequest(const FIntVector& BlockWorldPos, const FGameplayTag& BlockTypeName, bool bEntering);

	/** Returns the current world-space parking anchor used for hidden instanced meshes. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Swap")
	FVector GetParkingOrigin() const;

	/** Applies a project-chosen world-space offset from the owning chunk world for hidden swap parking. */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Swap")
	void SetParkingWorldOffset(const FVector& InParkingWorldOffset);

	/** Associates the authoritative replicated swap entry with the actor spawned for that swap. */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Swap")
	bool TrySetSwapActor(const FIntVector& BlockWorldPos, AActor* SwapActor);

	/** Resolves the represented mesh instance transform currently used for swap actor spawning. */
	bool TryGetSwapTransformForBlock(const FIntVector& BlockWorldPos, FTransform& OutSwapTransform) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	struct FResolvedSwapInstance
	{
		TWeakObjectPtr<UInstancedStaticMeshComponent> Component;
		int32 InstanceIndex = INDEX_NONE;
		int32 MeshId = INDEX_NONE;
		FTransform InstanceTransform = FTransform::Identity;
	};

	struct FLocalSwapPresentationState
	{
		TWeakObjectPtr<UInstancedStaticMeshComponent> Component;
		int32 InstanceIndex = INDEX_NONE;
		int32 MeshId = INDEX_NONE;
		FTransform OriginalTransform = FTransform::Identity;
		FTransform ParkedTransform = FTransform::Identity;
	};

	bool CanApplySwapRequest(const FIntVector& BlockWorldPos, bool bEntering) const;
	bool AddActiveSwap(const FIntVector& BlockWorldPos, const FGameplayTag& BlockTypeName);
	bool RemoveActiveSwap(const FIntVector& BlockWorldPos);
	bool ApplySwapPresentationState(const FReplicatedChunkWorldSwapItem& SwapItem, bool bEntering);
	bool ParkRepresentedInstance(const FReplicatedChunkWorldSwapItem& SwapItem);
	bool RestoreRepresentedInstance(const FReplicatedChunkWorldSwapItem& SwapItem);
	bool ResolveRepresentedInstance(const FIntVector& BlockWorldPos, FResolvedSwapInstance& OutResolvedInstance) const;
	FTransform BuildParkingTransform(const FIntVector& BlockWorldPos, const FTransform& OriginalTransform) const;
	FVector BuildParkingLocation(const FIntVector& BlockWorldPos) const;
	void ReconcileSwapActorPresentation(const FReplicatedChunkWorldSwapItem& SwapItem);
	void ReconcilePendingSwapActors();
	void UpdatePendingSwapActorReconcileTimer();
	void HandleReplicatedSwapAdded(const FReplicatedChunkWorldSwapItem& SwapItem);
	void HandleReplicatedSwapRemoved(const FReplicatedChunkWorldSwapItem& SwapItem);
	void HandleReplicatedSwapChanged(const FReplicatedChunkWorldSwapItem& SwapItem);
	AChunkWorldExtended* GetChunkWorldOwner() const;
	UBlockTypeSchemaComponent* GetSchemaComponent() const;

	/** Active authoritative swap entries replicated from the server. */
	UPROPERTY(Replicated)
	FReplicatedChunkWorldSwapArray ReplicatedActiveSwaps;

	/** Local presentation-only cache used to restore parked ISMC transforms without mutating world save data. */
	TMap<FIntVector, FLocalSwapPresentationState> LocalPresentationStates;

	/** Client/server reconciliation queue used when replicated actor references and parked mesh state arrive out of order. */
	TSet<FIntVector> PendingSwapActorReconcileBlocks;

	/** Timer used to retry actor visibility reconciliation when actor references resolve after the swap item itself. */
	FTimerHandle PendingSwapActorReconcileHandle;

	/** World-space offset from the owning chunk world used as the hidden parking anchor for swapped voxel instances. */
	UPROPERTY(EditAnywhere, Category = "Block|ChunkWorld|Swap", meta = (ToolTip = "World-space offset from the owning chunk world used as the hidden parking anchor for swapped voxel instances. Keep this far outside meaningful generated chunk space, especially for infinite-Z worlds."))
	FVector ParkingWorldOffset = FVector(0.0f, 0.0f, -100000.0f);

	/** World-space X/Y spacing used when deterministically spreading parked mesh instances away from each other. */
	UPROPERTY(EditAnywhere, Category = "Block|ChunkWorld|Swap", meta = (ClampMin = "1.0", UIMin = "1.0", ToolTip = "World-space X/Y spacing used when deterministically spreading parked mesh instances away from each other inside the hidden parking area."))
	float ParkingCellSize = 200.0f;

	/** Number of deterministic parking cells per axis before the parking pattern wraps. */
	UPROPERTY(EditAnywhere, Category = "Block|ChunkWorld|Swap", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Number of deterministic parking cells per axis before the hidden parking pattern wraps. Larger values reduce parking overlap at the cost of a wider hidden volume."))
	int32 ParkingCellsPerAxis = 64;

	/** World-space Z spacing used between deterministic parking layers. */
	UPROPERTY(EditAnywhere, Category = "Block|ChunkWorld|Swap", meta = (ClampMin = "1.0", UIMin = "1.0", ToolTip = "World-space Z spacing used between deterministic parking layers inside the hidden parking area."))
	float ParkingLayerHeight = 200.0f;

	friend struct FReplicatedChunkWorldSwapArray;
};
