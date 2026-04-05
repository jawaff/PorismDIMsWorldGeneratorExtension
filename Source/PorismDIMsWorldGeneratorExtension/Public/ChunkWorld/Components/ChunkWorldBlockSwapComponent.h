// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChunkWorldStructs/ChunkWorldEnums.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "ChunkWorldBlockSwapComponent.generated.h"

class AChunkWorldExtended;
class UBlockTypeSchemaComponent;

/**
 * Shared replicated host for chunk-world block swap hide/restore transitions.
 * This lives in the plugin so projects can reuse the replicated block swap presentation path
 * even when their scan logic or swap-id selection policy remains game-specific.
 */
UCLASS(ClassGroup = (Block), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "ChunkWorld Block Swap Component"))
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldBlockSwapComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UChunkWorldBlockSwapComponent();

	/**
	 * Applies one authoritative swap request resolved from shared schema authoring and multicasts the resulting hide/restore transition to clients.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Swap")
	bool TryApplySwapRequest(const FIntVector& BlockWorldPos, bool bEntering);

protected:
	/** Replicates the authoritative hide/restore transition to every client. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastApplyBlockSwap(const FIntVector& BlockWorldPos, bool bEntering);

private:
	struct FBlockSwapBackup
	{
		int32 MaterialIndex = EmptyMaterial;
		FMeshData MeshData;
		bool bHasMesh = false;
	};

	bool CanApplySwapRequest(const FIntVector& BlockWorldPos) const;
	void ApplySwapHiddenState(const FIntVector& BlockWorldPos, bool bEntering);
	void CacheOriginalBlockState(const FIntVector& BlockWorldPos, FBlockSwapBackup& OutBackup) const;
	AChunkWorldExtended* GetChunkWorldOwner() const;
	UBlockTypeSchemaComponent* GetSchemaComponent() const;

	TMap<FIntVector, FBlockSwapBackup> SwapBackups;
};
