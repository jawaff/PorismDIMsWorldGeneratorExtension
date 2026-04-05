// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"

#include "Block/BlockCustomDataLayout.h"
#include "ChunkWorld/ChunkWorld.h"
#include "ChunkWorld/ChunkWorldBase.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "PorismDIMsWorldGeneratorExtension.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlockTypeSchemaComponent, Log, All);

UBlockTypeSchemaComponent::UBlockTypeSchemaComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UBlockTypeSchemaComponent::GetBlockDefinitionForMaterialIndex(int32 MaterialIndex, FInstancedStruct& OutDefinition) const
{
	if (const FInstancedStruct* FoundDefinition = MaterialDefinitionLookup.Find(MaterialIndex))
	{
		OutDefinition = *FoundDefinition;
		return true;
	}

	return false;
}

bool UBlockTypeSchemaComponent::GetBlockDefinitionForMeshIndex(int32 MeshIndex, FInstancedStruct& OutDefinition) const
{
	if (const FInstancedStruct* FoundDefinition = MeshDefinitionLookup.Find(MeshIndex))
	{
		OutDefinition = *FoundDefinition;
		return true;
	}

	return false;
}

const FBlockCustomDataLayout* UBlockTypeSchemaComponent::GetOrBuildBlockCustomDataLayout(const UScriptStruct* StructType) const
{
	if (StructType == nullptr)
	{
		return nullptr;
	}

	if (const FBlockCustomDataLayout* CachedLayout = BlockCustomDataLayoutCache.Find(StructType))
	{
		return CachedLayout;
	}

	if (!StructType->IsChildOf(FBlockCustomDataBase::StaticStruct()))
	{
		UE_LOG(
			LogBlockTypeSchemaComponent,
			Warning,
			TEXT("Custom-data struct '%s' is not derived from FBlockCustomDataBase and cannot be packed into runtime slots."),
			*GetNameSafe(StructType));
		return nullptr;
	}

	FBlockCustomDataLayout NewLayout;
	if (!NewLayout.Build(StructType))
	{
		UE_LOG(
			LogBlockTypeSchemaComponent,
			Warning,
			TEXT("Failed to build the block custom-data layout for struct '%s'. Only scalar bool, enum, numeric, and nested struct properties are supported."),
			*GetNameSafe(StructType));
		return nullptr;
	}

	return &BlockCustomDataLayoutCache.Add(StructType, MoveTemp(NewLayout));
}

bool UBlockTypeSchemaComponent::ReadBlockCustomDataSlots(const FIntVector& BlockWorldPos, const FBlockCustomDataLayout& Layout, TArray<int32>& OutPackedValues) const
{
	AChunkWorldBase* ChunkWorld = Cast<AChunkWorldBase>(GetOwner());
	if (ChunkWorld == nullptr)
	{
		return false;
	}

	OutPackedValues.Reset(Layout.GetValueSlotCount() + 1);
	for (int32 SlotIndex = 0; SlotIndex < Layout.GetValueSlotCount() + 1; ++SlotIndex)
	{
		OutPackedValues.Add(ChunkWorld->GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::CustomData, SlotIndex));
	}

	return true;
}

void UBlockTypeSchemaComponent::WriteBlockCustomDataSlots(const FIntVector& BlockWorldPos, const TArray<int32>& PackedValues) const
{
	AChunkWorldBase* ChunkWorld = Cast<AChunkWorldBase>(GetOwner());
	if (ChunkWorld == nullptr)
	{
		return;
	}

	for (int32 SlotIndex = 0; SlotIndex < PackedValues.Num(); ++SlotIndex)
	{
		ChunkWorld->SetBlockCustomValueByBlockWorldPos(BlockWorldPos, PackedValues[SlotIndex], SlotIndex);
	}
}

bool UBlockTypeSchemaComponent::GetBlockCustomDataForBlockWorldPos(const FIntVector& BlockWorldPos, FGameplayTag& OutBlockTypeName, FInstancedStruct& OutCustomData) const
{
	if (!bBlockDefinitionLookupReady)
	{
		UE_LOG(LogBlockTypeSchemaComponent, Warning, TEXT("Cannot reconstruct block custom data on %s because the lookup maps are not ready."), *GetNameSafe(GetOwner()));
		return false;
	}

	AChunkWorldBase* ChunkWorld = Cast<AChunkWorldBase>(GetOwner());
	if (ChunkWorld == nullptr)
	{
		UE_LOG(LogBlockTypeSchemaComponent, Warning, TEXT("Cannot reconstruct block custom data on %s because the owning actor is not a chunk world base."), *GetNameSafe(GetOwner()));
		return false;
	}

	const int32 MaterialIndex = ChunkWorld->GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::MaterialIndex);
	const int32 MeshIndex = ChunkWorld->GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::MeshIndex);
	const FBlockTypeSchema* BlockType = FindBlockTypeSchemaForIndexes(MaterialIndex, MeshIndex);
	if (BlockType == nullptr || !BlockType->CustomData.IsValid())
	{
		UE_LOG(
			LogBlockTypeSchemaComponent,
			Warning,
			TEXT("Cannot reconstruct block custom data on %s at %s because no valid block type schema was found for material index %d and mesh index %d."),
			*GetNameSafe(GetOwner()),
			*BlockWorldPos.ToString(),
			MaterialIndex,
			MeshIndex);
		return false;
	}

	OutBlockTypeName = BlockType->BlockTypeName;

	const UScriptStruct* StructType = BlockType->CustomData.GetScriptStruct();
	const FBlockCustomDataLayout* Layout = GetOrBuildBlockCustomDataLayout(StructType);
	if (Layout == nullptr)
	{
		return false;
	}

	TArray<int32> PackedValues;
	if (!ReadBlockCustomDataSlots(BlockWorldPos, *Layout, PackedValues))
	{
		return false;
	}

	const int32 MarkerSlotIndex = Layout->GetValueSlotCount();
	if (!PackedValues.IsValidIndex(MarkerSlotIndex) || PackedValues[MarkerSlotIndex] == 0)
	{
		return false;
	}

	OutCustomData.InitializeAs(StructType);
	if (!Layout->Unpack(PackedValues, OutCustomData.GetMutableMemory()))
	{
		OutCustomData.Reset();
		return false;
	}

	return true;
}

bool UBlockTypeSchemaComponent::SetBlockCustomDataForBlockWorldPos(const FIntVector& BlockWorldPos, const FInstancedStruct& CustomData)
{
	if (!CustomData.IsValid() || !bBlockDefinitionLookupReady)
	{
		return false;
	}

	const UScriptStruct* StructType = CustomData.GetScriptStruct();
	const FBlockCustomDataLayout* Layout = GetOrBuildBlockCustomDataLayout(StructType);
	if (Layout == nullptr)
	{
		return false;
	}

	TArray<int32> PackedCustomData;
	if (!Layout->Pack(CustomData.GetMemory(), PackedCustomData))
	{
		return false;
	}

	PackedCustomData.Add(1);
	WriteBlockCustomDataSlots(BlockWorldPos, PackedCustomData);
	return true;
}

bool UBlockTypeSchemaComponent::GetBlockDefinitionForBlockWorldPos(const FIntVector& BlockWorldPos, FGameplayTag& OutBlockTypeName, FInstancedStruct& OutDefinition) const
{
	if (!bBlockDefinitionLookupReady)
	{
		UE_LOG(LogBlockTypeSchemaComponent, Warning, TEXT("Cannot reconstruct block definition on %s because the lookup maps are not ready."), *GetNameSafe(GetOwner()));
		return false;
	}

	AChunkWorldBase* ChunkWorld = Cast<AChunkWorldBase>(GetOwner());
	if (ChunkWorld == nullptr)
	{
		UE_LOG(LogBlockTypeSchemaComponent, Warning, TEXT("Cannot reconstruct block definition on %s because the owning actor is not a chunk world base."), *GetNameSafe(GetOwner()));
		return false;
	}

	const int32 MaterialIndex = ChunkWorld->GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::MaterialIndex);
	const int32 MeshIndex = ChunkWorld->GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::MeshIndex);
	const FBlockTypeSchema* BlockType = FindBlockTypeSchemaForIndexes(MaterialIndex, MeshIndex);
	if (BlockType == nullptr || !BlockType->Definition.IsValid())
	{
		UE_LOG(
			LogBlockTypeSchemaComponent,
			Warning,
			TEXT("Cannot reconstruct block definition on %s at %s because no valid block type schema was found for material index %d and mesh index %d."),
			*GetNameSafe(GetOwner()),
			*BlockWorldPos.ToString(),
			MaterialIndex,
			MeshIndex);
		return false;
	}

	OutBlockTypeName = BlockType->BlockTypeName;
	OutDefinition = BlockType->Definition;
	return true;
}

const FBlockTypeSchema* UBlockTypeSchemaComponent::FindBlockTypeSchemaForIndexes(int32 MaterialIndex, int32 MeshIndex) const
{
	if (BlockTypeSchemaRegistry == nullptr)
	{
		return nullptr;
	}

	AChunkWorldBase* ChunkWorld = Cast<AChunkWorldBase>(GetOwner());
	if (ChunkWorld == nullptr || ChunkWorld->WorldGenDef == nullptr)
	{
		return nullptr;
	}

	const TArray<FMaterialDef>& MaterialDefs = ChunkWorld->WorldGenDef->WorldMaterialDefs;
	const TArray<FMeshEntry>& MeshEntries = ChunkWorld->WorldGenDef->WorldMeshes;

	FString MaterialPath;
	bool bHasMaterialPath = false;
	if (MaterialIndex != EmptyMaterial)
	{
		for (const FMaterialDef& MaterialDef : MaterialDefs)
		{
			if (MaterialDef.MaterialIndex != MaterialIndex || !IsValid(MaterialDef.Material))
			{
				continue;
			}

			MaterialPath = MaterialDef.Material->GetPathName();
			bHasMaterialPath = true;
			break;
		}
	}

	FString MeshPath;
	bool bHasMeshPath = false;
	if (MeshIndex != EmptyMesh)
	{
		for (const FMeshEntry& MeshEntry : MeshEntries)
		{
			if (MeshEntry.MeshIndex != MeshIndex || !IsValid(MeshEntry.Mesh))
			{
				continue;
			}

			MeshPath = MeshEntry.Mesh->GetPathName();
			bHasMeshPath = true;
			break;
		}
	}

	const FBlockTypeSchema* MaterialMatch = nullptr;
	for (const FBlockTypeSchema& BlockType : BlockTypeSchemaRegistry->GetBlockTypeDefinitions())
	{
		const FBlockDefinitionBase* Definition = BlockType.Definition.GetPtr<FBlockDefinitionBase>();
		if (Definition == nullptr)
		{
			continue;
		}

		const bool bMaterialMatches = bHasMaterialPath
			&& !Definition->MaterialAsset.IsNull()
			&& Definition->MaterialAsset.ToSoftObjectPath().ToString().Equals(MaterialPath, ESearchCase::CaseSensitive);
		if (bMaterialMatches)
		{
			MaterialMatch = &BlockType;
		}

		const bool bMeshMatches = bHasMeshPath
			&& !Definition->MeshAsset.IsNull()
			&& Definition->MeshAsset.ToSoftObjectPath().ToString().Equals(MeshPath, ESearchCase::CaseSensitive);
		if (bMeshMatches)
		{
			return &BlockType;
		}
	}

	return MaterialMatch;
}

bool UBlockTypeSchemaComponent::InitializeBlockCustomData(const FIntVector& BlockWorldPos)
{
	if (!bBlockDefinitionLookupReady)
	{
		UE_LOG(LogBlockTypeSchemaComponent, Warning, TEXT("Cannot initialize block custom data on %s because the lookup maps are not ready."), *GetNameSafe(GetOwner()));
		return false;
	}

	AChunkWorldBase* ChunkWorld = Cast<AChunkWorldBase>(GetOwner());
	if (ChunkWorld == nullptr)
	{
		UE_LOG(LogBlockTypeSchemaComponent, Warning, TEXT("Cannot initialize block custom data on %s because the owning actor is not a chunk world base."), *GetNameSafe(GetOwner()));
		return false;
	}

	const int32 MaterialIndex = ChunkWorld->GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::MaterialIndex);
	const int32 MeshIndex = ChunkWorld->GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::MeshIndex);
	const FBlockTypeSchema* BlockType = FindBlockTypeSchemaForIndexes(MaterialIndex, MeshIndex);
	if (BlockType == nullptr || !BlockType->CustomData.IsValid())
	{
		return false;
	}

	const UScriptStruct* StructType = BlockType->CustomData.GetScriptStruct();
	const FBlockCustomDataLayout* Layout = GetOrBuildBlockCustomDataLayout(StructType);
	if (Layout == nullptr)
	{
		return false;
	}

	TArray<int32> PackedCustomData;
	if (!Layout->Pack(BlockType->CustomData.GetMemory(), PackedCustomData))
	{
		return false;
	}

	// Reserve one trailing runtime slot as a materialization marker so we can detect first-time initialization without a side cache.
	PackedCustomData.Add(1);

	const int32 MaterializationSlotIndex = PackedCustomData.Num() - 1;
	const int32 StoredMarker = ChunkWorld->GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::CustomData, MaterializationSlotIndex);
	if (StoredMarker != 0)
	{
		return false;
	}

	WriteBlockCustomDataSlots(BlockWorldPos, PackedCustomData);

	return true;
}

bool UBlockTypeSchemaComponent::IsBlockCustomDataMaterialized(const FIntVector& BlockWorldPos) const
{
	if (!bBlockDefinitionLookupReady)
	{
		return false;
	}

	AChunkWorldBase* ChunkWorld = Cast<AChunkWorldBase>(GetOwner());
	if (ChunkWorld == nullptr)
	{
		return false;
	}

	const int32 MaterialIndex = ChunkWorld->GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::MaterialIndex);
	const int32 MeshIndex = ChunkWorld->GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::MeshIndex);
	const FBlockTypeSchema* BlockType = FindBlockTypeSchemaForIndexes(MaterialIndex, MeshIndex);
	if (BlockType == nullptr || !BlockType->CustomData.IsValid())
	{
		return false;
	}

	const UScriptStruct* StructType = BlockType->CustomData.GetScriptStruct();
	const FBlockCustomDataLayout* Layout = GetOrBuildBlockCustomDataLayout(StructType);
	if (Layout == nullptr)
	{
		return false;
	}

	const int32 MaterializationSlotIndex = Layout->GetValueSlotCount();
	return ChunkWorld->GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::CustomData, MaterializationSlotIndex) != 0;
}

bool UBlockTypeSchemaComponent::GetBlockSwapDefinitionForBlockWorldPos(const FIntVector& BlockWorldPos, FGameplayTag& OutBlockTypeName, FChunkWorldBlockSwapDefinition& OutSwapDefinition, bool& bOutAllowSwap, bool bInitializeCustomDataIfNeeded)
{
	OutBlockTypeName = FGameplayTag();
	OutSwapDefinition = FChunkWorldBlockSwapDefinition();
	bOutAllowSwap = false;

	if (!bBlockDefinitionLookupReady)
	{
		UE_LOG(LogBlockTypeSchemaComponent, Warning, TEXT("Cannot resolve block swap definition on %s because the lookup maps are not ready."), *GetNameSafe(GetOwner()));
		return false;
	}

	AChunkWorldBase* ChunkWorld = Cast<AChunkWorldBase>(GetOwner());
	if (ChunkWorld == nullptr)
	{
		UE_LOG(LogBlockTypeSchemaComponent, Warning, TEXT("Cannot resolve block swap definition on %s because the owning actor is not a chunk world base."), *GetNameSafe(GetOwner()));
		return false;
	}

	const int32 MaterialIndex = ChunkWorld->GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::MaterialIndex);
	const int32 MeshIndex = ChunkWorld->GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::MeshIndex);
	const FBlockTypeSchema* BlockType = FindBlockTypeSchemaForIndexes(MaterialIndex, MeshIndex);
	if (BlockType == nullptr || !BlockType->Definition.IsValid())
	{
		return false;
	}

	const FBlockDefinitionBase* Definition = BlockType->Definition.GetPtr<FBlockDefinitionBase>();
	if (Definition == nullptr)
	{
		return false;
	}

	OutBlockTypeName = BlockType->BlockTypeName;
	OutSwapDefinition.SwapActorClass = Definition->SwapActorClass;
	OutSwapDefinition.bSpawnActor = Definition->bSpawnSwapActor;
	OutSwapDefinition.SwapInDistance = Definition->SwapInDistance;
	OutSwapDefinition.SwapOutDistance = Definition->SwapOutDistance;

	const FBlockCustomDataBase* AuthoredCustomData = BlockType->CustomData.GetPtr<FBlockCustomDataBase>();
	if (AuthoredCustomData != nullptr)
	{
		bOutAllowSwap = AuthoredCustomData->bAllowSwap;
	}

	if (bInitializeCustomDataIfNeeded && ChunkWorld->GetNetMode() != NM_Client)
	{
		(void)InitializeBlockCustomData(BlockWorldPos);
	}

	FGameplayTag RuntimeBlockTypeName;
	FBlockCustomDataBase RuntimeCustomData;
	if (GetBlockCustomDataForBlockWorldPos(BlockWorldPos, RuntimeBlockTypeName, RuntimeCustomData))
	{
		OutBlockTypeName = RuntimeBlockTypeName;
		bOutAllowSwap = RuntimeCustomData.bAllowSwap;
	}

	return true;
}

void UBlockTypeSchemaComponent::RebuildBlockDefinitionLookupMaps()
{
	ClearBlockDefinitionLookupMaps();

	if (BlockTypeSchemaRegistry == nullptr)
	{
		UE_LOG(LogBlockTypeSchemaComponent, Warning, TEXT("Cannot build block definition lookup maps on %s because no block type schema registry is assigned."), *GetNameSafe(GetOwner()));
		return;
	}

	AChunkWorld* ChunkWorld = Cast<AChunkWorld>(GetOwner());
	if (ChunkWorld == nullptr || ChunkWorld->WorldGenDef == nullptr)
	{
		UE_LOG(LogBlockTypeSchemaComponent, Warning, TEXT("Cannot build block definition lookup maps on %s because the owning chunk world has no WorldGenDef."), *GetNameSafe(GetOwner()));
		return;
	}

	const TArray<FMaterialDef>& MaterialDefs = ChunkWorld->WorldGenDef->WorldMaterialDefs;
	const TArray<FMeshEntry>& MeshEntries = ChunkWorld->WorldGenDef->WorldMeshes;

	for (const FBlockTypeSchema& BlockType : BlockTypeSchemaRegistry->GetBlockTypeDefinitions())
	{
		const FBlockDefinitionBase* Definition = BlockType.Definition.GetPtr<FBlockDefinitionBase>();
		if (Definition == nullptr)
		{
			continue;
		}

		int32 MaterialIndex = EmptyMaterial;
		if (!Definition->MaterialAsset.IsNull())
		{
			const UMaterialInterface* LoadedMaterial = Definition->MaterialAsset.Get();
			const FString MaterialPath = Definition->MaterialAsset.ToSoftObjectPath().ToString();
			for (const FMaterialDef& MaterialDef : MaterialDefs)
			{
				if (!IsValid(MaterialDef.Material))
				{
					continue;
				}

				if ((LoadedMaterial != nullptr && MaterialDef.Material == LoadedMaterial)
					|| (!MaterialPath.IsEmpty() && MaterialDef.Material->GetPathName().Equals(MaterialPath, ESearchCase::CaseSensitive)))
				{
					MaterialIndex = MaterialDef.MaterialIndex;
					break;
				}
			}
		}

		int32 MeshIndex = EmptyMesh;
		if (!Definition->MeshAsset.IsNull())
		{
			const UStaticMesh* LoadedMesh = Definition->MeshAsset.Get();
			const FString MeshPath = Definition->MeshAsset.ToSoftObjectPath().ToString();
			for (const FMeshEntry& MeshEntry : MeshEntries)
			{
				if (!IsValid(MeshEntry.Mesh))
				{
					continue;
				}

				if ((LoadedMesh != nullptr && MeshEntry.Mesh == LoadedMesh)
					|| (!MeshPath.IsEmpty() && MeshEntry.Mesh->GetPathName().Equals(MeshPath, ESearchCase::CaseSensitive)))
				{
					MeshIndex = MeshEntry.MeshIndex;
					break;
				}
			}
		}

		if (MaterialIndex != EmptyMaterial)
		{
			if (MaterialDefinitionLookup.Contains(MaterialIndex))
			{
				UE_LOG(LogBlockTypeSchemaComponent, Warning, TEXT("Duplicate material index %d while building block definition lookup maps on %s."), MaterialIndex, *GetNameSafe(GetOwner()));
			}
			MaterialDefinitionLookup.Add(MaterialIndex, BlockType.Definition);
		}

		if (MeshIndex != EmptyMesh)
		{
			if (MeshDefinitionLookup.Contains(MeshIndex))
			{
				UE_LOG(LogBlockTypeSchemaComponent, Warning, TEXT("Duplicate mesh index %d while building block definition lookup maps on %s."), MeshIndex, *GetNameSafe(GetOwner()));
			}
			MeshDefinitionLookup.Add(MeshIndex, BlockType.Definition);
		}
	}

	bBlockDefinitionLookupReady = true;
}

void UBlockTypeSchemaComponent::ClearBlockDefinitionLookupMaps()
{
	MaterialDefinitionLookup.Reset();
	MeshDefinitionLookup.Reset();
	bBlockDefinitionLookupReady = false;
}

void UBlockTypeSchemaComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearBlockDefinitionLookupMaps();
	Super::EndPlay(EndPlayReason);
}
