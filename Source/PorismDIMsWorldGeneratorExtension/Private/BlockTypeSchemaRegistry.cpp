// Copyright 2026 Spotted Loaf Studio

#include "BlockTypeSchemaRegistry.h"

#include "PorismDIMsWorldGeneratorExtension.h"

UBlockTypeSchemaRegistry::UBlockTypeSchemaRegistry()
{
	BaseDefinitionStruct = FBlockDefinitionBase::StaticStruct();
	BaseCustomDataStruct = FBlockCustomDataBase::StaticStruct();
}

bool UBlockTypeSchemaRegistry::IsCompatiblePayload(const FInstancedStruct& Payload, const UScriptStruct* BaseStruct, const TCHAR* ContextLabel)
{
	const UScriptStruct* PayloadStruct = Payload.GetScriptStruct();
	if (PayloadStruct == nullptr)
	{
		return true;
	}

	if (BaseStruct != nullptr && PayloadStruct->IsChildOf(BaseStruct))
	{
		return true;
	}

	UE_LOG(
		LogPorismDIMsWorldGeneratorExtension,
		Warning,
		TEXT("Block type payload '%s' uses struct '%s' which is not compatible with base struct '%s'."),
		ContextLabel,
		*GetNameSafe(PayloadStruct),
		*GetNameSafe(BaseStruct));
	return false;
}

bool UBlockTypeSchemaRegistry::ValidateSchema(FString* OutErrorMessage) const
{
	if (BaseDefinitionStruct == nullptr || BaseCustomDataStruct == nullptr)
	{
		if (OutErrorMessage != nullptr)
		{
			*OutErrorMessage = TEXT("Schema base struct references are not initialized.");
		}
		return false;
	}

	for (const FBlockTypeSchema& Definition : BlockTypeDefinitions)
	{
		if (!Definition.BlockTypeName.IsValid())
		{
			if (OutErrorMessage != nullptr)
			{
				*OutErrorMessage = TEXT("A block type definition is missing its semantic gameplay tag.");
			}
			return false;
		}

		if (!IsCompatiblePayload(Definition.Definition, BaseDefinitionStruct, *Definition.BlockTypeName.ToString()))
		{
			if (OutErrorMessage != nullptr)
			{
				*OutErrorMessage = FString::Printf(TEXT("Definition payload for block type '%s' is incompatible with the configured base definition struct."), *Definition.BlockTypeName.ToString());
			}
			return false;
		}

		if (!IsCompatiblePayload(Definition.CustomData, BaseCustomDataStruct, *Definition.BlockTypeName.ToString()))
		{
			if (OutErrorMessage != nullptr)
			{
				*OutErrorMessage = FString::Printf(TEXT("Custom-data payload for block type '%s' is incompatible with the configured base custom-data struct."), *Definition.BlockTypeName.ToString());
			}
			return false;
		}
	}

	return true;
}

const FBlockTypeSchema* UBlockTypeSchemaRegistry::FindBlockTypeDefinitionInternal(FGameplayTag BlockTypeName) const
{
	return BlockTypeDefinitions.FindByPredicate(
		[&BlockTypeName](const FBlockTypeSchema& Definition)
		{
			return Definition.BlockTypeName.MatchesTagExact(BlockTypeName);
		});
}

bool UBlockTypeSchemaRegistry::FindBlockTypeDefinition(FGameplayTag BlockTypeName, FBlockTypeSchema& OutDefinition) const
{
	if (const FBlockTypeSchema* FoundDefinition = FindBlockTypeDefinitionInternal(BlockTypeName))
	{
		OutDefinition = *FoundDefinition;
		return true;
	}

	return false;
}

bool UBlockTypeSchemaRegistry::TryGetBlockDefinition(FGameplayTag BlockTypeName, FInstancedStruct& OutDefinition) const
{
	FBlockTypeSchema BlockTypeSchema;
	if (!FindBlockTypeDefinition(BlockTypeName, BlockTypeSchema) || !BlockTypeSchema.Definition.IsValid())
	{
		return false;
	}

	OutDefinition = BlockTypeSchema.Definition;
	return true;
}

bool UBlockTypeSchemaRegistry::TryGetBlockCustomData(FGameplayTag BlockTypeName, FInstancedStruct& OutCustomData) const
{
	FBlockTypeSchema BlockTypeSchema;
	if (!FindBlockTypeDefinition(BlockTypeName, BlockTypeSchema) || !BlockTypeSchema.CustomData.IsValid())
	{
		return false;
	}

	OutCustomData = BlockTypeSchema.CustomData;
	return true;
}

#if WITH_EDITOR
void UBlockTypeSchemaRegistry::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FString ValidationError;
	if (!ValidateSchema(&ValidationError))
	{
		UE_LOG(LogPorismDIMsWorldGeneratorExtension, Warning, TEXT("Block type schema registry validation failed: %s"), *ValidationError);
	}
}
#endif

void UBlockTypeSchemaRegistry::PostLoad()
{
	Super::PostLoad();

	FString ValidationError;
	if (!ValidateSchema(&ValidationError))
	{
		UE_LOG(LogPorismDIMsWorldGeneratorExtension, Warning, TEXT("Block type schema registry validation failed while loading: %s"), *ValidationError);
	}
}
