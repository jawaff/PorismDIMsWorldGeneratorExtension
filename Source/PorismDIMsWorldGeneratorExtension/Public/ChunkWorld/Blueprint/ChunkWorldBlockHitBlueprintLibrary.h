// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Block/BlockTypeSchemaRegistry.h"
#include "ChunkWorld/Hit/ChunkWorldBlockHitTypes.h"
#include "ChunkWorldBlockHitBlueprintLibrary.generated.h"

class AChunkWorld;
class UBlockTypeSchemaComponent;
class UChunkWorldBlockFeedbackComponent;

/**
 * Shared helper library for resolving chunk-world block hits and schema data from gameplay hit information.
 */
UCLASS()
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldBlockHitBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Returns the chunk world that owns the supplied hit when the hit actor or component owner is a chunk world.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|ChunkWorld")
	static bool GetChunkWorldFromHitResult(const FHitResult& Hit, AChunkWorld*& OutChunkWorld);

	/**
	 * Returns the schema component attached to the supplied chunk world when available.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|ChunkWorld")
	static bool GetBlockTypeSchemaComponentFromChunkWorld(AChunkWorld* ChunkWorld, UBlockTypeSchemaComponent*& OutSchemaComponent);

	/**
	 * Returns the replicated block feedback component attached to the supplied chunk world when available.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|ChunkWorld")
	static bool GetBlockFeedbackComponentFromChunkWorld(AChunkWorld* ChunkWorld, UChunkWorldBlockFeedbackComponent*& OutFeedbackComponent);

	/**
	 * Resolves the chunk-world hit into a reusable block context that can be shared by trace and damage components.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|ChunkWorld|Block")
	static bool TryResolveBlockHitContextFromHitResult(const FHitResult& Hit, const FVector& TraceDirection, FChunkWorldResolvedBlockHit& OutResolvedHit);

	/**
	 * Resolves one block world position on a known chunk world into the shared block-hit context,
	 * including the same above-block mesh promotion used by hit-based block resolution.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|ChunkWorld|Block")
	static bool TryResolveBlockHitContextFromBlockWorldPos(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, FChunkWorldResolvedBlockHit& OutResolvedHit);

	/**
	 * Resolves one instanced-mesh candidate block position using the same support-block fallback
	 * that runtime ISM hit resolution uses after a mesh cell has already been cleared.
	 */
	static bool TryResolveInstancedMeshBlockHitContextFromBlockWorldPos(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, FChunkWorldResolvedBlockHit& OutResolvedHit);

	/**
	 * Resolves exactly the represented block at the supplied position without promoting to any overlay block above it.
	 * This is used by local prediction code when a cached or promoted overlay target is known to be dead.
	 */
	static bool TryResolveDirectBlockHitContextFromBlockWorldPos(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, FChunkWorldResolvedBlockHit& OutResolvedHit);

	/**
	 * Returns whether one gameplay hit should redirect from the support block to a mesh block above it.
	 * Uses an upper-region heuristic instead of requiring a mathematically flat top face.
	 */
	static bool ShouldPromoteOverlayBlockFromHit(
		AChunkWorld* ChunkWorld,
		const FIntVector& BaseBlockWorldPos,
		const FVector& ImpactPoint,
		const FVector& ImpactNormal,
		const FVector& TraceDirection);

	/**
	 * Reads the custom data for a resolved block hit and returns the authored block type tag that owns it.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|ChunkWorld|Block")
	static bool TryGetBlockCustomDataForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit, FGameplayTag& OutBlockTypeName, FInstancedStruct& OutCustomData);

	/**
	 * Reads the block definition for a resolved block hit by resolving the block type tag from the custom data first.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|ChunkWorld|Block")
	static bool TryGetBlockDefinitionForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit, FGameplayTag& OutBlockTypeName, FInstancedStruct& OutDefinition);

	/**
	 * Reads the custom data for one block type directly from the schema registry.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|ChunkWorld|Block")
	static bool TryGetBlockCustomDataForBlockTypeName(UBlockTypeSchemaComponent* SchemaComponent, FGameplayTag BlockTypeName, FInstancedStruct& OutCustomData);

	/**
	 * Reads the definition for one block type directly from the schema registry.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|ChunkWorld|Block")
	static bool TryGetBlockDefinitionForBlockTypeName(UBlockTypeSchemaComponent* SchemaComponent, FGameplayTag BlockTypeName, FInstancedStruct& OutDefinition);

	/**
	 * Multicasts generic destroyed-block feedback using the block definition base family on the resolved hit.
	 */
	UFUNCTION(BlueprintCallable, Category = "Porism|ChunkWorld|Block")
	static bool TryBroadcastDestroyedFeedbackForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit);
};
