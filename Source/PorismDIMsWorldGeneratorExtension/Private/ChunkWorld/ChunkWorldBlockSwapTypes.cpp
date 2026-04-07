// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/ChunkWorldBlockSwapTypes.h"

#include "ChunkWorld/Components/ChunkWorldBlockSwapComponent.h"

bool FReplicatedChunkWorldSwapArray::NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
{
	return FFastArraySerializer::FastArrayDeltaSerialize<FReplicatedChunkWorldSwapItem, FReplicatedChunkWorldSwapArray>(Items, DeltaParms, *this);
}

void FReplicatedChunkWorldSwapArray::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
	if (Owner == nullptr)
	{
		return;
	}

	for (const int32 RemovedIndex : RemovedIndices)
	{
		if (Items.IsValidIndex(RemovedIndex))
		{
			Owner->HandleReplicatedSwapRemoved(Items[RemovedIndex]);
		}
	}
}

void FReplicatedChunkWorldSwapArray::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	if (Owner == nullptr)
	{
		return;
	}

	for (const int32 AddedIndex : AddedIndices)
	{
		if (Items.IsValidIndex(AddedIndex))
		{
			Owner->HandleReplicatedSwapAdded(Items[AddedIndex]);
		}
	}
}

void FReplicatedChunkWorldSwapArray::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	if (Owner == nullptr)
	{
		return;
	}

	for (const int32 ChangedIndex : ChangedIndices)
	{
		if (Items.IsValidIndex(ChangedIndex))
		{
			Owner->HandleReplicatedSwapChanged(Items[ChangedIndex]);
		}
	}
}

FReplicatedChunkWorldSwapItem* FReplicatedChunkWorldSwapArray::FindByBlockWorldPos(const FIntVector& BlockWorldPos)
{
	return Items.FindByPredicate(
		[&BlockWorldPos](const FReplicatedChunkWorldSwapItem& Item)
		{
			return Item.BlockWorldPos == BlockWorldPos;
		});
}

const FReplicatedChunkWorldSwapItem* FReplicatedChunkWorldSwapArray::FindByBlockWorldPos(const FIntVector& BlockWorldPos) const
{
	return Items.FindByPredicate(
		[&BlockWorldPos](const FReplicatedChunkWorldSwapItem& Item)
		{
			return Item.BlockWorldPos == BlockWorldPos;
		});
}
