// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Block/BlockTypeSchemaRegistry.h"
#include "BlockTypeSchemaBlueprintLibrary.generated.h"

/**
 * Blueprint helper library for querying the plugin-owned block schema base types.
 *
 * This keeps Blueprint code on the generic FInstancedStruct path while still exposing the canonical
 * base struct references used by the registry and component helpers.
 */
UCLASS()
class PORISMDIMSWORLDGENERATOREXTENSION_API UBlockTypeSchemaBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Returns the shared base struct for block definition payloads.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema")
	static UScriptStruct* GetBlockDefinitionBaseStruct();

	/**
	 * Returns the shared base struct for block custom-data payloads.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema")
	static UScriptStruct* GetBlockCustomDataBaseStruct();

	/**
	 * Returns the default damage-oriented definition struct.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema")
	static UScriptStruct* GetBlockDamageDefinitionStruct();

	/**
	 * Returns the default damage-oriented custom-data struct.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema")
	static UScriptStruct* GetBlockDamageCustomDataStruct();

	/**
	 * Returns true when the supplied instanced payload derives from the block definition base family.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema")
	static bool IsBlockDefinitionPayload(const FInstancedStruct& Payload);

	/**
	 * Returns true when the supplied instanced payload derives from the block custom-data base family.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema")
	static bool IsBlockCustomDataPayload(const FInstancedStruct& Payload);

	/**
	 * Copies a block definition base payload out of an instanced struct when the payload derives from the shared definition base.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema")
	static bool TryGetBlockDefinitionBase(const FInstancedStruct& Payload, FBlockDefinitionBase& OutBlockDefinitionBase);

	/**
	 * Copies a block custom-data base payload out of an instanced struct when the payload derives from the shared custom-data base.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema")
	static bool TryGetBlockCustomDataBase(const FInstancedStruct& Payload, FBlockCustomDataBase& OutBlockCustomDataBase);

	/**
	 * Returns true when the supplied instanced payload derives from the damage definition family.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema|Damage")
	static bool IsBlockDamageDefinitionPayload(const FInstancedStruct& Payload);

	/**
	 * Returns true when the supplied instanced payload derives from the damage custom-data family.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema|Damage")
	static bool IsBlockDamageCustomDataPayload(const FInstancedStruct& Payload);

	/**
	 * Copies a damage-oriented definition payload out of an instanced struct when the payload derives from the shared damage definition base.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema|Damage")
	static bool TryGetBlockDamageDefinition(const FInstancedStruct& Payload, FBlockDamageDefinition& OutDamageDefinition);

	/**
	 * Copies a damage-oriented custom-data payload out of an instanced struct when the payload derives from the shared damage custom-data base.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema|Damage")
	static bool TryGetBlockDamageCustomData(const FInstancedStruct& Payload, FBlockDamageCustomData& OutDamageCustomData);
};
