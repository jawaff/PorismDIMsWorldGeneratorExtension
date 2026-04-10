// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChunkWorldProximityComponent.generated.h"

/**
 * Actor-owned chunk-world proximity source used by shared proximity-driven systems such as block swap scanning.
 */
UCLASS(ClassGroup = (Block), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "ChunkWorld Proximity Component"))
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldProximityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Creates a non-ticking component that exposes an origin and radius for chunk-world proximity systems. */
	UChunkWorldProximityComponent();

	/** Returns the world-space scan origin for this proximity source. */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld")
	FVector GetScanOrigin() const;

	/** Returns the configured chunk-world swap relevance radius, which matches the swap-out distance. */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld")
	float GetProximityRadius() const { return SwapOutDistance; }

	/** Returns the distance at which mesh-backed swap voxels become relevant for this proximity source. */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Swap")
	float GetSwapInDistance() const { return SwapInDistance; }

	/** Returns the distance at which active swap actors stop being relevant for this proximity source. */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Swap")
	float GetSwapOutDistance() const { return SwapOutDistance; }

	/** Returns the larger radius used to visualize future preload coverage around this proximity source. */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Swap")
	float GetSwapPreloadDistance() const { return SwapPreloadDistance; }

	/** Returns the collision channel used by polling trace queries for this proximity source. */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld")
	ECollisionChannel GetProximityCollisionChannel() const { return ProximityCollisionChannel; }

	/** Returns true when this source should draw active swapped voxel blocks for debugging. */
	bool ShouldDebugDrawActiveSwapBlocks() const { return bDebugDrawActiveSwapBlocks; }

	/** Returns the debug color used for active swapped voxel blocks. */
	FColor GetDebugActiveSwapBlockColor() const { return DebugActiveSwapBlockColor; }

	/** Returns the debug line thickness used for active swapped voxel blocks. */
	float GetDebugActiveSwapBlockThickness() const { return DebugActiveSwapBlockThickness; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Offset applied to the scan origin relative to the owning actor location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld", meta = (ToolTip = "Offset applied to the scan origin relative to the owning actor location."))
	FVector ScanOriginOffset = FVector::ZeroVector;

	/** Collision channel used by the polling sphere sweep for chunk-world proximity checks. Configure this to match the block/world trace policy for represented voxel meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld", meta = (ToolTip = "Collision channel used by the polling sphere sweep. Configure this to match the block/world trace policy for represented voxel meshes."))
	TEnumAsByte<ECollisionChannel> ProximityCollisionChannel = ECC_WorldDynamic;

	/** Distance at which this proximity source considers one queried mesh-backed voxel relevant enough to swap into actor presentation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Swap", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Distance at which this proximity source considers one queried mesh-backed voxel relevant enough to swap into actor presentation."))
	float SwapInDistance = 1200.0f;

	/** Distance at which this proximity source considers one active swapped voxel no longer relevant and ready to restore back into the chunk world. This also defines the polling sphere-sweep radius for mesh-backed swap candidate detection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Swap", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Distance at which this proximity source considers one active swapped voxel no longer relevant and ready to restore back into the chunk world. This also defines the polling sphere-sweep radius for mesh-backed swap candidate detection and must be greater than SwapInDistance."))
	float SwapOutDistance = 1700.0f;

	/** Larger radius used to visualize future preload coverage before authored swap actors become swap-in eligible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Swap", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Larger radius used to visualize future preload coverage before authored swap actors become swap-in eligible. Set this greater than or equal to SwapOutDistance when you want preload debug visualization beyond the active query radius."))
	float SwapPreloadDistance = 2200.0f;

	/** If true, draws wireframe debug spheres at the effective scan origin so the swap-in and swap-out query radii can be visualized in runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Debug", meta = (ToolTip = "If true, draws wireframe debug spheres at the effective scan origin so the swap-in and swap-out query radii can be visualized in runtime."))
	bool bDebugDrawProximitySphere = false;

	/** If true, draws boxes around active swapped voxel block centers so the current swap set is visible in runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Debug", meta = (ToolTip = "If true, draws boxes around active swapped voxel block centers so the current swap set is visible in runtime."))
	bool bDebugDrawActiveSwapBlocks = false;

	/** Color used for the swap-in debug sphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Debug", meta = (ToolTip = "Color used for the runtime swap-in debug sphere."))
	FColor DebugSwapInSphereColor = FColor(70, 150, 255);

	/** Color used for the swap-out debug sphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Debug", meta = (ToolTip = "Color used for the runtime swap-out debug sphere."))
	FColor DebugSwapOutSphereColor = FColor(20, 90, 210);

	/** Color used for the preload debug sphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Debug", meta = (ToolTip = "Color used for the runtime preload debug sphere."))
	FColor DebugSwapPreloadSphereColor = FColor(40, 200, 140);

	/** Color used for active swapped voxel block debug boxes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Debug", meta = (ToolTip = "Color used for active swapped voxel block debug boxes."))
	FColor DebugActiveSwapBlockColor = FColor::Yellow;

	/** Thickness used when drawing the runtime proximity debug sphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Debug", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Thickness used when drawing the runtime proximity debug sphere."))
	float DebugProximitySphereThickness = 2.0f;

	/** Thickness used when drawing active swapped voxel block debug boxes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Debug", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Thickness used when drawing active swapped voxel block debug boxes."))
	float DebugActiveSwapBlockThickness = 2.0f;

	/** Segment count used when drawing the runtime proximity debug sphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Debug", meta = (ClampMin = "4", UIMin = "4", ToolTip = "Segment count used when drawing the runtime proximity debug sphere."))
	int32 DebugProximitySphereSegments = 24;

};
