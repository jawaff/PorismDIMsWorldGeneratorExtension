// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Block/BlockTypeSchemaRegistry.h"
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChunkWorld/ChunkWorldBlockSwapTypes.h"
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
		TSharedPtr<FStreamableHandle> Handle;
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
	};

	void ScanForSwaps();
	void CleanupInvalidSources();
	AChunkWorld* GetOwningChunkWorld() const;
	bool ShouldRunSwapScan() const;
	void UpdateSwapScanTimer();
	void RestoreActiveSwapsForShutdown();
	void GatherSourceCandidateBlocks(AChunkWorld* ChunkWorld, UChunkWorldProximityComponent* Source, TSet<FIntVector>& EvaluatedBlockPositions, int32& InOutProcessedBlocks, int32& InOutSwapsEntered, FSwapScanDiagnostics& Diagnostics);
	void TrySwapInBlock(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const FTransform& SwapTransform, const FGameplayTag& BlockTypeName, const FBlockDefinitionBase& Definition, FSwapScanDiagnostics& Diagnostics, bool& bOutEnteredSwap);
	void TrySwapOutBlock(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const FActiveBlockSwap& Active);
	bool ForceRemoveSwap(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const TCHAR* Reason);
	void HandlePendingSwapActorClassLoaded(FIntVector BlockWorldPos);
	bool FinalizeSwapIn(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const FTransform& SwapTransform, const FGameplayTag& BlockTypeName, const FBlockDefinitionBase& Definition, float MinDistance, float RelevantDistance, FSwapScanDiagnostics* Diagnostics = nullptr);
	bool IsWithinAnySourceSwapDistance(const FVector& Location, bool bUseSwapOutDistance, float& OutMinDistance, float& OutRelevantDistance) const;
	void DrawDebugActiveSwapBlocks(AChunkWorld* ChunkWorld) const;
	int32 GetSpawnedSwapActorCount() const;
	void LogSwapDiagnostics(const TCHAR* Stage, int32 ProcessedBlocks = INDEX_NONE, int32 SwapsEntered = INDEX_NONE, int32 SwapsExited = INDEX_NONE, const FSwapScanDiagnostics* Diagnostics = nullptr) const;

	TArray<TWeakObjectPtr<UChunkWorldProximityComponent>> ProximitySources;
	TMap<FIntVector, FActiveBlockSwap> ActiveSwaps;
	TMap<FIntVector, FPendingSwapActorLoad> PendingSwapActorLoads;
	FTimerHandle SwapScanHandle;

	/** Interval in seconds between server-side swap scans around registered proximity sources. */
	UPROPERTY(EditAnywhere, Category = "Block|ChunkWorld|Swap", meta = (ClampMin = "0.01", UIMin = "0.01", DisplayPriority = "1", ToolTip = "Interval in seconds between server-side swap scans around registered proximity sources."))
	float SwapScanInterval = 0.2f;

	/** Maximum number of candidate blocks processed during one server-side swap scan tick. */
	UPROPERTY(EditAnywhere, Category = "Block|ChunkWorld|Swap", meta = (ClampMin = "1", UIMin = "1", DisplayPriority = "2", ToolTip = "Maximum number of query-resolved mesh-backed voxel candidates processed during one server-side swap scan tick."))
	int32 MaxBlocksPerScan = 5000;
};
