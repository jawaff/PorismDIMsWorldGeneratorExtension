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
class AActor;
class UMaterialInterface;
class UNiagaraSystem;
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
	 * Optional material asset reference used to associate this semantic block type with a Porism block.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Association", meta = (DisplayPriority = "1", ToolTip = "Optional material asset reference used to associate this semantic block type with a Porism block."))
	TSoftObjectPtr<UMaterialInterface> MaterialAsset;

	/**
	 * Optional mesh asset reference used to associate this semantic block type with a Porism block.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Association", meta = (DisplayPriority = "2", ToolTip = "Optional mesh asset reference used to associate this semantic block type with a Porism block."))
	TSoftObjectPtr<UStaticMesh> MeshAsset;

	/**
	 * Optional actor class spawned by project-side swap scanning when this block swaps out of the chunk world.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swap", meta = (DisplayPriority = "3", ToolTip = "Optional actor class spawned by project-side swap scanning when this block swaps out of the chunk world."))
	TSoftClassPtr<AActor> SwapActorClass;

	/**
	 * Optional sound to play when this block type is destroyed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Definition", meta = (DisplayPriority = "4", ToolTip = "Optional sound to play when this block type is destroyed."))
	TObjectPtr<USoundBase> DestroyedSound = nullptr;

	/**
	 * Optional Niagara system to spawn when this block type is destroyed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Definition", meta = (DisplayPriority = "5", ToolTip = "Optional Niagara system to spawn when this block type is destroyed."))
	TObjectPtr<UNiagaraSystem> DestroyedEffect = nullptr;

	/**
	 * If true, this block ignores damage requests entirely.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Definition", meta = (DisplayPriority = "6", ToolTip = "If true, this block ignores damage requests entirely."))
	bool bInvincible = false;
};

/**
 * Base runtime custom-data family for project-specific block custom-data structs.
 */
USTRUCT(BlueprintType)
struct FBlockCustomDataBase
{
	GENERATED_BODY()
};

/** Network delivery policy for one destruction presentation actor spawn. */
UENUM(BlueprintType)
enum class EBlockDestructionPresentationNetMode : uint8
{
	/** Only the authority spawns the destruction actor, and the actor class is expected to replicate when needed. */
	ReplicatedActor UMETA(DisplayName = "Replicated Actor"),

	/** Each machine spawns its own local destruction actor after it observes the authoritative block removal. */
	LocalOnlyPerClient UMETA(DisplayName = "Local Only Per Client")
};

/**
 * Default health-oriented definition payload for blocks that can take hits.
 */
USTRUCT(BlueprintType)
struct FBlockHealthDefinition : public FBlockDefinitionBase
{
	GENERATED_BODY()

	/**
	 * Optional destruction presentation actor spawned when this block reaches zero health and is removed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health", meta = (DisplayPriority = "7", ToolTip = "Optional destruction presentation actor spawned when this block reaches zero health and is removed. Implement the chunk-world destruction actor interface on that class so it can run its authored destruction behavior when triggered."))
	TSoftClassPtr<AActor> DestructionActorClass;

	/** Network delivery mode used when spawning the authored destruction presentation actor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health", meta = (DisplayPriority = "8", ToolTip = "Controls whether the destruction presentation actor is spawned only on the authority and allowed to replicate, or spawned locally on each machine after it observes the authoritative block removal. Use Local Only Per Client for Chaos-driven cosmetic destruction presentations."))
	EBlockDestructionPresentationNetMode DestructionPresentationNetMode = EBlockDestructionPresentationNetMode::ReplicatedActor;

	/**
	 * Maximum health for this block type before it is destroyed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health", meta = (DisplayPriority = "9", ClampMin = "0", UIMin = "0", ToolTip = "Maximum health for this block type before it is destroyed."))
	int32 MaxHealth = 1;

	/**
	 * Generic authored damage scalar applied by project-side damage calculators after they compute their local damage model.
	 * Kept in the shared health schema so projects can opt into a simple block-wide multiplier before introducing richer hit-response families.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health", meta = (DisplayPriority = "10", ClampMin = "0.0", UIMin = "0.0", ToolTip = "Generic authored damage scalar applied by project-side damage calculators after they compute their local damage model."))
	double DamageMultiplier = 1.0;

	/**
	 * Sound to play when this block type is hit but not destroyed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health", meta = (DisplayPriority = "11", ToolTip = "Sound to play when this block type is hit but not destroyed."))
	TObjectPtr<USoundBase> HitSound = nullptr;
};

/**
 * Default health-oriented custom-data payload for blocks that can be damaged at runtime.
 */
USTRUCT(BlueprintType)
struct FBlockHealthCustomData : public FBlockCustomDataBase
{
	GENERATED_BODY()

	/**
	 * Current health for this initialized block instance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health", meta = (DisplayPriority = "1", ClampMin = "0", UIMin = "0", ToolTip = "Current health for this initialized block instance."))
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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block Type", meta = (DisplayPriority = "1", Categories = "BlockType", ToolTip = "Stable gameplay tag for the semantic block type represented by this schema row."))
	FGameplayTag BlockTypeName;

	/**
	 * Typed definition payload for this semantic block type.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block Type", meta = (DisplayPriority = "2", BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.BlockDefinitionBase", ToolTip = "Typed definition payload for this semantic block type."))
	FInstancedStruct Definition;

	/**
	 * Typed runtime custom-data payload for this semantic block type.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block Type", meta = (DisplayPriority = "3", BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.BlockCustomDataBase", ToolTip = "Typed runtime custom-data payload for this semantic block type."))
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
	UPROPERTY(EditAnywhere, Category = "Schema", meta = (MetaStruct = "/Script/PorismDIMsWorldGeneratorExtension.BlockDefinitionBase", ToolTip = "Base definition family for all lookup payloads in this schema registry. Must derive from FBlockDefinitionBase."))
	TObjectPtr<UScriptStruct> BaseDefinitionStruct;

	/**
	 * Base runtime custom-data family for all custom-data payloads in this schema.
	 */
	UPROPERTY(EditAnywhere, Category = "Schema", meta = (MetaStruct = "/Script/PorismDIMsWorldGeneratorExtension.BlockCustomDataBase", ToolTip = "Base runtime custom-data family for all custom-data payloads in this schema registry. Must derive from FBlockCustomDataBase."))
	TObjectPtr<UScriptStruct> BaseCustomDataStruct;

	/**
	 * All semantic block type payload definitions in this schema.
	 */
	UPROPERTY(EditAnywhere, Category = "Schema", meta = (TitleProperty = "BlockTypeName", ToolTip = "All semantic block type payload definitions in this schema."))
	TArray<FBlockTypeSchema> BlockTypeDefinitions;
};
