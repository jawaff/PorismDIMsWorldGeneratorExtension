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
	FChunkWorldBlockSwapDefinition SwapDefinition;
	bool bAllowSwap = false;
	if (SchemaComponent->GetBlockSwapDefinitionForBlockWorldPos(BlockWorldPos, BlockTypeName, SwapDefinition, bAllowSwap))
	{
		return bAllowSwap && SwapDefinition.IsConfigured();
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
		CacheOriginalBlockState(BlockWorldPos, Backup);
		SwapBackups.Add(BlockWorldPos, Backup);

		FMeshData EmptyMeshData;
		EmptyMeshData.MeshId = EmptyMesh;

		ChunkWorld->SetMeshDataByBlockWorldPos(BlockWorldPos, EmptyMeshData, true);
		ChunkWorld->SetBlockValueByBlockWorldPos(BlockWorldPos, EmptyMaterial, true);
		return;
	}

	const FBlockSwapBackup* Backup = SwapBackups.Find(BlockWorldPos);
	if (Backup == nullptr)
	{
		return;
	}

	ChunkWorld->SetMeshDataByBlockWorldPos(BlockWorldPos, Backup->bHasMesh ? Backup->MeshData : FMeshData(), true);
	ChunkWorld->SetBlockValueByBlockWorldPos(BlockWorldPos, Backup->MaterialIndex, true);
	SwapBackups.Remove(BlockWorldPos);
}

void UChunkWorldBlockSwapComponent::CacheOriginalBlockState(const FIntVector& BlockWorldPos, FBlockSwapBackup& OutBackup) const
{
	AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner();
	if (ChunkWorld == nullptr)
	{
		return;
	}

	OutBackup.MaterialIndex = ChunkWorld->GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::MaterialIndex, 0);
	OutBackup.MeshData = ChunkWorld->GetMeshDataByBlockWorldPos(BlockWorldPos);
	OutBackup.bHasMesh = OutBackup.MeshData.MeshId != EmptyMesh;
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
