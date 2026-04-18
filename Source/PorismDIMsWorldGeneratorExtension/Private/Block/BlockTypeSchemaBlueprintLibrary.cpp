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

UScriptStruct* UBlockTypeSchemaBlueprintLibrary::GetBlockHealthDefinitionStruct()
{
	return FBlockHealthDefinition::StaticStruct();
}

UScriptStruct* UBlockTypeSchemaBlueprintLibrary::GetBlockHealthCustomDataStruct()
{
	return FBlockHealthCustomData::StaticStruct();
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

bool UBlockTypeSchemaBlueprintLibrary::IsBlockHealthDefinitionPayload(const FInstancedStruct& Payload)
{
	const UScriptStruct* PayloadStruct = Payload.GetScriptStruct();
	return PayloadStruct != nullptr && PayloadStruct->IsChildOf(FBlockHealthDefinition::StaticStruct());
}

bool UBlockTypeSchemaBlueprintLibrary::IsBlockHealthCustomDataPayload(const FInstancedStruct& Payload)
{
	const UScriptStruct* PayloadStruct = Payload.GetScriptStruct();
	return PayloadStruct != nullptr && PayloadStruct->IsChildOf(FBlockHealthCustomData::StaticStruct());
}

bool UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthDefinition(const FInstancedStruct& Payload, FBlockHealthDefinition& OutHealthDefinition)
{
	const FBlockHealthDefinition* HealthDefinition = GetCompatiblePayload<FBlockHealthDefinition>(Payload);
	if (HealthDefinition == nullptr)
	{
		return false;
	}

	OutHealthDefinition = *HealthDefinition;
	return true;
}

bool UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthCustomData(const FInstancedStruct& Payload, FBlockHealthCustomData& OutHealthCustomData)
{
	const FBlockHealthCustomData* HealthCustomData = GetCompatiblePayload<FBlockHealthCustomData>(Payload);
	if (HealthCustomData == nullptr)
	{
		return false;
	}

	OutHealthCustomData = *HealthCustomData;
	return true;
}
