// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldExtended.h"

#include "Block/BlockTypeSchemaRegistry.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockHitBlueprintLibrary.h"
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
	struct FObservedBlockCustomDataChange
	{
		bool bTouchedHealth = false;
	};

	void EnsureCustomFeatureDefaultCapacity(TArray<int32>& CustomFeatureDefaultData, int32 RequiredChannelCount)
	{
		if (RequiredChannelCount > 0 && CustomFeatureDefaultData.Num() < RequiredChannelCount)
		{
			CustomFeatureDefaultData.SetNumZeroed(RequiredChannelCount);
		}
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
	return Super::StartGenDTs();
}

void AChunkWorldExtended::StartGen()
{
	SyncBlockTypeSchemaRegistry();
	Super::StartGen();

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

	const int32 HealthCustomDataIndex = BlockTypeSchemaComponent != nullptr
		? BlockTypeSchemaComponent->GetBlockDamageHealthCustomDataIndex()
		: INDEX_NONE;
	TMap<FIntVector, FObservedBlockCustomDataChange> ObservedBlockChanges;

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

		FObservedBlockCustomDataChange& ObservedBlockChange = ObservedBlockChanges.FindOrAdd(BlockWorldPos);
		ObservedBlockChange.bTouchedHealth |= HealthCustomDataIndex != INDEX_NONE && ChangeCall.customDataIndex == HealthCustomDataIndex;
	}

	// Coalesce slot-level replicated writes into one block notification so
	// prediction/UI refresh runs against the settled block view for that apply batch.
	for (const TPair<FIntVector, FObservedBlockCustomDataChange>& ObservedBlockChange : ObservedBlockChanges)
	{
		QueueBlockCustomDataChanged(ObservedBlockChange.Key, ObservedBlockChange.Value.bTouchedHealth, TEXT("ReplicatedApplyBatch"));
	}
}

void AChunkWorldExtended::HandleBlockCustomDataCommit(const FIntVector& BlockWorldPos, const TArray<int32>& PackedValues)
{
	if (!HasAuthority())
	{
		UE_LOG(
			LogChunkWorldExtended,
			Warning,
			TEXT("HandleBlockCustomDataCommit ignored on non-authority chunk world '%s' at block %s."),
			*GetNameSafe(this),
			*BlockWorldPos.ToString());
		return;
	}

	if (PackedValues.Num() <= 0)
	{
		return;
	}

	HandleCommittedBlockCustomData(BlockWorldPos, PackedValues);
}

bool AChunkWorldExtended::DestroyBlock(const FIntVector& BlockWorldPos, bool bRefreshChunks)
{
	if (!HasAuthority())
	{
		UE_LOG(
			LogChunkWorldExtended,
			Warning,
			TEXT("DestroyBlock ignored on non-authority chunk world '%s' at block %s."),
			*GetNameSafe(this),
			*BlockWorldPos.ToString());
		return false;
	}

	if (BlockSwapScannerComponent != nullptr)
	{
		(void)BlockSwapScannerComponent->ForceRemoveSwapForDestroyedBlock(BlockWorldPos);
	}

	FChunkWorldResolvedBlockHit DestroyedFeedbackHit;
	const bool bHasDestroyedFeedbackHit = UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromBlockWorldPos(this, BlockWorldPos, DestroyedFeedbackHit);

	FGameplayTag BlockTypeName;
	FBlockDefinitionBase Definition;
	if (BlockTypeSchemaComponent != nullptr && BlockTypeSchemaComponent->GetBlockDefinitionForBlockWorldPos(BlockWorldPos, BlockTypeName, Definition))
	{
		if (!Definition.MeshAsset.IsNull())
		{
			FMeshData EmptyMeshData;
			EmptyMeshData.MeshId = EmptyMesh;
			if (bHasDestroyedFeedbackHit && BlockFeedbackComponent != nullptr)
			{
				(void)BlockFeedbackComponent->BroadcastAuthoritativeDestroyFeedback(DestroyedFeedbackHit);
			}
			SetMeshDataByBlockWorldPos(BlockWorldPos, EmptyMeshData, bRefreshChunks);
		    return true;
		}

		if (!Definition.MaterialAsset.IsNull())
		{
			if (bHasDestroyedFeedbackHit && BlockFeedbackComponent != nullptr)
			{
				(void)BlockFeedbackComponent->BroadcastAuthoritativeDestroyFeedback(DestroyedFeedbackHit);
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
		if (bHasDestroyedFeedbackHit && BlockFeedbackComponent != nullptr)
		{
			(void)BlockFeedbackComponent->BroadcastAuthoritativeDestroyFeedback(DestroyedFeedbackHit);
		}
		SetMeshDataByBlockWorldPos(BlockWorldPos, EmptyMeshData, bRefreshChunks);
		return true;
	}

	if (MaterialIndex != EmptyMaterial)
	{
		if (bHasDestroyedFeedbackHit && BlockFeedbackComponent != nullptr)
		{
			(void)BlockFeedbackComponent->BroadcastAuthoritativeDestroyFeedback(DestroyedFeedbackHit);
		}
		SetBlockValueByBlockWorldPos(BlockWorldPos, EmptyMaterial, bRefreshChunks);
		return true;
	}
	
	return false;
}

void AChunkWorldExtended::HandleCommittedBlockCustomData(const FIntVector& BlockWorldPos, const TArray<int32>& PackedValues)
{
	const int32 HealthCustomDataIndex = BlockTypeSchemaComponent != nullptr
		? BlockTypeSchemaComponent->GetBlockDamageHealthCustomDataIndex()
		: INDEX_NONE;
	const bool bTouchedHealth = HealthCustomDataIndex != INDEX_NONE && PackedValues.IsValidIndex(HealthCustomDataIndex);

	TryDestroyBlockFromCommittedHealth(BlockWorldPos, bTouchedHealth);
	QueueBlockCustomDataChanged(BlockWorldPos, bTouchedHealth, TEXT("CommittedWrite"));
}

void AChunkWorldExtended::QueueBlockCustomDataChanged(
	const FIntVector& BlockWorldPos,
	const bool bTouchedHealth,
	const TCHAR* SourceLabel)
{
	FDeferredBlockCustomDataChange& DeferredChange = DeferredBlockCustomDataChanges.FindOrAdd(BlockWorldPos);
	DeferredChange.bTouchedHealth |= bTouchedHealth;

	if (bHasDeferredBlockCustomDataFlushQueued)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		bHasDeferredBlockCustomDataFlushQueued = true;
		World->GetTimerManager().SetTimerForNextTick(this, &AChunkWorldExtended::FlushDeferredBlockCustomDataChanges);
	}
}

void AChunkWorldExtended::FlushDeferredBlockCustomDataChanges()
{
	bHasDeferredBlockCustomDataFlushQueued = false;

	TMap<FIntVector, FDeferredBlockCustomDataChange> PendingChanges = MoveTemp(DeferredBlockCustomDataChanges);
	DeferredBlockCustomDataChanges.Reset();

	for (const TPair<FIntVector, FDeferredBlockCustomDataChange>& PendingChange : PendingChanges)
	{
		NotifyBlockCustomDataChanged(PendingChange.Key, PendingChange.Value.bTouchedHealth, TEXT("DeferredBlockUpdate"));
	}
}

void AChunkWorldExtended::NotifyBlockCustomDataChanged(
	const FIntVector& BlockWorldPos,
	const bool bTouchedHealth,
	const TCHAR* /*SourceLabel*/)
{
	OnBlockCustomDataChanged.Broadcast(this, BlockWorldPos, bTouchedHealth);
}

void AChunkWorldExtended::TryDestroyBlockFromCommittedHealth(const FIntVector& BlockWorldPos, const bool bTouchedHealth)
{
	if (!HasAuthority() || BlockTypeSchemaComponent == nullptr)
	{
		return;
	}

	const int32 HealthCustomDataIndex = BlockTypeSchemaComponent->GetBlockDamageHealthCustomDataIndex();
	if (HealthCustomDataIndex == INDEX_NONE)
	{
		return;
	}

	if (!bTouchedHealth)
	{
		return;
	}

	FGameplayTag BlockTypeName;
	FInstancedStruct BlockCustomData;
	if (!BlockTypeSchemaComponent->GetBlockCustomDataForBlockWorldPos(BlockWorldPos, BlockTypeName, BlockCustomData))
	{
		return;
	}

	const FBlockDamageCustomData* DamageCustomData = BlockCustomData.GetPtr<FBlockDamageCustomData>();
	if (DamageCustomData == nullptr || DamageCustomData->Health > 0)
	{
		return;
	}

	(void)DestroyBlock(BlockWorldPos, true);
}
