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
	 * Returns the default health-oriented definition struct.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema")
	static UScriptStruct* GetBlockHealthDefinitionStruct();

	/**
	 * Returns the default health-oriented custom-data struct.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema")
	static UScriptStruct* GetBlockHealthCustomDataStruct();

	/**
	 * Returns the default health-oriented definition struct.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema")
	static UScriptStruct* GetBlockDamageDefinitionStruct();

	/**
	 * Returns the default health-oriented custom-data struct.
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
	 * Returns true when the supplied instanced payload derives from the health definition family.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema|Health")
	static bool IsBlockHealthDefinitionPayload(const FInstancedStruct& Payload);

	/**
	 * Returns true when the supplied instanced payload derives from the health custom-data family.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema|Health")
	static bool IsBlockHealthCustomDataPayload(const FInstancedStruct& Payload);

	/**
	 * Copies a health-oriented definition payload out of an instanced struct when the payload derives from the shared health definition base.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema|Health")
	static bool TryGetBlockHealthDefinition(const FInstancedStruct& Payload, FBlockHealthDefinition& OutHealthDefinition);

	/**
	 * Copies a health-oriented custom-data payload out of an instanced struct when the payload derives from the shared health custom-data base.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema|Health")
	static bool TryGetBlockHealthCustomData(const FInstancedStruct& Payload, FBlockHealthCustomData& OutHealthCustomData);

	/**
	 * Returns true when the supplied instanced payload derives from the health definition family.
	 * Kept for backward compatibility with older damage-oriented naming.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema|Health")
	static bool IsBlockDamageDefinitionPayload(const FInstancedStruct& Payload);

	/**
	 * Returns true when the supplied instanced payload derives from the health custom-data family.
	 * Kept for backward compatibility with older damage-oriented naming.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema|Health")
	static bool IsBlockDamageCustomDataPayload(const FInstancedStruct& Payload);

	/**
	 * Copies a health-oriented definition payload out of an instanced struct when the payload derives from the shared health definition base.
	 * Kept for backward compatibility with older damage-oriented naming.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema|Health")
	static bool TryGetBlockDamageDefinition(const FInstancedStruct& Payload, FBlockDamageDefinition& OutDamageDefinition);

	/**
	 * Copies a health-oriented custom-data payload out of an instanced struct when the payload derives from the shared health custom-data base.
	 * Kept for backward compatibility with older damage-oriented naming.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|Schema|Health")
	static bool TryGetBlockDamageCustomData(const FInstancedStruct& Payload, FBlockDamageCustomData& OutDamageCustomData);
};
