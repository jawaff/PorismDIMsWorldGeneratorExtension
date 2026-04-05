// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include <concepts>
#include <type_traits>

#include "BlockTypeSchemaRegistry.generated.h"

class UScriptStruct;
class UMaterialInterface;
class USoundBase;
class UStaticMesh;

/**
 * Base definition family for project-specific block lookup structs.
 */
USTRUCT(BlueprintType)
struct FBlockDefinitionBase
{
	GENERATED_BODY()

	/**
	 * Optional material asset reference for this block type.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Definition", meta = (ToolTip = "Optional material asset reference for this block type."))
	TSoftObjectPtr<UMaterialInterface> MaterialAsset;

	/**
	 * Optional mesh asset reference for this block type.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Definition", meta = (ToolTip = "Optional mesh asset reference for this block type."))
	TSoftObjectPtr<UStaticMesh> MeshAsset;

	/**
	 * Optional default swap id for this block type.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Definition", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Optional default swap id for this block type."))
	int32 SwapId = 0;

	/**
	 * Optional sound to play when this block type is destroyed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Definition", meta = (ToolTip = "Optional sound to play when this block type is destroyed."))
	TObjectPtr<USoundBase> DestroyedSound = nullptr;
};

/**
 * Base runtime custom-data family for project-specific block custom-data structs.
 */
USTRUCT(BlueprintType)
struct FBlockCustomDataBase
{
	GENERATED_BODY()

	/**
	 * If true, this block is invincible by default when materialized at runtime.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CustomData", meta = (ToolTip = "If true, this block is invincible by default when materialized at runtime."))
	bool bInvincible = false;

	/**
	 * If true, this block can participate in swaps by default.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CustomData", meta = (ToolTip = "If true, this block can participate in swaps by default."))
	bool bAllowSwap = false;
};

/**
 * Default damage-oriented definition payload for blocks that can take hits.
 */
USTRUCT(BlueprintType)
struct FBlockDamageDefinition : public FBlockDefinitionBase
{
	GENERATED_BODY()

	/**
	 * Maximum health for this block type before it is destroyed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Maximum health for this block type before it is destroyed."))
	int32 MaxHealth = 1;

	/**
	 * Sound to play when this block type is hit but not destroyed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ToolTip = "Sound to play when this block type is hit but not destroyed."))
	TObjectPtr<USoundBase> HitSound = nullptr;
};

/**
 * Default damage-oriented custom-data payload for blocks that can be damaged at runtime.
 */
USTRUCT(BlueprintType)
struct FBlockDamageCustomData : public FBlockCustomDataBase
{
	GENERATED_BODY()

	/**
	 * Current health for this materialized block instance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Current health for this materialized block instance."))
	int32 Health = 1;
};

/**
 * Authored payloads for one semantic block type.
 */
USTRUCT(BlueprintType)
struct FBlockTypeSchema
{
	GENERATED_BODY()

	/**
	 * Stable gameplay tag for the semantic block type represented by this schema row.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block Type", meta = (Categories = "BlockType", ToolTip = "Stable gameplay tag for the semantic block type represented by this schema row."))
	FGameplayTag BlockTypeName;

	/**
	 * Typed definition payload for this semantic block type.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block Type", meta = (BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.BlockDefinitionBase", ToolTip = "Typed definition payload for this semantic block type."))
	FInstancedStruct Definition;

	/**
	 * Typed runtime custom-data payload for this semantic block type.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block Type", meta = (BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.BlockCustomDataBase", ToolTip = "Typed runtime custom-data payload for this semantic block type."))
	FInstancedStruct CustomData;
};

/**
 * Registry asset that owns the shared base definition/custom-data families and the authored block type payloads.
 */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API UBlockTypeSchemaRegistry : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Creates a schema registry asset with plugin-owned base struct defaults.
	 */
	UBlockTypeSchemaRegistry();

	/**
	 * Finds a semantic block type definition by gameplay tag.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|Voxel")
	bool FindBlockTypeDefinition(FGameplayTag BlockTypeName, FBlockTypeSchema& OutDefinition) const;

	/**
	 * Resolves a block definition by gameplay tag and copies it into a typed struct when the stored schema row matches.
	 */
	template <typename TDefinition>
	requires std::derived_from<std::remove_cvref_t<TDefinition>, FBlockDefinitionBase>
	bool TryGetBlockDefinition(FGameplayTag BlockTypeName, TDefinition& OutDefinition) const
	{
		FBlockTypeSchema BlockTypeSchema;
		if (!FindBlockTypeDefinition(BlockTypeName, BlockTypeSchema) || !BlockTypeSchema.Definition.IsValid())
		{
			return false;
		}

		const TDefinition* TypedDefinition = BlockTypeSchema.Definition.GetPtr<TDefinition>();
		if (TypedDefinition == nullptr)
		{
			return false;
		}

		OutDefinition = *TypedDefinition;
		return true;
	}

	/**
	 * Resolves a block definition by gameplay tag and copies the instanced payload when the stored schema row matches.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|Voxel")
	bool TryGetBlockDefinition(FGameplayTag BlockTypeName, FInstancedStruct& OutDefinition) const;

	/**
	 * Resolves a block custom-data payload by gameplay tag and copies it into a typed struct when the stored schema row matches.
	 */
	template <typename TCustomData>
	requires std::derived_from<std::remove_cvref_t<TCustomData>, FBlockCustomDataBase>
	bool TryGetBlockCustomData(FGameplayTag BlockTypeName, TCustomData& OutCustomData) const
	{
		FBlockTypeSchema BlockTypeSchema;
		if (!FindBlockTypeDefinition(BlockTypeName, BlockTypeSchema) || !BlockTypeSchema.CustomData.IsValid())
		{
			return false;
		}

		const TCustomData* TypedCustomData = BlockTypeSchema.CustomData.GetPtr<TCustomData>();
		if (TypedCustomData == nullptr)
		{
			return false;
		}

		OutCustomData = *TypedCustomData;
		return true;
	}

	/**
	 * Resolves a block custom-data payload by gameplay tag and copies the instanced payload when the stored schema row matches.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|Voxel")
	bool TryGetBlockCustomData(FGameplayTag BlockTypeName, FInstancedStruct& OutCustomData) const;

	/**
	 * Returns the authored schema rows that the component uses to build material and mesh lookup maps.
	 */
	const TArray<FBlockTypeSchema>& GetBlockTypeDefinitions() const { return BlockTypeDefinitions; }

#if WITH_EDITOR
	/**
	 * Validates schema payload compatibility when edited in the editor.
	 */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/**
	 * Validates schema payload compatibility after the asset loads so invalid authored rows are reported early.
	 */
	virtual void PostLoad() override;

private:
	/**
	 * Validates that one instanced struct is compatible with the supplied base family.
	 */
	static bool IsCompatiblePayload(const FInstancedStruct& Payload, const UScriptStruct* BaseStruct, const TCHAR* ContextLabel);

	/**
	 * Validates that the definition/custom-data payloads match the plugin-owned base families.
	 */
	bool ValidateSchema(FString* OutErrorMessage = nullptr) const;

	/**
	 * Returns the first matching type definition for a gameplay tag.
	 */
	const FBlockTypeSchema* FindBlockTypeDefinitionInternal(FGameplayTag BlockTypeName) const;

	/**
	 * Base definition family for all lookup payloads in this schema.
	 */
	UPROPERTY(EditAnywhere, Category = "Schema", meta = (ToolTip = "Base definition family for all lookup payloads in this schema registry."))
	TObjectPtr<UScriptStruct> BaseDefinitionStruct;

	/**
	 * Base runtime custom-data family for all custom-data payloads in this schema.
	 */
	UPROPERTY(EditAnywhere, Category = "Schema", meta = (ToolTip = "Base runtime custom-data family for all custom-data payloads in this schema registry."))
	TObjectPtr<UScriptStruct> BaseCustomDataStruct;

	/**
	 * All semantic block type payload definitions in this schema.
	 */
	UPROPERTY(EditAnywhere, Category = "Schema", meta = (TitleProperty = "BlockTypeName", ToolTip = "All semantic block type payload definitions in this schema."))
	TArray<FBlockTypeSchema> BlockTypeDefinitions;
};
