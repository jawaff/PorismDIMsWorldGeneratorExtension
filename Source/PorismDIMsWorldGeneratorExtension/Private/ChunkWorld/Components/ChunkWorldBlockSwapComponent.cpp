// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Components/ChunkWorldBlockSwapComponent.h"

#include "Block/BlockTypeSchemaRegistry.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"

UChunkWorldBlockSwapComponent::UChunkWorldBlockSwapComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

bool UChunkWorldBlockSwapComponent::TryApplySwapRequest(const FIntVector& BlockWorldPos, bool bEntering)
{
	AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner();
	if (ChunkWorld == nullptr || !ChunkWorld->HasAuthority())
	{
		return false;
	}

	if (bEntering && !CanApplySwapRequest(BlockWorldPos))
	{
		return false;
	}

	MulticastApplyBlockSwap(BlockWorldPos, bEntering);
	return true;
}

void UChunkWorldBlockSwapComponent::MulticastApplyBlockSwap_Implementation(const FIntVector& BlockWorldPos, bool bEntering)
{
	ApplySwapHiddenState(BlockWorldPos, bEntering);
}

bool UChunkWorldBlockSwapComponent::CanApplySwapRequest(const FIntVector& BlockWorldPos) const
{
	if (SwapBackups.Contains(BlockWorldPos))
	{
		return false;
	}

	UBlockTypeSchemaComponent* SchemaComponent = GetSchemaComponent();
	if (SchemaComponent == nullptr)
	{
		return true;
	}

	FGameplayTag BlockTypeName;
	FBlockDefinitionBase Definition;
	if (SchemaComponent->GetBlockDefinitionForBlockWorldPos(BlockWorldPos, BlockTypeName, Definition))
	{
		return !Definition.SwapActorClass.IsNull();
	}

	return false;
}

void UChunkWorldBlockSwapComponent::ApplySwapHiddenState(const FIntVector& BlockWorldPos, bool bEntering)
{
	AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner();
	if (ChunkWorld == nullptr)
	{
		return;
	}

	if (bEntering)
	{
		if (SwapBackups.Contains(BlockWorldPos))
		{
			return;
		}

		FBlockSwapBackup Backup;
		if (CacheOriginalBlockState(BlockWorldPos, Backup))
		{
		    SwapBackups.Add(BlockWorldPos, Backup);
		    ChunkWorld->DestroyBlock(BlockWorldPos, true);
		}

		return;
	}

	const FBlockSwapBackup* Backup = SwapBackups.Find(BlockWorldPos);
	if (Backup == nullptr)
	{
		return;
	}

	if (Backup->bHasMesh)
	{
	    ChunkWorld->SetMeshDataByBlockWorldPos(BlockWorldPos, Backup->MeshData, true);
	}
	
	SwapBackups.Remove(BlockWorldPos);
}

bool UChunkWorldBlockSwapComponent::CacheOriginalBlockState(const FIntVector& BlockWorldPos, FBlockSwapBackup& OutBackup) const
{
	AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner();
	if (ChunkWorld == nullptr)
	{
		return false;
	}

	OutBackup.MeshData = ChunkWorld->GetMeshDataByBlockWorldPos(BlockWorldPos);
	OutBackup.bHasMesh = OutBackup.MeshData.MeshId != EmptyMesh;
	return OutBackup.bHasMesh;
}

AChunkWorldExtended* UChunkWorldBlockSwapComponent::GetChunkWorldOwner() const
{
	return Cast<AChunkWorldExtended>(GetOwner());
}

UBlockTypeSchemaComponent* UChunkWorldBlockSwapComponent::GetSchemaComponent() const
{
	AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner();
	return ChunkWorld != nullptr ? ChunkWorld->GetBlockTypeSchemaComponent() : nullptr;
}
