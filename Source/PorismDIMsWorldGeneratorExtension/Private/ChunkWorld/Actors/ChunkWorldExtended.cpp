// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldExtended.h"

#include "Block/BlockTypeSchemaRegistry.h"
#include "ChunkWorld/Blueprint/ChunkWorldHitBlueprintLibrary.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockDamageBlueprintLibrary.h"
#include "ChunkWorld/Components/ChunkWorldBlockFeedbackComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockSwapScannerComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockSwapComponent.h"
#include "ChunkWorldStructs/ChunkWorldRuntimeStructs.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "PorismDIMsWorldGeneratorExtension.h"

DEFINE_LOG_CATEGORY_STATIC(LogChunkWorldExtended, Log, All);

namespace
{
	void EnsureCustomFeatureDefaultCapacity(TArray<int32>& CustomFeatureDefaultData, int32 RequiredChannelCount)
	{
		if (RequiredChannelCount > 0 && CustomFeatureDefaultData.Num() < RequiredChannelCount)
		{
			CustomFeatureDefaultData.SetNumZeroed(RequiredChannelCount);
		}
	}

	void LogCustomDataStartupState(
		const AChunkWorldExtended* ChunkWorld,
		const TCHAR* StageLabel,
		int32 RequiredCustomDataChannels)
	{
		const int32 WorldGenDefChannels = ChunkWorld != nullptr && ChunkWorld->WorldGenDef != nullptr
			? ChunkWorld->WorldGenDef->CustomFeatureDefaultData.Num()
			: INDEX_NONE;
		const int32 RuntimeChannels = ChunkWorld != nullptr && ChunkWorld->RuntimeConfig != nullptr
			? ChunkWorld->RuntimeConfig->CustomFeatureDefaultData.Num()
			: INDEX_NONE;
		const int32 CustomSaveCount = ChunkWorld != nullptr
			? static_cast<int32>(ChunkWorld->CustomSave.size())
			: INDEX_NONE;

		UE_LOG(
			LogChunkWorldExtended,
			Warning,
			TEXT("Temporary custom-data startup diagnostic [%s] ChunkWorld=%s RequiredChannels=%d WorldGenDefChannels=%d RuntimeChannels=%d CustomSaveCount=%d WorldGenDef=%s Registry=%s"),
			StageLabel,
			*GetNameSafe(ChunkWorld),
			RequiredCustomDataChannels,
			WorldGenDefChannels,
			RuntimeChannels,
			CustomSaveCount,
			*GetNameSafe(ChunkWorld != nullptr ? ChunkWorld->WorldGenDef : nullptr),
			*GetNameSafe(ChunkWorld != nullptr ? ChunkWorld->GetBlockTypeSchemaRegistry() : nullptr));
	}

}

AChunkWorldExtended::AChunkWorldExtended()
{
	BlockTypeSchemaComponent = CreateDefaultSubobject<UBlockTypeSchemaComponent>(TEXT("BlockTypeSchemaComponent"));
	BlockFeedbackComponent = CreateDefaultSubobject<UChunkWorldBlockFeedbackComponent>(TEXT("BlockFeedbackComponent"));
	BlockSwapScannerComponent = CreateDefaultSubobject<UChunkWorldBlockSwapScannerComponent>(TEXT("BlockSwapScannerComponent"));
	BlockSwapComponent = CreateDefaultSubobject<UChunkWorldBlockSwapComponent>(TEXT("BlockSwapComponent"));
	SyncBlockTypeSchemaRegistry();
}

SCacheKey AChunkWorldExtended::StartGenDTs()
{
	SyncBlockTypeSchemaRegistry();
	EnsureSchemaCustomDataCapacity();
	LogCustomDataStartupState(this, TEXT("BeforeSuperStartGenDTs"), BlockTypeSchemaComponent != nullptr ? BlockTypeSchemaComponent->GetRequiredCustomDataChannelCount() : 0);

	SCacheKey Result = Super::StartGenDTs();

	LogCustomDataStartupState(this, TEXT("AfterSuperStartGenDTs"), BlockTypeSchemaComponent != nullptr ? BlockTypeSchemaComponent->GetRequiredCustomDataChannelCount() : 0);
	return Result;
}

void AChunkWorldExtended::StartGen()
{
	SyncBlockTypeSchemaRegistry();
	LogCustomDataStartupState(this, TEXT("BeforeSuperStartGen"), BlockTypeSchemaComponent != nullptr ? BlockTypeSchemaComponent->GetRequiredCustomDataChannelCount() : 0);

	Super::StartGen();
	LogCustomDataStartupState(this, TEXT("AfterSuperStartGen"), BlockTypeSchemaComponent != nullptr ? BlockTypeSchemaComponent->GetRequiredCustomDataChannelCount() : 0);

	if (BlockTypeSchemaComponent != nullptr)
	{
		// Build the lookup tables after the base chunk world has populated its runtime material and mesh indexes.
		BlockTypeSchemaComponent->RebuildBlockDefinitionLookupMaps();
		if (!BlockTypeSchemaComponent->IsBlockDefinitionLookupReady())
		{
			UE_LOG(
				LogChunkWorldExtended,
				Warning,
				TEXT("Chunk world '%s' failed to build schema lookup maps during StartGen. Registry=%s WorldGenDef=%s"),
				*GetNameSafe(this),
				*GetNameSafe(BlockTypeSchemaRegistry),
				*GetNameSafe(WorldGenDef));
		}
	}
}

void AChunkWorldExtended::EnsureSchemaCustomDataCapacity()
{
	if (BlockTypeSchemaComponent == nullptr)
	{
		return;
	}

	// Schema-defined custom-data storage should be provisioned automatically so projects do not
	// need to author RuntimeConfig/WorldGenDef channel counts by hand.
	const int32 RequiredCustomDataChannels = BlockTypeSchemaComponent->GetRequiredCustomDataChannelCount();
	if (WorldGenDef != nullptr)
	{
		EnsureCustomFeatureDefaultCapacity(WorldGenDef->CustomFeatureDefaultData, RequiredCustomDataChannels);
	}
	if (RuntimeConfig != nullptr)
	{
		EnsureCustomFeatureDefaultCapacity(RuntimeConfig->CustomFeatureDefaultData, RequiredCustomDataChannels);
	}
}

UBlockTypeSchemaComponent* AChunkWorldExtended::GetBlockTypeSchemaComponent() const
{
	return BlockTypeSchemaComponent;
}

UChunkWorldBlockFeedbackComponent* AChunkWorldExtended::GetBlockFeedbackComponent() const
{
	return BlockFeedbackComponent;
}

UChunkWorldBlockSwapScannerComponent* AChunkWorldExtended::GetBlockSwapScannerComponent() const
{
	return BlockSwapScannerComponent;
}

UChunkWorldBlockSwapComponent* AChunkWorldExtended::GetBlockSwapComponent() const
{
	return BlockSwapComponent;
}

void AChunkWorldExtended::PostLoad()
{
	Super::PostLoad();
	SyncBlockTypeSchemaRegistry();
}

#if WITH_EDITOR
void AChunkWorldExtended::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SyncBlockTypeSchemaRegistry();
}
#endif

void AChunkWorldExtended::SyncBlockTypeSchemaRegistry()
{
	if (BlockTypeSchemaComponent == nullptr)
	{
		return;
	}

	// Project-side migration note: older chunk world Blueprints may still have the schema registry serialized on the
	// component from before the actor-level property became the single source of truth. Adopt that value so runtime
	// block lookup continues to work until those assets are resaved.
	if (BlockTypeSchemaRegistry == nullptr)
	{
		if (UBlockTypeSchemaRegistry* LegacyComponentRegistry = BlockTypeSchemaComponent->GetBlockTypeSchemaRegistry())
		{
			BlockTypeSchemaRegistry = LegacyComponentRegistry;
			UE_LOG(
				LogChunkWorldExtended,
				Warning,
				TEXT("Chunk world '%s' adopted legacy schema registry '%s' from its BlockTypeSchemaComponent. Resave the asset so the actor-level registry becomes authoritative."),
				*GetNameSafe(this),
				*GetNameSafe(BlockTypeSchemaRegistry));
		}
	}

	BlockTypeSchemaComponent->SetBlockTypeSchemaRegistry(BlockTypeSchemaRegistry);
}

void AChunkWorldExtended::WriteCustomDataValuesAndUpdate(const TArray<SCustomDataChangeCall>& NetCustomDataChangeCalls)
{
	Super::WriteCustomDataValuesAndUpdate(NetCustomDataChangeCalls);

	for (const SCustomDataChangeCall& ChangeCall : NetCustomDataChangeCalls)
	{
		if (ChangeCall.chunkDataIndex < 0
			|| ChangeCall.chunkDataIndex >= static_cast<int32>(WorldChunks.size()))
		{
			continue;
		}

		const CChunkData* ChunkData = WorldChunks[ChangeCall.chunkDataIndex];
		if (ChunkData == nullptr)
		{
			continue;
		}

		const FIntVector BlockWorldPos = ChunkGridPosToBlockWorldPos(ChangeCall.chunkPose, ChunkData)
			+ BlockChunkIndexToBlockChunkPos(ChangeCall.blockChunkIndex, ChunkData);
		OnAuthoritativeBlockCustomDataUpdated.Broadcast(this, BlockWorldPos);
	}
}

void AChunkWorldExtended::ProcessAuthoritativeCustomDataChanges(const FIntVector& BlockWorldPos, const TArray<int32>& PackedValues)
{
	if (!HasAuthority() || PackedValues.Num() <= 0)
	{
		return;
	}

	if (BlockTypeSchemaComponent == nullptr)
	{
		OnAuthoritativeBlockCustomDataUpdated.Broadcast(this, BlockWorldPos);
		return;
	}

	const int32 HealthCustomDataIndex = BlockTypeSchemaComponent->GetBlockDamageHealthCustomDataIndex();
	if (HealthCustomDataIndex != INDEX_NONE && PackedValues.IsValidIndex(HealthCustomDataIndex))
	{
		FGameplayTag BlockTypeName;
		FInstancedStruct BlockCustomData;
		if (BlockTypeSchemaComponent->GetBlockCustomDataForBlockWorldPos(BlockWorldPos, BlockTypeName, BlockCustomData))
		{
			const UScriptStruct* CustomDataStruct = BlockCustomData.GetScriptStruct();
			if (CustomDataStruct != nullptr
				&& CustomDataStruct->IsChildOf(FBlockDamageCustomData::StaticStruct())
				&& PackedValues[HealthCustomDataIndex] <= 0)
			{
				DestroyBlock(BlockWorldPos, true);
			}
		}
	}

	OnAuthoritativeBlockCustomDataUpdated.Broadcast(this, BlockWorldPos);
}

bool AChunkWorldExtended::DestroyBlock(const FIntVector& BlockWorldPos, bool bRefreshChunks)
{
	if (!HasAuthority())
	{
		return false;
	}

	if (BlockSwapScannerComponent != nullptr)
	{
		(void)BlockSwapScannerComponent->ForceRemoveSwapForDestroyedBlock(BlockWorldPos);
	}

	FChunkWorldResolvedBlockHit DestroyedFeedbackHit;
	const bool bHasDestroyedFeedbackHit = UChunkWorldHitBlueprintLibrary::TryResolveBlockHitContextFromBlockWorldPos(this, BlockWorldPos, DestroyedFeedbackHit);

	FGameplayTag BlockTypeName;
	FBlockDefinitionBase Definition;
	if (BlockTypeSchemaComponent != nullptr && BlockTypeSchemaComponent->GetBlockDefinitionForBlockWorldPos(BlockWorldPos, BlockTypeName, Definition))
	{
		if (!Definition.MeshAsset.IsNull())
		{
			FMeshData EmptyMeshData;
			EmptyMeshData.MeshId = EmptyMesh;
			if (bHasDestroyedFeedbackHit)
			{
				(void)UChunkWorldHitBlueprintLibrary::TryBroadcastDestroyedFeedbackForResolvedBlockHit(DestroyedFeedbackHit);
			}
			SetMeshDataByBlockWorldPos(BlockWorldPos, EmptyMeshData, bRefreshChunks);
		    return true;
		}

		if (!Definition.MaterialAsset.IsNull())
		{
			if (bHasDestroyedFeedbackHit)
			{
				(void)UChunkWorldHitBlueprintLibrary::TryBroadcastDestroyedFeedbackForResolvedBlockHit(DestroyedFeedbackHit);
			}
			SetBlockValueByBlockWorldPos(BlockWorldPos, EmptyMaterial, bRefreshChunks);
		    return true;
		}
	}

	// Fallback for unexpected/runtime-only states: preserve old behavior if the authored association cannot be resolved.
	const int32 MaterialIndex = GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::MaterialIndex, 0);
	const int32 MeshIndex = GetMeshDataByBlockWorldPos(BlockWorldPos).MeshId;
	if (MeshIndex != EmptyMesh && MeshIndex != DefaultMesh)
	{
		FMeshData EmptyMeshData;
		EmptyMeshData.MeshId = EmptyMesh;
		if (bHasDestroyedFeedbackHit)
		{
			(void)UChunkWorldHitBlueprintLibrary::TryBroadcastDestroyedFeedbackForResolvedBlockHit(DestroyedFeedbackHit);
		}
		SetMeshDataByBlockWorldPos(BlockWorldPos, EmptyMeshData, bRefreshChunks);
		return true;
	}

	if (MaterialIndex != EmptyMaterial)
	{
		if (bHasDestroyedFeedbackHit)
		{
			(void)UChunkWorldHitBlueprintLibrary::TryBroadcastDestroyedFeedbackForResolvedBlockHit(DestroyedFeedbackHit);
		}
		SetBlockValueByBlockWorldPos(BlockWorldPos, EmptyMaterial, bRefreshChunks);
		return true;
	}
	
	return false;
}
