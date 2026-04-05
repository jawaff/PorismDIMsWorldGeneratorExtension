// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldExtended.h"

#include "Actor/Components/PorismPredictedBlockStateComponent.h"
#include "Block/BlockTypeSchemaRegistry.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockFeedbackComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockSwapComponent.h"
#include "PorismDIMsWorldGeneratorExtension.h"

DEFINE_LOG_CATEGORY_STATIC(LogChunkWorldExtended, Log, All);

AChunkWorldExtended::AChunkWorldExtended()
{
	BlockTypeSchemaComponent = CreateDefaultSubobject<UBlockTypeSchemaComponent>(TEXT("BlockTypeSchemaComponent"));
	BlockFeedbackComponent = CreateDefaultSubobject<UChunkWorldBlockFeedbackComponent>(TEXT("BlockFeedbackComponent"));
	BlockSwapComponent = CreateDefaultSubobject<UChunkWorldBlockSwapComponent>(TEXT("BlockSwapComponent"));
	SyncBlockTypeSchemaRegistry();
}

void AChunkWorldExtended::StartGen()
{
	SyncBlockTypeSchemaRegistry();
	Super::StartGen();

	if (BlockTypeSchemaComponent != nullptr)
	{
		// Build the lookup tables after the base chunk world has populated its runtime material and mesh indexes.
		BlockTypeSchemaComponent->RebuildBlockDefinitionLookupMaps();
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
	if (BlockTypeSchemaComponent != nullptr)
	{
		BlockTypeSchemaComponent->SetBlockTypeSchemaRegistry(BlockTypeSchemaRegistry);
	}
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
		OnAuthoritativeBlockCustomDataUpdated.Broadcast(BlockWorldPos);
		UPorismPredictedBlockStateComponent::NotifyAuthoritativeBlockStateUpdated(this, BlockWorldPos);
	}
}
