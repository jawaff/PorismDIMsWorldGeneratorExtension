// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InstancedStruct.h"
#include "Block/BlockCustomDataLayout.h"
#include "Block/BlockTypeSchemaRegistry.h"
#include <concepts>
#include <type_traits>

#include "BlockTypeSchemaComponent.generated.h"

/**
 * Reusable component that builds block-definition lookup tables from the schema registry once the chunk world is ready.
 */
UCLASS(ClassGroup = (Block), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "Block Type Schema Component"))
class PORISMDIMSWORLDGENERATOREXTENSION_API UBlockTypeSchemaComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * Creates a schema component that does not tick and only reacts to explicit schema/custom-data requests.
	 */
	UBlockTypeSchemaComponent();

	/**
	 * Returns the configured block type schema registry.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld")
	UBlockTypeSchemaRegistry* GetBlockTypeSchemaRegistry() const { return BlockTypeSchemaRegistry; }

	/**
	 * Returns how many runtime custom-data channels this schema needs, including the reserved initialization marker slot.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld")
	int32 GetRequiredCustomDataChannelCount() const;

	/**
	 * Returns the shared runtime slot index used by the `Health` field in the `FBlockDamageCustomData` family.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld")
	int32 GetBlockDamageHealthCustomDataIndex() const;

	/**
	 * Sets the schema registry used by this component's runtime lookup maps.
	 */
	void SetBlockTypeSchemaRegistry(UBlockTypeSchemaRegistry* InBlockTypeSchemaRegistry) { BlockTypeSchemaRegistry = InBlockTypeSchemaRegistry; }

	/**
	 * Returns true when the startup-built material and mesh definition maps are ready for use.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld")
	bool IsBlockDefinitionLookupReady() const { return bBlockDefinitionLookupReady; }

	/**
	 * Returns the block definition mapped to one material index when available.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld")
	bool GetBlockDefinitionForMaterialIndex(int32 MaterialIndex, FInstancedStruct& OutDefinition) const;

	/**
	 * Returns the block definition mapped to one mesh index when available.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld")
	bool GetBlockDefinitionForMeshIndex(int32 MeshIndex, FInstancedStruct& OutDefinition) const;

	/**
	 * Returns the material-index definition payload as a typed struct when the stored schema row matches the requested type.
	 */
	template <typename TDefinition>
	requires std::derived_from<std::remove_cvref_t<TDefinition>, FBlockDefinitionBase>
	bool GetBlockDefinitionForMaterialIndex(int32 MaterialIndex, TDefinition& OutDefinition) const
	{
		FInstancedStruct Definition;
		if (!GetBlockDefinitionForMaterialIndex(MaterialIndex, Definition))
		{
			return false;
		}

		const TDefinition* TypedDefinition = Definition.GetPtr<TDefinition>();
		if (TypedDefinition == nullptr)
		{
			return false;
		}

		OutDefinition = *TypedDefinition;
		return true;
	}

	/**
	 * Returns the mesh-index definition payload as a typed struct when the stored schema row matches the requested type.
	 */
	template <typename TDefinition>
	requires std::derived_from<std::remove_cvref_t<TDefinition>, FBlockDefinitionBase>
	bool GetBlockDefinitionForMeshIndex(int32 MeshIndex, TDefinition& OutDefinition) const
	{
		FInstancedStruct Definition;
		if (!GetBlockDefinitionForMeshIndex(MeshIndex, Definition))
		{
			return false;
		}

		const TDefinition* TypedDefinition = Definition.GetPtr<TDefinition>();
		if (TypedDefinition == nullptr)
		{
			return false;
		}

		OutDefinition = *TypedDefinition;
		return true;
	}

	/**
	 * Resolves the authored block definition directly from already-resolved runtime material and mesh indexes.
	 */
	bool GetBlockDefinitionForRepresentationIndexes(int32 MaterialIndex, int32 MeshIndex, FGameplayTag& OutBlockTypeName, FInstancedStruct& OutDefinition) const;

	/**
	 * Initializes block custom data using already-resolved runtime material and mesh indexes for schema selection.
	 */
	bool InitializeBlockCustomDataForRepresentationIndexes(const FIntVector& BlockWorldPos, int32 MaterialIndex, int32 MeshIndex);

	/**
	 * Initializes the block's custom data at the supplied world position when the runtime marker slot is still empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld")
	bool InitializeBlockCustomData(const FIntVector& BlockWorldPos);

	/**
	 * Returns whether the block at the supplied world position has runtime custom data initialized.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld")
	bool IsBlockCustomDataInitialized(const FIntVector& BlockWorldPos) const;

	/**
	 * Reconstructs the authored custom-data struct for the block at the supplied world position.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld")
	bool GetBlockCustomDataForBlockWorldPos(const FIntVector& BlockWorldPos, FGameplayTag& OutBlockTypeName, FInstancedStruct& OutCustomData) const;

	/**
	 * Reconstructs authored custom data using already-resolved runtime material and mesh indexes for schema selection.
	 */
	bool GetBlockCustomDataForRepresentationIndexes(const FIntVector& BlockWorldPos, int32 MaterialIndex, int32 MeshIndex, FGameplayTag& OutBlockTypeName, FInstancedStruct& OutCustomData) const;

	/**
	 * Writes one authored custom-data payload back into runtime slots for the supplied block world position.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld")
	bool SetBlockCustomDataForBlockWorldPos(const FIntVector& BlockWorldPos, const FInstancedStruct& CustomData);

	/**
	 * Reconstructs the authored custom-data struct for the block at the supplied world position and copies it into a typed struct.
	 */
	template <typename TCustomData>
	requires std::derived_from<std::remove_cvref_t<TCustomData>, FBlockCustomDataBase>
	bool GetBlockCustomDataForBlockWorldPos(const FIntVector& BlockWorldPos, FGameplayTag& OutBlockTypeName, TCustomData& OutCustomData) const
	{
		FInstancedStruct CustomData;
		if (!GetBlockCustomDataForBlockWorldPos(BlockWorldPos, OutBlockTypeName, CustomData))
		{
			return false;
		}

		const TCustomData* TypedCustomData = CustomData.GetPtr<TCustomData>();
		if (TypedCustomData == nullptr)
		{
			return false;
		}

		OutCustomData = *TypedCustomData;
		return true;
	}

	/**
	 * Writes one typed authored custom-data payload back into runtime slots for the supplied block world position.
	 */
	template <typename TCustomData>
	requires std::derived_from<std::remove_cvref_t<TCustomData>, FBlockCustomDataBase>
	bool SetBlockCustomDataForBlockWorldPos(const FIntVector& BlockWorldPos, const TCustomData& CustomData)
	{
		FInstancedStruct CustomDataStruct;
		CustomDataStruct.InitializeAs<TCustomData>(CustomData);
		return SetBlockCustomDataForBlockWorldPos(BlockWorldPos, CustomDataStruct);
	}

	/**
	 * Reconstructs the authored definition struct for the block at the supplied world position.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld")
	bool GetBlockDefinitionForBlockWorldPos(const FIntVector& BlockWorldPos, FGameplayTag& OutBlockTypeName, FInstancedStruct& OutDefinition) const;

	/**
	 * Reconstructs the authored definition struct for the block at the supplied world position and copies it into a typed struct.
	 */
	template <typename TDefinition>
	requires std::derived_from<std::remove_cvref_t<TDefinition>, FBlockDefinitionBase>
	bool GetBlockDefinitionForBlockWorldPos(const FIntVector& BlockWorldPos, FGameplayTag& OutBlockTypeName, TDefinition& OutDefinition) const
	{
		FInstancedStruct Definition;
		if (!GetBlockDefinitionForBlockWorldPos(BlockWorldPos, OutBlockTypeName, Definition))
		{
			return false;
		}

		const TDefinition* TypedDefinition = Definition.GetPtr<TDefinition>();
		if (TypedDefinition == nullptr)
		{
			return false;
		}

		OutDefinition = *TypedDefinition;
		return true;
	}

	/**
	 * Rebuilds the index lookup maps from the current chunk world's material and mesh tables.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld")
	void RebuildBlockDefinitionLookupMaps();

	/**
	 * Clears the startup-built lookup maps.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld")
	void ClearBlockDefinitionLookupMaps();

protected:
	/**
	 * Clears the startup-built lookup maps when the owning actor ends play.
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/**
	 * Returns how many custom-data channels the owning chunk world actually has available at runtime.
	 */
	int32 GetAvailableCustomDataChannelCount() const;

	/**
	 * Returns false when the supplied packed payload would overflow the chunk world's available custom-data channels.
	 * This project-specific guard prevents schema-driven custom-data writes from crashing Porism when the world runtime
	 * was configured with fewer custom channels than the schema layout requires.
	 */
	bool CanAccessCustomDataChannelCount(int32 RequiredChannelCount, const FIntVector& BlockWorldPos, const TCHAR* Context) const;

	/**
	 * Required schema registry asset that resolves block type schema rows and authored defaults.
	 */
	UPROPERTY()
	TObjectPtr<UBlockTypeSchemaRegistry> BlockTypeSchemaRegistry = nullptr;

	/**
	 * Startup-built lookup from material index to resolved block definition payload.
	 */
	UPROPERTY(Transient)
	TMap<int32, FInstancedStruct> MaterialDefinitionLookup;

	/**
	 * Startup-built lookup from material index to authored block type tag.
	 */
	UPROPERTY(Transient)
	TMap<int32, FGameplayTag> MaterialBlockTypeLookup;

	/**
	 * Startup-built lookup from mesh index to resolved block definition payload.
	 */
	UPROPERTY(Transient)
	TMap<int32, FInstancedStruct> MeshDefinitionLookup;

	/**
	 * Startup-built lookup from mesh index to authored block type tag.
	 */
	UPROPERTY(Transient)
	TMap<int32, FGameplayTag> MeshBlockTypeLookup;

	/**
	 * True after the startup lookup tables have been built successfully.
	 */
	UPROPERTY(Transient)
	bool bBlockDefinitionLookupReady = false;

	/**
	 * Returns the authored schema row for the supplied runtime material and mesh indexes.
	 */
	const FBlockTypeSchema* FindBlockTypeSchemaForIndexes(int32 MaterialIndex, int32 MeshIndex) const;

	/**
	 * Returns the cached runtime layout for one authored custom-data struct family.
	 */
	const FBlockCustomDataLayout* GetOrBuildBlockCustomDataLayout(const UScriptStruct* StructType) const;

	/**
	 * Reads the packed runtime custom-data slots for one block world position.
	 */
	bool ReadBlockCustomDataSlots(const FIntVector& BlockWorldPos, const FBlockCustomDataLayout& Layout, TArray<int32>& OutPackedValues) const;

	/**
	 * Writes one packed runtime custom-data array back into the chunk world at the supplied block world position.
	 */
	void WriteBlockCustomDataSlots(const FIntVector& BlockWorldPos, const TArray<int32>& PackedValues) const;

	/**
	 * Cache of authored custom-data layouts keyed by the struct family that declared them.
	 */
	mutable TMap<const UScriptStruct*, FBlockCustomDataLayout> BlockCustomDataLayoutCache;
};
