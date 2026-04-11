// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldExtended.h"

#include "Block/BlockTypeSchemaRegistry.h"
#include "Block/BlockTypeSchemaBlueprintLibrary.h"
#include "ChunkWorld/Actors/ChunkWorldDestructionActorInterface.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockHitBlueprintLibrary.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockDamageBlueprintLibrary.h"
#include "ChunkWorld/Components/ChunkWorldBlockFeedbackComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockSwapScannerComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockSwapComponent.h"
#include "ChunkWorldStructs/ChunkWorldRuntimeStructs.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Kismet/GameplayStatics.h"
#include "PorismDIMsWorldGeneratorExtension.h"

DEFINE_LOG_CATEGORY_STATIC(LogChunkWorldExtended, Log, All);

namespace
{
	struct FObservedBlockCustomDataChange
	{
		bool bTouchedHealth = false;
	};

	bool IsMeshBackedVoxel(const int32 MeshIndex)
	{
		return MeshIndex != EmptyMesh && MeshIndex != DefaultMesh;
	}

	bool IsRepresentedVoxel(const int32 MaterialIndex, const int32 MeshIndex)
	{
		return MaterialIndex != EmptyMaterial || IsMeshBackedVoxel(MeshIndex);
	}

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
	const int32 HealthCustomDataIndex = BlockTypeSchemaComponent != nullptr
		? BlockTypeSchemaComponent->GetBlockDamageHealthCustomDataIndex()
		: INDEX_NONE;
	TMap<FIntVector, FObservedBlockCustomDataChange> ObservedBlockChanges;
	TMap<FIntVector, int32> PreApplyHealthByBlock;
	const bool bShouldObserveSettledReplication = !HasAuthority();

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
		if (bShouldObserveSettledReplication
			&& ObservedBlockChange.bTouchedHealth
			&& !PreApplyHealthByBlock.Contains(BlockWorldPos))
		{
			int32 PreviousHealth = 0;
			if (TryGetObservedRuntimeHealth(BlockWorldPos, PreviousHealth))
			{
				PreApplyHealthByBlock.Add(BlockWorldPos, PreviousHealth);
			}
		}
	}

	Super::WriteCustomDataValuesAndUpdate(NetCustomDataChangeCalls);

	// Coalesce slot-level replicated writes into one block notification so
	// prediction/UI refresh runs against the settled block view for that apply batch.
	for (const TPair<FIntVector, FObservedBlockCustomDataChange>& ObservedBlockChange : ObservedBlockChanges)
	{
		if (bShouldObserveSettledReplication && ObservedBlockChange.Value.bTouchedHealth)
		{
			if (const int32* PreviousHealth = PreApplyHealthByBlock.Find(ObservedBlockChange.Key))
			{
				int32 CurrentHealth = 0;
				if (TryGetObservedRuntimeHealth(ObservedBlockChange.Key, CurrentHealth) && CurrentHealth != *PreviousHealth)
				{
					QueueObservedReplicatedHealthTransition(ObservedBlockChange.Key, *PreviousHealth, CurrentHealth);
				}
			}
		}

		QueueBlockCustomDataChanged(ObservedBlockChange.Key, ObservedBlockChange.Value.bTouchedHealth, TEXT("ReplicatedApplyBatch"));
	}
}

void AChunkWorldExtended::WriteBlockValuesAndUpdate(const TArray<SBlockChangeCall>& NetBlockChangeCalls, const bool bRefreshChunks)
{
	TMap<FIntVector, FChunkWorldResolvedBlockHit> PreviousResolvedHitByBlock;
	TMap<FIntVector, FGameplayTag> PreviousBlockTypeByBlock;
	if (!HasAuthority())
	{
		for (const SBlockChangeCall& ChangeCall : NetBlockChangeCalls)
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
			if (PreviousResolvedHitByBlock.Contains(BlockWorldPos) || !IsRepresentedBlockAt(BlockWorldPos))
			{
				continue;
			}

			FChunkWorldResolvedBlockHit PreviousResolvedHit;
			FGameplayTag PreviousBlockTypeName;
			if (TryBuildPreviousResolvedHit(BlockWorldPos, PreviousResolvedHit, PreviousBlockTypeName))
			{
				PreviousResolvedHitByBlock.Add(BlockWorldPos, PreviousResolvedHit);
				PreviousBlockTypeByBlock.Add(BlockWorldPos, PreviousBlockTypeName);
			}
		}
	}

	Super::WriteBlockValuesAndUpdate(NetBlockChangeCalls, bRefreshChunks);

	if (HasAuthority())
	{
		return;
	}

	for (const TPair<FIntVector, FChunkWorldResolvedBlockHit>& PreviousResolvedEntry : PreviousResolvedHitByBlock)
	{
		if (!IsRepresentedBlockAt(PreviousResolvedEntry.Key))
		{
			QueueObservedReplicatedRepresentationRemoved(
				PreviousResolvedEntry.Key,
				&PreviousResolvedEntry.Value,
				PreviousBlockTypeByBlock.FindRef(PreviousResolvedEntry.Key));
		}
	}
}

void AChunkWorldExtended::WriteMeshDataAndUpdate(const TArray<SMeshChangeCall>& NetMeshChangeCalls, const bool bRefreshChunks)
{
	TMap<FIntVector, FChunkWorldResolvedBlockHit> PreviousResolvedHitByBlock;
	TMap<FIntVector, FGameplayTag> PreviousBlockTypeByBlock;
	if (!HasAuthority())
	{
		for (const SMeshChangeCall& ChangeCall : NetMeshChangeCalls)
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
			if (PreviousResolvedHitByBlock.Contains(BlockWorldPos) || !IsRepresentedBlockAt(BlockWorldPos))
			{
				continue;
			}

			FChunkWorldResolvedBlockHit PreviousResolvedHit;
			FGameplayTag PreviousBlockTypeName;
			if (TryBuildPreviousResolvedHit(BlockWorldPos, PreviousResolvedHit, PreviousBlockTypeName))
			{
				PreviousResolvedHitByBlock.Add(BlockWorldPos, PreviousResolvedHit);
				PreviousBlockTypeByBlock.Add(BlockWorldPos, PreviousBlockTypeName);
			}
		}
	}

	Super::WriteMeshDataAndUpdate(NetMeshChangeCalls, bRefreshChunks);

	if (BlockSwapComponent != nullptr)
	{
		// Porism chunk visuals rebuild asynchronously after the mesh write, so queue retry-based
		// swap presentation refresh instead of relying on a one-shot immediate re-park.
		BlockSwapComponent->QueueActiveSwapPresentationRefresh();
	}

	if (HasAuthority())
	{
		return;
	}

	for (const TPair<FIntVector, FChunkWorldResolvedBlockHit>& PreviousResolvedEntry : PreviousResolvedHitByBlock)
	{
		if (!IsRepresentedBlockAt(PreviousResolvedEntry.Key))
		{
			QueueObservedReplicatedRepresentationRemoved(
				PreviousResolvedEntry.Key,
				&PreviousResolvedEntry.Value,
				PreviousBlockTypeByBlock.FindRef(PreviousResolvedEntry.Key));
		}
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

	FChunkWorldResolvedBlockHit DestroyedFeedbackHit;
	const bool bHasDestroyedFeedbackHit = UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromBlockWorldPos(this, BlockWorldPos, DestroyedFeedbackHit);

	FGameplayTag BlockTypeName;
	FBlockDefinitionBase Definition;
	if (BlockTypeSchemaComponent != nullptr && BlockTypeSchemaComponent->GetBlockDefinitionForBlockWorldPos(BlockWorldPos, BlockTypeName, Definition))
	{
		TrySpawnDestructionActorForDestroyedBlock(BlockWorldPos, bHasDestroyedFeedbackHit ? &DestroyedFeedbackHit : nullptr, BlockTypeName);

		if (BlockSwapScannerComponent != nullptr)
		{
			(void)BlockSwapScannerComponent->ForceRemoveSwapForDestroyedBlock(BlockWorldPos);
		}

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

	if (BlockSwapScannerComponent != nullptr)
	{
		(void)BlockSwapScannerComponent->ForceRemoveSwapForDestroyedBlock(BlockWorldPos);
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

void AChunkWorldExtended::TrySpawnDestructionActorForDestroyedBlock(
	const FIntVector& BlockWorldPos,
	const FChunkWorldResolvedBlockHit* DestroyedFeedbackHit,
	const FGameplayTag& BlockTypeName)
{
	if (BlockTypeSchemaComponent == nullptr)
	{
		return;
	}

	FInstancedStruct DefinitionPayload;
	if (BlockTypeSchemaRegistry == nullptr || !BlockTypeSchemaRegistry->TryGetBlockDefinition(BlockTypeName, DefinitionPayload))
	{
		return;
	}

	FBlockDamageDefinition DamageDefinition;
	if (!UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageDefinition(DefinitionPayload, DamageDefinition)
		|| DamageDefinition.DestructionActorClass.IsNull())
	{
		return;
	}

	UClass* DestructionActorClass = DamageDefinition.DestructionActorClass.Get();
	if (DestructionActorClass == nullptr)
	{
		// Project-facing behavior: try one blocking load as a fallback so the first lethal destroy can still present.
		DestructionActorClass = DamageDefinition.DestructionActorClass.LoadSynchronous();
		if (DestructionActorClass == nullptr)
		{
			return;
		}
	}

	FTransform SpawnTransform = FTransform(BlockWorldPosToUEWorldPos(BlockWorldPos));
	bool bHasPresentationTransform = false;
	if (BlockSwapScannerComponent != nullptr)
	{
		bHasPresentationTransform = BlockSwapScannerComponent->TryGetActiveSwapPresentationTransformForBlock(BlockWorldPos, SpawnTransform);
	}
	if (!bHasPresentationTransform && BlockSwapComponent != nullptr)
	{
		bHasPresentationTransform = BlockSwapComponent->TryGetSwapTransformForBlock(BlockWorldPos, SpawnTransform);
	}

	if (!bHasPresentationTransform && DestroyedFeedbackHit != nullptr)
	{
		SpawnTransform.SetLocation(DestroyedFeedbackHit->RepresentativeWorldPos);
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	if (!DestructionActorClass->ImplementsInterface(UChunkWorldDestructionActorInterface::StaticClass()))
	{
		UE_LOG(
			LogChunkWorldExtended,
			Warning,
			TEXT("DestroyBlock skipped destruction presentation for block %s because destruction actor class '%s' does not implement UChunkWorldDestructionActorInterface."),
			*BlockWorldPos.ToString(),
			*GetNameSafe(DestructionActorClass));
		return;
	}

	AActor* DestructionActor = World->SpawnActorDeferred<AActor>(
		DestructionActorClass,
		SpawnTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (DestructionActor == nullptr)
	{
		return;
	}

	DestructionActor->SetReplicates(true);
	UGameplayStatics::FinishSpawningActor(DestructionActor, SpawnTransform);

	FChunkWorldBlockDestructionRequest Request;
	Request.ChunkWorld = this;
	Request.BlockTypeName = BlockTypeName;
	Request.BlockWorldPos = BlockWorldPos;
	Request.SpawnTransform = SpawnTransform;
	Request.RepresentativeWorldPos = DestroyedFeedbackHit != nullptr
		? DestroyedFeedbackHit->RepresentativeWorldPos
		: SpawnTransform.GetLocation();
	IChunkWorldDestructionActorInterface::Execute_TriggerBlockDestruction(DestructionActor, Request);
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
		FChunkWorldSettledBlockTransition Transition;
		if (BuildSettledBlockTransition(PendingChange.Key, PendingChange.Value, Transition))
		{
			OnSettledBlockTransition.Broadcast(this, Transition);
		}

		NotifyBlockCustomDataChanged(PendingChange.Key, PendingChange.Value.bTouchedHealth, TEXT("DeferredBlockUpdate"));
	}
}

bool AChunkWorldExtended::BuildSettledBlockTransition(
	const FIntVector& BlockWorldPos,
	const FDeferredBlockCustomDataChange& DeferredChange,
	FChunkWorldSettledBlockTransition& OutTransition) const
{
	OutTransition = FChunkWorldSettledBlockTransition();
	if (!DeferredChange.bTouchedHealth
		&& !DeferredChange.bObservedReplicatedHealthDecrease
		&& !DeferredChange.bObservedReplicatedRepresentationRemoved)
	{
		return false;
	}

	OutTransition.BlockWorldPos = BlockWorldPos;
	OutTransition.bTouchedHealth = DeferredChange.bTouchedHealth;
	OutTransition.bObservedHealthChange = DeferredChange.bObservedReplicatedHealthChange;
	OutTransition.bObservedHealthDecrease = DeferredChange.bObservedReplicatedHealthDecrease;
	OutTransition.bObservedRepresentationRemoved = DeferredChange.bObservedReplicatedRepresentationRemoved;
	OutTransition.bHasPreviousHealth = DeferredChange.bHasPreviousHealth;
	OutTransition.PreviousHealth = DeferredChange.PreviousHealth;
	OutTransition.bHasCurrentHealth = DeferredChange.bHasCurrentHealth;
	OutTransition.CurrentHealth = DeferredChange.CurrentHealth;
	OutTransition.PreviousResolvedHit = DeferredChange.PreviousResolvedHit;
	OutTransition.PreviousBlockTypeName = DeferredChange.PreviousBlockTypeName;
	OutTransition.bIsRepresentedAfterTransition = IsRepresentedBlockAt(BlockWorldPos);
	if (OutTransition.bIsRepresentedAfterTransition)
	{
		(void)UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromBlockWorldPos(
			const_cast<AChunkWorldExtended*>(this),
			BlockWorldPos,
			OutTransition.CurrentResolvedHit);
	}

	return true;
}

void AChunkWorldExtended::NotifyBlockCustomDataChanged(
	const FIntVector& BlockWorldPos,
	const bool bTouchedHealth,
	const TCHAR* /*SourceLabel*/)
{
	OnBlockCustomDataChanged.Broadcast(this, BlockWorldPos, bTouchedHealth);
}

bool AChunkWorldExtended::IsRepresentedBlockAt(const FIntVector& BlockWorldPos) const
{
	AChunkWorldExtended* MutableThis = const_cast<AChunkWorldExtended*>(this);
	const int32 MaterialIndex = MutableThis->GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::MaterialIndex, 0);
	const int32 MeshIndex = MutableThis->GetMeshDataByBlockWorldPos(BlockWorldPos).MeshId;
	return IsRepresentedVoxel(MaterialIndex, MeshIndex);
}

bool AChunkWorldExtended::TryGetObservedRuntimeHealth(const FIntVector& BlockWorldPos, int32& OutHealth) const
{
	int32 MaxHealth = 0;
	bool bInvincible = false;
	bool bHasRuntimeHealth = false;
	FGameplayTag BlockTypeName;
	return UChunkWorldBlockDamageBlueprintLibrary::TryGetRuntimeBlockHealthStateForBlockWorldPos(
		const_cast<AChunkWorldExtended*>(this),
		BlockWorldPos,
		OutHealth,
		MaxHealth,
		bInvincible,
		bHasRuntimeHealth,
		BlockTypeName);
}

bool AChunkWorldExtended::TryBuildPreviousResolvedHit(
	const FIntVector& BlockWorldPos,
	FChunkWorldResolvedBlockHit& OutResolvedHit,
	FGameplayTag& OutBlockTypeName) const
{
	OutResolvedHit = FChunkWorldResolvedBlockHit();
	OutBlockTypeName = FGameplayTag();
	if (!UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromBlockWorldPos(
		const_cast<AChunkWorldExtended*>(this),
		BlockWorldPos,
		OutResolvedHit))
	{
		return false;
	}

	FInstancedStruct DefinitionStruct;
	return UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForResolvedBlockHit(
		OutResolvedHit,
		OutBlockTypeName,
		DefinitionStruct);
}

void AChunkWorldExtended::QueueObservedReplicatedHealthTransition(
	const FIntVector& BlockWorldPos,
	const int32 PreviousHealth,
	const int32 CurrentHealth)
{
	FDeferredBlockCustomDataChange& DeferredChange = DeferredBlockCustomDataChanges.FindOrAdd(BlockWorldPos);
	DeferredChange.bObservedReplicatedHealthChange = true;
	DeferredChange.bObservedReplicatedHealthDecrease |= CurrentHealth < PreviousHealth;
	DeferredChange.bHasPreviousHealth = true;
	DeferredChange.PreviousHealth = PreviousHealth;
	DeferredChange.bHasCurrentHealth = true;
	DeferredChange.CurrentHealth = CurrentHealth;
	QueueBlockCustomDataChanged(BlockWorldPos, true, TEXT("ReplicatedHealthTransition"));
}

void AChunkWorldExtended::QueueObservedReplicatedRepresentationRemoved(
	const FIntVector& BlockWorldPos,
	const FChunkWorldResolvedBlockHit* PreviousResolvedHit,
	const FGameplayTag& PreviousBlockTypeName)
{
	FDeferredBlockCustomDataChange& DeferredChange = DeferredBlockCustomDataChanges.FindOrAdd(BlockWorldPos);
	DeferredChange.bObservedReplicatedRepresentationRemoved = true;
	if (PreviousResolvedHit != nullptr && PreviousResolvedHit->bHasBlock && !DeferredChange.PreviousResolvedHit.bHasBlock)
	{
		DeferredChange.PreviousResolvedHit = *PreviousResolvedHit;
		DeferredChange.PreviousBlockTypeName = PreviousBlockTypeName;
	}

	QueueBlockCustomDataChanged(BlockWorldPos, false, TEXT("ReplicatedRepresentationRemoved"));
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
