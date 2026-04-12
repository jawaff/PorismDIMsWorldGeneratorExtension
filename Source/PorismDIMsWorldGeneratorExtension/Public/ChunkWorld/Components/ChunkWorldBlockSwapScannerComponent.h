// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Block/BlockTypeSchemaRegistry.h"
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChunkWorld/ChunkWorldBlockSwapTypes.h"
#include "UObject/SoftObjectPath.h"
#include "ChunkWorldBlockSwapScannerComponent.generated.h"

class AChunkWorld;
class AActor;
class UChunkWorldProximityComponent;
struct FChunkWorldResolvedBlockHit;
struct FStreamableHandle;

/**
 * Shared server-side chunk-world swap scanner that turns nearby schema-authored blocks into actor presentation.
 */
UCLASS(ClassGroup = (Block), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "ChunkWorld Block Swap Scanner Component"))
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldBlockSwapScannerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Creates a non-ticking component that scans around registered proximity sources on a timer. */
	UChunkWorldBlockSwapScannerComponent();

	/** Registers a proximity source that should drive swap scanning for this chunk world. */
	void RegisterProximitySource(UChunkWorldProximityComponent* Source);

	/** Unregisters a previously registered proximity source. */
	void UnregisterProximitySource(UChunkWorldProximityComponent* Source);

	/** Force-removes one active or pending swap because the underlying voxel is being authoritatively destroyed. */
	bool ForceRemoveSwapForDestroyedBlock(const FIntVector& BlockWorldPos);

	/** Resolves the current actor presentation transform for one swapped block so destruction or follow-up presentation can inherit the same placement. */
	bool TryGetActiveSwapPresentationTransformForBlock(const FIntVector& BlockWorldPos, FTransform& OutSwapTransform) const;

	/** Shared swap scan event emitted when the server swaps a block into or out of actor presentation. */
	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld|Swap")
	FOnChunkWorldBlockSwapRequest OnBlockSwapRequested;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FActiveBlockSwap
	{
		TWeakObjectPtr<AActor> SpawnedActor;
		FGameplayTag BlockTypeName;
		FBlockDefinitionBase Definition;
		FTransform SwapTransform = FTransform::Identity;
	};

	struct FPendingSwapActorLoad
	{
		FGameplayTag BlockTypeName;
		FBlockDefinitionBase Definition;
		FTransform SwapTransform = FTransform::Identity;
	};

	struct FPendingSwapActorClassLoad
	{
		TSoftClassPtr<AActor> SwapActorClass;
		TSharedPtr<FStreamableHandle> Handle;
		int32 RequestedWarmCount = 0;
	};

	struct FPendingPooledSwapActorRecycle
	{
		TWeakObjectPtr<AActor> Actor;
		double ReadyTimeSeconds = 0.0;
	};

	struct FSwapActorPool
	{
		TArray<TWeakObjectPtr<AActor>> AvailableActors;
		TArray<TWeakObjectPtr<AActor>> ActiveActors;
		TArray<FPendingPooledSwapActorRecycle> PendingRecycleActors;
	};

	struct FSwapScanDiagnostics
	{
		int32 QueryHitCount = 0;
		int32 ResolvedBlockHits = 0;
		int32 CandidateBlocksInRadius = 0;
		int32 SchemaHits = 0;
		int32 SwapAuthoredBlocks = 0;
		int32 SwapInRejectedByDistance = 0;
		int32 AlreadyActiveBlocks = 0;
		int32 SharedApplyFailures = 0;
		int32 SwapActorSpawnFailures = 0;
		int32 SwapActorClassPreloadRequests = 0;
		int32 FreshSwapActorSpawns = 0;
		int32 PooledSwapActorAcquires = 0;
		int32 PooledSwapActorReleases = 0;
	};

	void ScanForSwaps();
	void CleanupInvalidSources();
	AChunkWorld* GetOwningChunkWorld() const;
	bool ShouldRunSwapScan() const;
	void UpdateSwapScanTimer();
	void RestoreActiveSwapsForShutdown();
	void DestroyPooledSwapActors();
	void GatherSourceCandidateBlocks(AChunkWorld* ChunkWorld, UChunkWorldProximityComponent* Source, TSet<FIntVector>& EvaluatedBlockPositions, int32& InOutProcessedBlocks, int32& InOutSwapsEntered, FSwapScanDiagnostics& Diagnostics);
	void TrySwapInBlock(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const FTransform& SwapTransform, const FGameplayTag& BlockTypeName, const FBlockDefinitionBase& Definition, FSwapScanDiagnostics& Diagnostics, bool& bOutEnteredSwap);
	void TrySwapOutBlock(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const FActiveBlockSwap& Active);
	bool ForceRemoveSwap(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const TCHAR* Reason);
	void RequestSwapActorClassPreload(const TSoftClassPtr<AActor>& SwapActorClass, int32 RequestedWarmCount, FSwapScanDiagnostics* Diagnostics = nullptr);
	void HandlePendingSwapActorClassLoaded(FSoftObjectPath LoadedClassPath);
	bool FinalizeSwapIn(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const FTransform& SwapTransform, const FGameplayTag& BlockTypeName, const FBlockDefinitionBase& Definition, float MinDistance, float RelevantDistance, FSwapScanDiagnostics* Diagnostics = nullptr);
	AActor* AcquireSwapActor(const FIntVector& BlockWorldPos, const FTransform& SwapTransform, UClass* SwapActorClass, FSwapScanDiagnostics* Diagnostics = nullptr);
	void ReleaseSwapActor(AActor* SwapActor, FSwapScanDiagnostics* Diagnostics = nullptr);
	void PrewarmSwapActorClass(UClass* SwapActorClass, int32 RequestedWarmCount);
	AActor* SpawnSwapActor(UClass* SwapActorClass, const FTransform& SpawnTransform) const;
	void PrepareSwapActorForUse(AActor* SwapActor, const FIntVector& BlockWorldPos, const FTransform& SwapTransform) const;
	void ResetSwapActorForPool(AActor* SwapActor) const;
	FTransform BuildPooledSwapActorParkingTransform(UClass* SwapActorClass, int32 SlotIndex) const;
	void FlushPendingPooledSwapActorRecycle();
	void UpdatePooledSwapActorRecycleTimer();
	bool IsWithinAnySourceSwapDistance(const FVector& Location, bool bUseSwapOutDistance, float& OutMinDistance, float& OutRelevantDistance) const;
	void DrawDebugActiveSwapBlocks(AChunkWorld* ChunkWorld) const;
	int32 GetSpawnedSwapActorCount() const;
	int32 GetPooledSwapActorCount() const;

	TArray<TWeakObjectPtr<UChunkWorldProximityComponent>> ProximitySources;
	TMap<FIntVector, FActiveBlockSwap> ActiveSwaps;
	TMap<FIntVector, FPendingSwapActorLoad> PendingSwapActorLoads;
	TMap<FSoftObjectPath, FPendingSwapActorClassLoad> PendingSwapActorClassLoads;
	TMap<TObjectPtr<UClass>, FSwapActorPool> SwapActorPools;
	FTimerHandle SwapScanHandle;
	FTimerHandle PooledSwapActorRecycleHandle;

	/** Interval in seconds between server-side swap scans around registered proximity sources. */
	UPROPERTY(EditAnywhere, Category = "Block|ChunkWorld|Swap", meta = (ClampMin = "0.01", UIMin = "0.01", DisplayPriority = "1", ToolTip = "Interval in seconds between server-side swap scans around registered proximity sources."))
	float SwapScanInterval = 0.2f;

	/** Maximum number of candidate blocks processed during one server-side swap scan tick. */
	UPROPERTY(EditAnywhere, Category = "Block|ChunkWorld|Swap", meta = (ClampMin = "1", UIMin = "1", DisplayPriority = "2", ToolTip = "Maximum number of query-resolved mesh-backed voxel candidates processed during one server-side swap scan tick."))
	int32 MaxBlocksPerScan = 5000;

	/** Local project change: keep expensive swap actors warm and reusable so proximity-driven swaps avoid repeated spawn hitches. */
	UPROPERTY(EditAnywhere, Category = "Block|ChunkWorld|Swap|Pooling", meta = (DisplayPriority = "3", ToolTip = "If true, loaded swap actor classes keep a small reusable actor pool so repeated proximity swaps can reuse existing actors instead of respawning every time."))
	bool bEnableSwapActorPooling = true;

	/** If true, nearby schema-authored swap actor classes begin loading before they cross the swap-in distance threshold. */
	UPROPERTY(EditAnywhere, Category = "Block|ChunkWorld|Swap|Pooling", meta = (DisplayPriority = "4", ToolTip = "If true, nearby schema-authored swap actor classes begin loading before they cross the swap-in distance threshold so the first relevant swap can avoid waiting on class load."))
	bool bEnableSwapActorPreload = true;

	/** Default number of parked actors to keep warm per loaded swap actor class. */
	UPROPERTY(EditAnywhere, Category = "Block|ChunkWorld|Swap|Pooling", meta = (ClampMin = "0", UIMin = "0", DisplayPriority = "5", ToolTip = "Default number of parked actors to keep warm per loaded swap actor class. Increase this to reduce first-use spawn hitches at the cost of memory."))
	int32 DefaultSwapActorWarmPoolSize = 1;

	/** Maximum number of parked reusable actors retained for one swap actor class. */
	UPROPERTY(EditAnywhere, Category = "Block|ChunkWorld|Swap|Pooling", meta = (ClampMin = "0", UIMin = "0", DisplayPriority = "6", ToolTip = "Maximum number of parked reusable actors retained for one swap actor class. Additional released actors are destroyed instead of returned to the pool."))
	int32 MaxPooledSwapActorsPerClass = 4;

	/** Delay before one released actor becomes eligible for reassignment to another replicated swap entry. */
	UPROPERTY(EditAnywhere, Category = "Block|ChunkWorld|Swap|Pooling", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayPriority = "7", ToolTip = "Delay before one released actor becomes eligible for reassignment to another replicated swap entry. A short delay helps the swap fast array and actor replication settle before reuse."))
	float PooledSwapActorRecycleDelay = 0.05f;

	/** World-space offset from the owning chunk world used as the parking origin for pooled swap actors. */
	UPROPERTY(EditAnywhere, Category = "Block|ChunkWorld|Swap|Pooling", meta = (DisplayPriority = "8", ToolTip = "World-space offset from the owning chunk world used as the parking origin for pooled swap actors. Keep this far outside meaningful generated chunk space so hidden pooled actors stay out of gameplay traces and visibility."))
	FVector PooledSwapActorParkingWorldOffset = FVector(0.0f, 0.0f, -120000.0f);

	/** World-space spacing used when deterministically spreading parked pooled swap actors away from each other. */
	UPROPERTY(EditAnywhere, Category = "Block|ChunkWorld|Swap|Pooling", meta = (ClampMin = "1.0", UIMin = "1.0", DisplayPriority = "9", ToolTip = "World-space spacing used when deterministically spreading parked pooled swap actors away from each other. Larger values reduce overlap inside the hidden pool parking area."))
	float PooledSwapActorParkingCellSize = 300.0f;
};
