// Copyright 2026 Spotted Loaf Studio

#include "Block/BlockTypeSchemaBlueprintLibrary.h"

namespace
{
	/**
	 * Returns the typed base subobject for an instanced payload when the payload derives from the requested base family.
	 */
	template <typename TBase>
	const TBase* GetCompatiblePayload(const FInstancedStruct& Payload)
	{
		const UScriptStruct* PayloadStruct = Payload.GetScriptStruct();
		if (PayloadStruct == nullptr || !PayloadStruct->IsChildOf(TBase::StaticStruct()))
		{
			return nullptr;
		}

		const void* PayloadMemory = Payload.GetMemory();
		return PayloadMemory != nullptr ? static_cast<const TBase*>(PayloadMemory) : nullptr;
	}
}

UScriptStruct* UBlockTypeSchemaBlueprintLibrary::GetBlockDefinitionBaseStruct()
{
	return FBlockDefinitionBase::StaticStruct();
}

UScriptStruct* UBlockTypeSchemaBlueprintLibrary::GetBlockCustomDataBaseStruct()
{
	return FBlockCustomDataBase::StaticStruct();
}

UScriptStruct* UBlockTypeSchemaBlueprintLibrary::GetBlockDamageDefinitionStruct()
{
	return FBlockDamageDefinition::StaticStruct();
}

UScriptStruct* UBlockTypeSchemaBlueprintLibrary::GetBlockDamageCustomDataStruct()
{
	return FBlockDamageCustomData::StaticStruct();
}

bool UBlockTypeSchemaBlueprintLibrary::IsBlockDefinitionPayload(const FInstancedStruct& Payload)
{
	const UScriptStruct* PayloadStruct = Payload.GetScriptStruct();
	return PayloadStruct != nullptr && PayloadStruct->IsChildOf(FBlockDefinitionBase::StaticStruct());
}

bool UBlockTypeSchemaBlueprintLibrary::IsBlockCustomDataPayload(const FInstancedStruct& Payload)
{
	const UScriptStruct* PayloadStruct = Payload.GetScriptStruct();
	return PayloadStruct != nullptr && PayloadStruct->IsChildOf(FBlockCustomDataBase::StaticStruct());
}

bool UBlockTypeSchemaBlueprintLibrary::TryGetBlockDefinitionBase(const FInstancedStruct& Payload, FBlockDefinitionBase& OutBlockDefinitionBase)
{
	const FBlockDefinitionBase* BlockDefinition = GetCompatiblePayload<FBlockDefinitionBase>(Payload);
	if (BlockDefinition == nullptr)
	{
		return false;
	}

	OutBlockDefinitionBase = *BlockDefinition;
	return true;
}

bool UBlockTypeSchemaBlueprintLibrary::TryGetBlockCustomDataBase(const FInstancedStruct& Payload, FBlockCustomDataBase& OutBlockCustomDataBase)
{
	const FBlockCustomDataBase* BlockCustomData = GetCompatiblePayload<FBlockCustomDataBase>(Payload);
	if (BlockCustomData == nullptr)
	{
		return false;
	}

	OutBlockCustomDataBase = *BlockCustomData;
	return true;
}

bool UBlockTypeSchemaBlueprintLibrary::IsBlockDamageDefinitionPayload(const FInstancedStruct& Payload)
{
	const UScriptStruct* PayloadStruct = Payload.GetScriptStruct();
	return PayloadStruct != nullptr && PayloadStruct->IsChildOf(FBlockDamageDefinition::StaticStruct());
}

bool UBlockTypeSchemaBlueprintLibrary::IsBlockDamageCustomDataPayload(const FInstancedStruct& Payload)
{
	const UScriptStruct* PayloadStruct = Payload.GetScriptStruct();
	return PayloadStruct != nullptr && PayloadStruct->IsChildOf(FBlockDamageCustomData::StaticStruct());
}

bool UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageDefinition(const FInstancedStruct& Payload, FBlockDamageDefinition& OutDamageDefinition)
{
	const FBlockDamageDefinition* DamageDefinition = GetCompatiblePayload<FBlockDamageDefinition>(Payload);
	if (DamageDefinition == nullptr)
	{
		return false;
	}

	OutDamageDefinition = *DamageDefinition;
	return true;
}

bool UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageCustomData(const FInstancedStruct& Payload, FBlockDamageCustomData& OutDamageCustomData)
{
	const FBlockDamageCustomData* DamageCustomData = GetCompatiblePayload<FBlockDamageCustomData>(Payload);
	if (DamageCustomData == nullptr)
	{
		return false;
	}

	OutDamageCustomData = *DamageCustomData;
	return true;
}
