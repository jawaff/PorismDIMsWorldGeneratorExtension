// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Block/BlockTypeSchemaRegistry.h"
#include "ChunkWorld/Hit/ChunkWorldBlockDamageTypes.h"
#include "ChunkWorld/Hit/ChunkWorldBlockHitTypes.h"
#include "ChunkWorldBlockDamageBlueprintLibrary.generated.h"

class UBlockTypeSchemaComponent;

/**
 * Shared helper library for schema-aligned chunk-world health deltas.
 */
UCLASS()
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldBlockDamageBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Builds a transport-safe payload from one resolved block hit so the receiver can re-resolve the block authoritatively.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Damage")
	static bool TryBuildAuthorityPayloadFromResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit, FChunkWorldBlockHitAuthorityPayload& OutPayload);

	/**
	 * Re-resolves a transport-safe payload into a fresh runtime block context, typically on the server before authoritative damage calculation.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Damage")
	static bool TryResolveBlockHitContextFromAuthorityPayload(const FChunkWorldBlockHitAuthorityPayload& Payload, FChunkWorldResolvedBlockHit& OutResolvedHit);

	/**
	 * Broadcasts non-lethal hit feedback for one resolved block using the shared feedback component.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Damage")
	static bool TryBroadcastHitFeedbackForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit);

	/**
	 * Applies authoritative damage to one resolved block hit using the shared health schema family.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Damage")
	static bool TryApplyBlockDamageForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit, int32 DamageAmount, FChunkWorldBlockHealthDeltaResult& OutResult);

	/**
	 * Applies authoritative healing to one resolved block hit using the shared health schema family.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Damage")
	static bool TryApplyBlockHealingForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit, int32 HealingAmount, FChunkWorldBlockHealthDeltaResult& OutResult);

	/**
	 * Reads current health and invincibility state for one resolved block using the shared health schema family.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Damage")
	static bool TryGetCurrentBlockHealthStateForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit, int32& OutHealth, bool& bOutInvincible, FGameplayTag& OutBlockTypeName);

	/**
	 * Reads the block's current health, falling back to authored max health when runtime custom data is not initialized yet.
	 * `bOutHasRuntimeHealth` reports whether the returned value came from initialized runtime custom data.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Damage")
	static bool TryGetRuntimeBlockHealthStateForResolvedBlockHit(
		const FChunkWorldResolvedBlockHit& ResolvedHit,
		int32& OutHealth,
		int32& OutMaxHealth,
		bool& bOutInvincible,
		bool& bOutHasRuntimeHealth,
		FGameplayTag& OutBlockTypeName);

	/**
	 * Resolves one block world position and returns current health, authored max health, invincibility, and whether runtime custom data is initialized.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Damage")
	static bool TryGetBlockHealthStateForBlockWorldPos(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, int32& OutHealth, int32& OutMaxHealth, bool& bOutInvincible, bool& bOutHasStoredHealth, FGameplayTag& OutBlockTypeName);

	/**
	 * Resolves one block world position and reads current health, falling back to authored max health when runtime custom data is not initialized yet.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Damage")
	static bool TryGetRuntimeBlockHealthStateForBlockWorldPos(
		AChunkWorld* ChunkWorld,
		const FIntVector& BlockWorldPos,
		int32& OutHealth,
		int32& OutMaxHealth,
		bool& bOutInvincible,
		bool& bOutHasRuntimeHealth,
		FGameplayTag& OutBlockTypeName);

private:
	static bool TryResolveHealthSchemaForResolvedBlockHit(
		const FChunkWorldResolvedBlockHit& ResolvedHit,
		bool bAllowInitialization,
		FGameplayTag& OutBlockTypeName,
		FBlockHealthDefinition& OutDefinition,
		FInstancedStruct& OutCustomDataPayload,
		FBlockHealthCustomData& OutCustomData);

	static bool TryApplyBlockHealthDeltaForResolvedBlockHit(
		const FChunkWorldResolvedBlockHit& ResolvedHit,
		int32 Amount,
		bool bIsHealing,
		FChunkWorldBlockHealthDeltaResult& OutResult);
};
