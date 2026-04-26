// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldExtended.h"

#include "Block/BlockTypeSchemaRegistry.h"
#include "Block/BlockTypeSchemaBlueprintLibrary.h"
#include "ChunkWorld/Actors/ChunkWorldChaosDestructionPresentationActor.h"
#include "ChunkWorld/Actors/ChunkWorldBlockSwapReplicationProxy.h"
#include "ChunkWorld/Actors/ChunkWorldDestructionActorInterface.h"
#include "ChunkWorld/Actors/ChunkWorldTimedCleanupDestructionActor.h"
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
#include "TimerManager.h"

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

	bool TryExecuteReusableDestructionActorTrigger(AActor* DestructionActor, const FChunkWorldBlockDestructionRequest& Request)
	{
		if (AChunkWorldChaosDestructionPresentationActor* ChaosDestructionActor = Cast<AChunkWorldChaosDestructionPresentationActor>(DestructionActor))
		{
			ChaosDestructionActor->ExecuteFrameworkDestructionTrigger(Request);
			return true;
		}

		if (AChunkWorldTimedCleanupDestructionActor* TimedCleanupActor = Cast<AChunkWorldTimedCleanupDestructionActor>(DestructionActor))
		{
			TimedCleanupActor->ExecuteFrameworkDestructionTrigger(Request);
			return true;
		}

		return false;
	}

}

AChunkWorldExtended::AChunkWorldExtended()
{
	BlockTypeSchemaComponent = CreateDefaultSubobject<UBlockTypeSchemaComponent>(TEXT("BlockTypeSchemaComponent"));
	BlockFeedbackComponent = CreateDefaultSubobject<UChunkWorldBlockFeedbackComponent>(TEXT("BlockFeedbackComponent"));
	BlockSwapScannerComponent = CreateDefaultSubobject<UChunkWorldBlockSwapScannerComponent>(TEXT("BlockSwapScannerComponent"));
	BlockSwapComponent = CreateDefaultSubobject<UChunkWorldBlockSwapComponent>(TEXT("BlockSwapComponent"));
	BlockFeedbackComponent->SetIsReplicated(true);
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
	ResetWorldReadyStateTracking();
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

void AChunkWorldExtended::Tick(float DeltaTime)
{
	if (!bStartupWorldReadyTrackingActive)
	{
		Super::Tick(DeltaTime);
		return;
	}

	TArray<SPendingWalkerInfo> PendingInfos;
	{
		std::lock_guard<std::mutex> PendingWalkerInfoLock(PendingWalkerInfos.Mutex);
		PendingInfos.Reserve(static_cast<int32>(PendingWalkerInfos.WriteBuffer.size()));
		for (const SPendingWalkerInfo& PendingInfo : PendingWalkerInfos.WriteBuffer)
		{
			PendingInfos.Add(PendingInfo);
		}
	}

	for (const SPendingWalkerInfo& PendingInfo : PendingInfos)
	{
		if (IsValid(PendingInfo.Walker))
		{
			HandlePendingWalkerInfo(PendingInfo.Walker, PendingInfo.Info);
		}
	}

	Super::Tick(DeltaTime);
	PruneWalkerReadyStates();
	RefreshWorldReadyState();
}

void AChunkWorldExtended::BeginPlay()
{
	Super::BeginPlay();
	EnsureBlockSwapReplicationProxy();
}

void AChunkWorldExtended::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority() && BlockSwapReplicationProxy != nullptr)
	{
		BlockSwapReplicationProxy->Destroy();
		BlockSwapReplicationProxy = nullptr;
	}

	ResetWorldReadyStateTracking();
	Super::EndPlay(EndPlayReason);
}

void AChunkWorldExtended::AddChunkWorldWalker(UObject* NewWorldLoader)
{
	Super::AddChunkWorldWalker(NewWorldLoader);

	if (!bStartupWorldReadyTrackingActive || !NewWorldLoader)
	{
		return;
	}

	FChunkWorldWalkerReadyState& ReadyState = WalkerReadyStates.FindOrAdd(FObjectKey(NewWorldLoader));
	ReadyState.Walker = NewWorldLoader;
	ReadyState.bHasReceivedReadyInfo = false;
	ReadyState.bIsReady = false;
	ReadyState.ReadyDetailLevel = INDEX_NONE;
	ReadyState.LastWalkerInfo = FChunkWorldWalkerInfo();
	RefreshWorldReadyState();
}

void AChunkWorldExtended::RemoveChunkWorldWalker(UObject* WorldLoaderToRemove)
{
	Super::RemoveChunkWorldWalker(WorldLoaderToRemove);

	if (!bStartupWorldReadyTrackingActive)
	{
		return;
	}

	if (WorldLoaderToRemove)
	{
		WalkerReadyStates.Remove(FObjectKey(WorldLoaderToRemove));
	}

	RefreshWorldReadyState();
}

void AChunkWorldExtended::SetChunkWorldWalkers(TArray<UObject*> NewWorldLoaders)
{
	Super::SetChunkWorldWalkers(NewWorldLoaders);

	if (!bStartupWorldReadyTrackingActive)
	{
		return;
	}

	TMap<FObjectKey, FChunkWorldWalkerReadyState> NewWalkerReadyStates;
	for (UObject* NewWorldLoader : NewWorldLoaders)
	{
		if (!NewWorldLoader)
		{
			continue;
		}

		const FObjectKey WalkerKey(NewWorldLoader);
		if (const FChunkWorldWalkerReadyState* ExistingState = WalkerReadyStates.Find(WalkerKey))
		{
			NewWalkerReadyStates.Add(WalkerKey, *ExistingState);
			NewWalkerReadyStates[WalkerKey].Walker = NewWorldLoader;
			continue;
		}

		FChunkWorldWalkerReadyState& NewState = NewWalkerReadyStates.Add(WalkerKey);
		NewState.Walker = NewWorldLoader;
	}

	WalkerReadyStates = MoveTemp(NewWalkerReadyStates);
	RefreshWorldReadyState();
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

AChunkWorldBlockSwapReplicationProxy* AChunkWorldExtended::GetBlockSwapReplicationProxy() const
{
	return BlockSwapReplicationProxy;
}

bool AChunkWorldExtended::HasRegisteredChunkWorldWalker(const UObject* WorldLoader) const
{
	if (WorldLoader == nullptr)
	{
		return false;
	}

	std::lock_guard<std::mutex> WorldLoaderLock(const_cast<std::mutex&>(WorldLoadersKey));
	return WorldLoaders.Contains(const_cast<UObject*>(WorldLoader));
}

bool AChunkWorldExtended::WasChunkWorldWalkerIncludedInStartupReady(const UObject* WorldLoader) const
{
	return WorldLoader != nullptr && StartupReadyWalkerKeys.Contains(FObjectKey(WorldLoader));
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

void AChunkWorldExtended::HandlePendingWalkerInfo(UObject* Walker, const FChunkWorldWalkerInfo& Info)
{
	if (!bStartupWorldReadyTrackingActive || !Walker)
	{
		return;
	}

	FChunkWorldWalkerReadyState& ReadyState = WalkerReadyStates.FindOrAdd(FObjectKey(Walker));
	ReadyState.Walker = Walker;
	ReadyState.bHasReceivedReadyInfo = true;
	ReadyState.bIsReady = IsWalkerReadyForWorld(Info);
	ReadyState.ReadyDetailLevel = Info.DetailLevel;
	ReadyState.LastWalkerInfo = Info;

	RefreshWorldReadyState();
}

bool AChunkWorldExtended::IsWalkerReadyForWorld(const FChunkWorldWalkerInfo& Info) const
{
	if (!GeneratorIsRunning || WorldChunks.empty() || Info.Chunk.Chunk == nullptr || !Info.Chunk.Chunk->IsReady)
	{
		return false;
	}

	const int32 FinestDetailLevel = static_cast<int32>(WorldChunks.size()) - 1;
	return Info.DetailLevel >= FinestDetailLevel;
}

void AChunkWorldExtended::RefreshWorldReadyState()
{
	if (!bStartupWorldReadyTrackingActive)
	{
		return;
	}

	PruneWalkerReadyStates();

	bool bAllWalkersReady = WorldLoaders.Num() > 0;
	for (UObject* WorldLoader : WorldLoaders)
	{
		if (!WorldLoader || !WorldLoader->GetClass()->ImplementsInterface(UChunkWorldWalker::StaticClass()))
		{
			continue;
		}

		const FChunkWorldWalkerReadyState* ReadyState = WalkerReadyStates.Find(FObjectKey(WorldLoader));
		if (ReadyState == nullptr || !ReadyState->bHasReceivedReadyInfo || !ReadyState->bIsReady)
		{
			bAllWalkersReady = false;
			break;
		}
	}

	const bool bWasWorldReady = bWorldReady;
	bWorldReady = bAllWalkersReady;
	if (!bWasWorldReady && bWorldReady)
	{
		StartupReadyWalkerKeys.Reset();
		for (UObject* WorldLoader : WorldLoaders)
		{
			if (IsValid(WorldLoader) && WorldLoader->GetClass()->ImplementsInterface(UChunkWorldWalker::StaticClass()))
			{
				StartupReadyWalkerKeys.Add(FObjectKey(WorldLoader));
			}
		}

		UE_LOG(
			LogChunkWorldExtended,
			Log,
			TEXT("Chunk world '%s' reached startup ready state for %d walkers."),
			*GetNameSafe(this),
			WorldLoaders.Num());
		OnWorldReady.Broadcast(this);
		bStartupWorldReadyTrackingActive = false;
		WalkerReadyStates.Reset();
	}
}

void AChunkWorldExtended::PruneWalkerReadyStates()
{
	TSet<FObjectKey> ActiveWalkerKeys;
	for (UObject* WorldLoader : WorldLoaders)
	{
		if (IsValid(WorldLoader))
		{
			ActiveWalkerKeys.Add(FObjectKey(WorldLoader));
		}
	}

	for (auto It = WalkerReadyStates.CreateIterator(); It; ++It)
	{
		const FChunkWorldWalkerReadyState& ReadyState = It.Value();
		if (!ReadyState.Walker.IsValid() || !ActiveWalkerKeys.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
}

void AChunkWorldExtended::ResetWorldReadyStateTracking()
{
	WalkerReadyStates.Reset();
	StartupReadyWalkerKeys.Reset();
	bWorldReady = false;
	bStartupWorldReadyTrackingActive = true;
}

void AChunkWorldExtended::EnsureBlockSwapReplicationProxy()
{
	if (!HasAuthority() || GetNetMode() == NM_Standalone || BlockSwapReplicationProxy != nullptr)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	BlockSwapReplicationProxy = World->SpawnActor<AChunkWorldBlockSwapReplicationProxy>(
		AChunkWorldBlockSwapReplicationProxy::StaticClass(),
		GetActorTransform(),
		SpawnParameters);
	if (BlockSwapReplicationProxy != nullptr)
	{
		BlockSwapReplicationProxy->InitializeForChunkWorld(this);
	}
}

// Keep the settled block-transition hook attached to Porism's
// renamed replicated custom-data apply path so client prediction/UI still observe the final batch.
void AChunkWorldExtended::WriteCustomDataAndUpdate(const TArray<SCustomDataChangeCall>& NetCustomDataChangeCalls)
{
	const int32 HealthCustomDataIndex = BlockTypeSchemaComponent != nullptr
		? BlockTypeSchemaComponent->GetBlockHealthCustomDataIndex()
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

	Super::WriteCustomDataAndUpdate(NetCustomDataChangeCalls);

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
					// Project-specific change: preserve both damage and healing deltas from the settled client-only
					// replication callback so UI/prediction listeners can react to health restoration as well.
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
		if (BlockSwapScannerComponent != nullptr)
		{
			(void)BlockSwapScannerComponent->ForceRemoveSwapForDestroyedBlock(BlockWorldPos);
		}

		if (!Definition.MeshAsset.IsNull())
		{
			const int32 ExistingMaterialIndex = GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::MaterialIndex, 0);
			const int32 ExistingMeshIndex = GetMeshDataByBlockWorldPos(BlockWorldPos).MeshId;
			FMeshData EmptyMeshData;
			EmptyMeshData.MeshId = EmptyMesh;
			if (bHasDestroyedFeedbackHit && BlockFeedbackComponent != nullptr)
			{
				(void)BlockFeedbackComponent->BroadcastAuthoritativeDestroyFeedback(DestroyedFeedbackHit);
			}
			SetMeshDataByBlockWorldPos(BlockWorldPos, EmptyMeshData, bRefreshChunks);
			TrySpawnDestructionActorForDestroyedBlock(BlockWorldPos, bHasDestroyedFeedbackHit ? &DestroyedFeedbackHit : nullptr, BlockTypeName);
		    return true;
		}

		if (!Definition.MaterialAsset.IsNull())
		{
			if (bHasDestroyedFeedbackHit && BlockFeedbackComponent != nullptr)
			{
				(void)BlockFeedbackComponent->BroadcastAuthoritativeDestroyFeedback(DestroyedFeedbackHit);
			}
			SetBlockValueByBlockWorldPos(BlockWorldPos, EmptyMaterial, bRefreshChunks);
			TrySpawnDestructionActorForDestroyedBlock(BlockWorldPos, bHasDestroyedFeedbackHit ? &DestroyedFeedbackHit : nullptr, BlockTypeName);
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

	FBlockHealthDefinition HealthDefinition;
	if (!UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthDefinition(DefinitionPayload, HealthDefinition)
		|| HealthDefinition.DestructionActorClass.IsNull())
	{
		return;
	}

	if (!TryReserveDestructionPresentation(BlockWorldPos))
	{
		return;
	}

	UClass* DestructionActorClass = HealthDefinition.DestructionActorClass.Get();
	if (DestructionActorClass == nullptr)
	{
		// Project-facing behavior: try one blocking load as a fallback so the first lethal destroy can still present.
		DestructionActorClass = HealthDefinition.DestructionActorClass.LoadSynchronous();
		if (DestructionActorClass == nullptr)
		{
			ReleaseDestructionPresentationReservation(BlockWorldPos);
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
		ReleaseDestructionPresentationReservation(BlockWorldPos);
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
		ReleaseDestructionPresentationReservation(BlockWorldPos);
		return;
	}

	FChunkWorldBlockDestructionRequest Request;
	Request.ChunkWorld = this;
	Request.BlockTypeName = BlockTypeName;
	Request.BlockWorldPos = BlockWorldPos;
	Request.SpawnTransform = SpawnTransform;
	Request.RepresentativeWorldPos = DestroyedFeedbackHit != nullptr
		? DestroyedFeedbackHit->RepresentativeWorldPos
		: SpawnTransform.GetLocation();

	const EBlockDestructionPresentationNetMode PresentationNetMode = HealthDefinition.DestructionPresentationNetMode;
	if (HasAuthority())
	{
		if (UWorld* TimerWorld = GetWorld())
		{
			FTimerDelegate DeferredSpawnDelegate = FTimerDelegate::CreateWeakLambda(
				this,
				[this, DestructionActorClass, Request, PresentationNetMode, BlockWorldPos]()
				{
					if (!SpawnResolvedDestructionActor(DestructionActorClass, Request, PresentationNetMode))
					{
						ReleaseDestructionPresentationReservation(BlockWorldPos);
						return;
					}

					ReleaseDestructionPresentationReservation(BlockWorldPos);
				});
			TimerWorld->GetTimerManager().SetTimerForNextTick(DeferredSpawnDelegate);
			return;
		}

		if (!SpawnResolvedDestructionActor(DestructionActorClass, Request, PresentationNetMode))
		{
			ReleaseDestructionPresentationReservation(BlockWorldPos);
			return;
		}

		ReleaseDestructionPresentationReservation(BlockWorldPos);
		return;
	}

	if (PresentationNetMode == EBlockDestructionPresentationNetMode::ReplicatedActor)
	{
		ReleaseDestructionPresentationReservation(BlockWorldPos);
		return;
	}

	if (!SpawnResolvedDestructionActor(DestructionActorClass, Request, PresentationNetMode))
	{
		ReleaseDestructionPresentationReservation(BlockWorldPos);
		return;
	}

	ReleaseDestructionPresentationReservation(BlockWorldPos);
}

bool AChunkWorldExtended::SpawnResolvedDestructionActor(
	UClass* DestructionActorClass,
	const FChunkWorldBlockDestructionRequest& Request,
	const EBlockDestructionPresentationNetMode PresentationNetMode)
{
	if (DestructionActorClass == nullptr)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	AActor* DestructionActor = World->SpawnActorDeferred<AActor>(
		DestructionActorClass,
		Request.SpawnTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (DestructionActor == nullptr)
	{
		return false;
	}

	const bool bShouldReplicateActor = PresentationNetMode == EBlockDestructionPresentationNetMode::ReplicatedActor;
	// Project-specific change: destruction presentation delivery is authored per block definition, so force the spawn
	// instance to match that policy instead of inheriting whichever replication default the actor class currently has.
	DestructionActor->SetReplicates(bShouldReplicateActor);
	if (!bShouldReplicateActor)
	{
		DestructionActor->SetReplicateMovement(false);
	}

	UGameplayStatics::FinishSpawningActor(DestructionActor, Request.SpawnTransform);
	if (!TryExecuteReusableDestructionActorTrigger(DestructionActor, Request))
	{
		IChunkWorldDestructionActorInterface::Execute_TriggerBlockDestruction(DestructionActor, Request);
	}
	return true;
}

bool AChunkWorldExtended::TryReserveDestructionPresentation(const FIntVector& BlockWorldPos)
{
	if (ReservedDestructionPresentationBlocks.Contains(BlockWorldPos))
	{
		UE_LOG(
			LogChunkWorldExtended,
			Warning,
			TEXT("DestroyBlock suppressed duplicate destruction presentation for block %s on chunk world '%s'."),
			*BlockWorldPos.ToString(),
			*GetNameSafe(this));
		return false;
	}

	ReservedDestructionPresentationBlocks.Add(BlockWorldPos);
	if (UWorld* World = GetWorld())
	{
		FTimerDelegate releaseReservation;
		releaseReservation.BindLambda([this, BlockWorldPos]()
		{
			ReleaseDestructionPresentationReservation(BlockWorldPos);
		});

		FTimerHandle releaseHandle;
		World->GetTimerManager().SetTimer(releaseHandle, releaseReservation, 0.25f, false);
	}

	return true;
}

void AChunkWorldExtended::ReleaseDestructionPresentationReservation(const FIntVector& BlockWorldPos)
{
	ReservedDestructionPresentationBlocks.Remove(BlockWorldPos);
}

void AChunkWorldExtended::HandleCommittedBlockCustomData(const FIntVector& BlockWorldPos, const TArray<int32>& PackedValues)
{
	const int32 HealthCustomDataIndex = BlockTypeSchemaComponent != nullptr
		? BlockTypeSchemaComponent->GetBlockHealthCustomDataIndex()
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
			if (!HasAuthority()
				&& Transition.bObservedRepresentationRemoved
				&& Transition.PreviousResolvedHit.bHasBlock
				&& Transition.PreviousBlockTypeName.IsValid())
			{
				TrySpawnDestructionActorForDestroyedBlock(
					Transition.BlockWorldPos,
					&Transition.PreviousResolvedHit,
					Transition.PreviousBlockTypeName);
			}

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
	OutTransition.bObservedHealthIncrease = DeferredChange.bObservedReplicatedHealthIncrease;
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
	DeferredChange.bObservedReplicatedHealthIncrease |= CurrentHealth > PreviousHealth;
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

	const int32 HealthCustomDataIndex = BlockTypeSchemaComponent->GetBlockHealthCustomDataIndex();
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

	const FBlockHealthCustomData* HealthCustomData = BlockCustomData.GetPtr<FBlockHealthCustomData>();
	if (HealthCustomData == nullptr || HealthCustomData->Health > 0)
	{
		return;
	}

	(void)DestroyBlock(BlockWorldPos, true);
}
