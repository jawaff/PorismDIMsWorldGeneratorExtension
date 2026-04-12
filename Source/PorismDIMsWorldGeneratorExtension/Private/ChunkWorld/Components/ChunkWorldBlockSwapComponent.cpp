// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Components/ChunkWorldBlockSwapComponent.h"

#include "Block/BlockTypeSchemaRegistry.h"
#include "ChunkWorld/Actors/ChunkWorldBlockSwapReplicationProxy.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "ChunkWorldStructs/ChunkWorldEnums.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogChunkWorldBlockSwapComponent, Log, All);

namespace
{
	const TCHAR* LexNetModeSafe(const UWorld* World)
	{
		if (World == nullptr)
		{
			return TEXT("NoWorld");
		}

		switch (World->GetNetMode())
		{
		case NM_Standalone:
			return TEXT("Standalone");
		case NM_DedicatedServer:
			return TEXT("DedicatedServer");
		case NM_ListenServer:
			return TEXT("ListenServer");
		case NM_Client:
			return TEXT("Client");
		default:
			return TEXT("Unknown");
		}
	}
}

UChunkWorldBlockSwapComponent::UChunkWorldBlockSwapComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UChunkWorldBlockSwapComponent::BeginPlay()
{
	Super::BeginPlay();

	bRepresentedInstanceIndexDirty = true;
}

void UChunkWorldBlockSwapComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingSwapActorReconcileHandle);
		World->GetTimerManager().ClearTimer(PendingActiveSwapPresentationRefreshHandle);
	}

	PendingSwapActorReconcileBlocks.Reset();
	ActiveSwaps.Reset();
	LocalPresentationStates.Reset();
	RepresentedInstanceIndex.Reset();
	bRepresentedInstanceIndexDirty = true;
	Super::EndPlay(EndPlayReason);
}

bool UChunkWorldBlockSwapComponent::TryApplySwapRequest(
	const FIntVector& BlockWorldPos,
	const FGameplayTag& BlockTypeName,
	bool bEntering,
	const FTransform& PresentationTransform)
{
	AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner();
	if (ChunkWorld == nullptr || !ChunkWorld->HasAuthority())
	{
		return false;
	}

	if (!CanApplySwapRequest(BlockWorldPos, bEntering))
	{
		return false;
	}

	const bool bApplied = bEntering
		? AddActiveSwap(BlockWorldPos, BlockTypeName, PresentationTransform)
		: RemoveActiveSwap(BlockWorldPos);
	return bApplied;
}

FVector UChunkWorldBlockSwapComponent::GetParkingOrigin() const
{
	const AActor* OwnerActor = GetOwner();
	return OwnerActor != nullptr
		? OwnerActor->GetActorLocation() + GetParkingWorldOffsetValue()
		: GetParkingWorldOffsetValue();
}

void UChunkWorldBlockSwapComponent::SetParkingWorldOffset(const FVector& InParkingWorldOffset)
{
	// Project code configures this at startup so swap parking can stay generic in the plugin.
	AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner();
	if (ChunkWorld == nullptr || ParkingWorldOffset.Equals(InParkingWorldOffset))
	{
		return;
	}

	ParkingWorldOffset = InParkingWorldOffset;
	QueueActiveSwapPresentationRefresh();
}

bool UChunkWorldBlockSwapComponent::TrySetSwapActor(const FIntVector& BlockWorldPos, AActor* SwapActor)
{
	AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner();
	if (ChunkWorld == nullptr || !ChunkWorld->HasAuthority())
	{
		return false;
	}

	FReplicatedChunkWorldSwapItem* ExistingItem = FindActiveSwapByBlockWorldPos(BlockWorldPos);
	if (ExistingItem == nullptr)
	{
		return false;
	}

	ExistingItem->SwapActor = SwapActor;
	if (AChunkWorldBlockSwapReplicationProxy* SwapProxy = ChunkWorld->GetBlockSwapReplicationProxy())
	{
		SwapProxy->BroadcastSwapChanged(*ExistingItem);
	}
	ReconcileSwapActorPresentation(*ExistingItem);
	return true;
}

bool UChunkWorldBlockSwapComponent::TryGetSwapTransformForBlock(const FIntVector& BlockWorldPos, FTransform& OutSwapTransform) const
{
	FResolvedSwapInstance ResolvedInstance;
	if (ResolveRepresentedInstance(BlockWorldPos, ResolvedInstance))
	{
		OutSwapTransform = ResolvedInstance.InstanceTransform;
		return true;
	}

	if (const AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner())
	{
		OutSwapTransform = FTransform(ChunkWorld->BlockWorldPosToUEWorldPos(BlockWorldPos));
		return true;
	}

	return false;
}

void UChunkWorldBlockSwapComponent::RefreshActiveSwapPresentationStates()
{
	bRepresentedInstanceIndexDirty = true;
	for (const FReplicatedChunkWorldSwapItem& SwapItem : ActiveSwaps)
	{
		FResolvedSwapInstance ResolvedInstance;
		if (!ResolveRepresentedInstance(SwapItem.BlockWorldPos, ResolvedInstance))
		{
			continue;
		}

		// Chunk mesh updates can recreate the represented instance at the live block location while the swap is
		// still active. Drop the stale cache and park the newly rebuilt instance so actor and voxel presentation
		// do not coexist.
		LocalPresentationStates.Remove(SwapItem.BlockWorldPos);
		(void)ParkRepresentedInstance(SwapItem);
		ReconcileSwapActorPresentation(SwapItem);
	}
}

void UChunkWorldBlockSwapComponent::QueueActiveSwapPresentationRefresh()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	RemainingActiveSwapPresentationRefreshAttempts = FMath::Max(RemainingActiveSwapPresentationRefreshAttempts, 120);
	World->GetTimerManager().SetTimer(
		PendingActiveSwapPresentationRefreshHandle,
		this,
		&UChunkWorldBlockSwapComponent::ProcessQueuedActiveSwapPresentationRefresh,
		0.05f,
		true,
		0.0f);
}

void UChunkWorldBlockSwapComponent::PurgeStaleRepresentedMeshInstances(const FIntVector& BlockWorldPos)
{
	AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner();
	if (ChunkWorld == nullptr)
	{
		return;
	}

	const int32 MeshId = ChunkWorld->GetMeshDataByBlockWorldPos(BlockWorldPos).MeshId;
	if (MeshId != EmptyMesh && MeshId != DefaultMesh)
	{
		return;
	}

	if (bRepresentedInstanceIndexDirty)
	{
		RebuildRepresentedInstanceIndex();
	}

	const TArray<FResolvedSwapInstance>* IndexedInstances = RepresentedInstanceIndex.Find(BlockWorldPos);
	if (IndexedInstances == nullptr || IndexedInstances->IsEmpty())
	{
		return;
	}

	TMap<TWeakObjectPtr<UInstancedStaticMeshComponent>, TArray<int32>> RemoveIndicesByComponent;
	for (const FResolvedSwapInstance& IndexedInstance : *IndexedInstances)
	{
		if (UInstancedStaticMeshComponent* MeshComponent = IndexedInstance.Component.Get())
		{
			RemoveIndicesByComponent.FindOrAdd(MeshComponent).Add(IndexedInstance.InstanceIndex);
		}
	}

	int32 RemovedInstanceCount = 0;
	for (TPair<TWeakObjectPtr<UInstancedStaticMeshComponent>, TArray<int32>>& RemovalEntry : RemoveIndicesByComponent)
	{
		UInstancedStaticMeshComponent* MeshComponent = RemovalEntry.Key.Get();
		if (MeshComponent == nullptr)
		{
			continue;
		}

		TArray<int32>& RemoveIndices = RemovalEntry.Value;
		RemoveIndices.Sort([](const int32 A, const int32 B) { return A > B; });

		TArray<int32> UniqueIndices;
		int32 LastIndex = INDEX_NONE;
		for (const int32 RemoveIndex : RemoveIndices)
		{
			if (RemoveIndex == LastIndex)
			{
				continue;
			}

			if (RemoveIndex >= 0 && RemoveIndex < MeshComponent->GetInstanceCount())
			{
				UniqueIndices.Add(RemoveIndex);
				LastIndex = RemoveIndex;
			}
		}

		if (UniqueIndices.IsEmpty())
		{
			continue;
		}

		UE_LOG(
			LogChunkWorldBlockSwapComponent,
			Warning,
			TEXT("Stale represented mesh purge removing Block=%s Component=%s Class=%s Owner=%s Mesh=%s RemoveCount=%d InstanceCountBefore=%d"),
			*BlockWorldPos.ToString(),
			*GetNameSafe(MeshComponent),
			*GetNameSafe(MeshComponent->GetClass()),
			*GetNameSafe(MeshComponent->GetOwner()),
			*GetNameSafe(MeshComponent->GetStaticMesh()),
			UniqueIndices.Num(),
			MeshComponent->GetInstanceCount());
		MeshComponent->RemoveInstances(UniqueIndices, true);
		RemovedInstanceCount += UniqueIndices.Num();
	}

	if (RemovedInstanceCount > 0)
	{
		bRepresentedInstanceIndexDirty = true;
	}
}

bool UChunkWorldBlockSwapComponent::CanApplySwapRequest(const FIntVector& BlockWorldPos, bool bEntering) const
{
	const bool bAlreadyActive = FindActiveSwapByBlockWorldPos(BlockWorldPos) != nullptr;
	if (bEntering)
	{
		if (bAlreadyActive || LocalPresentationStates.Contains(BlockWorldPos))
		{
			return false;
		}
	}
	else if (!bAlreadyActive)
	{
		return false;
	}

	UBlockTypeSchemaComponent* SchemaComponent = GetSchemaComponent();
	if (SchemaComponent == nullptr)
	{
		return !bEntering;
	}

	FGameplayTag ResolvedBlockTypeName;
	FBlockDefinitionBase Definition;
	if (SchemaComponent->GetBlockDefinitionForBlockWorldPos(BlockWorldPos, ResolvedBlockTypeName, Definition))
	{
		return !bEntering || !Definition.SwapActorClass.IsNull();
	}

	return !bEntering;
}

bool UChunkWorldBlockSwapComponent::AddActiveSwap(
	const FIntVector& BlockWorldPos,
	const FGameplayTag& BlockTypeName,
	const FTransform& PresentationTransform)
{
	FReplicatedChunkWorldSwapItem NewItem;
	NewItem.BlockWorldPos = BlockWorldPos;
	NewItem.BlockTypeName = BlockTypeName;
	if (AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner())
	{
		NewItem.MeshId = ChunkWorld->GetMeshDataByBlockWorldPos(BlockWorldPos).MeshId;
	}
	NewItem.PresentationTransform = PresentationTransform;

	if (!ApplySwapPresentationState(NewItem, true))
	{
		return false;
	}

	FReplicatedChunkWorldSwapItem& AddedItem = ActiveSwaps.Add_GetRef(NewItem);
	if (AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner())
	{
		if (AChunkWorldBlockSwapReplicationProxy* SwapProxy = ChunkWorld->GetBlockSwapReplicationProxy())
		{
			SwapProxy->BroadcastSwapEntered(AddedItem);
		}
	}
	return true;
}

bool UChunkWorldBlockSwapComponent::RemoveActiveSwap(const FIntVector& BlockWorldPos)
{
	const int32 ExistingIndex = ActiveSwaps.IndexOfByPredicate(
		[&BlockWorldPos](const FReplicatedChunkWorldSwapItem& Item)
		{
			return Item.BlockWorldPos == BlockWorldPos;
		});
	if (!ActiveSwaps.IsValidIndex(ExistingIndex))
	{
		return false;
	}

	const FReplicatedChunkWorldSwapItem ExistingItem = ActiveSwaps[ExistingIndex];
	(void)ApplySwapPresentationState(ExistingItem, false);
	ActiveSwaps.RemoveAt(ExistingIndex);
	if (AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner())
	{
		if (AChunkWorldBlockSwapReplicationProxy* SwapProxy = ChunkWorld->GetBlockSwapReplicationProxy())
		{
			SwapProxy->BroadcastSwapExited(ExistingItem);
		}
	}
	return true;
}

bool UChunkWorldBlockSwapComponent::ApplySwapPresentationState(const FReplicatedChunkWorldSwapItem& SwapItem, bool bEntering)
{
	return bEntering
		? ParkRepresentedInstance(SwapItem)
		: RestoreRepresentedInstance(SwapItem);
}

bool UChunkWorldBlockSwapComponent::ParkRepresentedInstance(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	if (LocalPresentationStates.Contains(SwapItem.BlockWorldPos))
	{
		return true;
	}

	TArray<FResolvedSwapInstance> ResolvedInstances;
	ResolveRepresentedInstances(SwapItem, ResolvedInstances);
	if (ResolvedInstances.IsEmpty())
	{
		UE_LOG(
			LogChunkWorldBlockSwapComponent,
			Warning,
			TEXT("BlockSwapComponent FailedToResolveInstanceForParking Owner=%s NetMode=%s Block=%s MeshId=%d PresentationLocation=%s IndexedBlocks=%d LocalStates=%d"),
			*GetNameSafe(GetOwner()),
			LexNetModeSafe(GetWorld()),
			*SwapItem.BlockWorldPos.ToString(),
			SwapItem.MeshId,
			*SwapItem.PresentationTransform.GetLocation().ToString(),
			RepresentedInstanceIndex.Num(),
			LocalPresentationStates.Num());
		return false;
	}

	TArray<FLocalSwapPresentationState> PresentationStates;
	PresentationStates.Reserve(ResolvedInstances.Num());

	for (const FResolvedSwapInstance& ResolvedInstance : ResolvedInstances)
	{
		UInstancedStaticMeshComponent* MeshComponent = ResolvedInstance.Component.Get();
		if (MeshComponent == nullptr)
		{
			continue;
		}

		FLocalSwapPresentationState& PresentationState = PresentationStates.AddDefaulted_GetRef();
		PresentationState.Component = MeshComponent;
		PresentationState.InstanceIndex = ResolvedInstance.InstanceIndex;
		PresentationState.MeshId = ResolvedInstance.MeshId;
		PresentationState.OriginalTransform = ResolvedInstance.InstanceTransform;
		PresentationState.ParkedTransform = BuildParkingTransform(SwapItem.BlockWorldPos, ResolvedInstance.InstanceTransform);

		if (!MeshComponent->UpdateInstanceTransform(PresentationState.InstanceIndex, PresentationState.ParkedTransform, true, true, true))
		{
			UE_LOG(
				LogChunkWorldBlockSwapComponent,
				Warning,
				TEXT("BlockSwapComponent FailedToParkInstance Owner=%s Block=%s Index=%d"),
				*GetNameSafe(GetOwner()),
				*SwapItem.BlockWorldPos.ToString(),
				PresentationState.InstanceIndex);
			continue;
		}
	}

	if (PresentationStates.IsEmpty())
	{
		UE_LOG(
			LogChunkWorldBlockSwapComponent,
			Warning,
			TEXT("BlockSwapComponent ParkingProducedNoStates Owner=%s NetMode=%s Block=%s ResolvedInstances=%d"),
			*GetNameSafe(GetOwner()),
			LexNetModeSafe(GetWorld()),
			*SwapItem.BlockWorldPos.ToString(),
			ResolvedInstances.Num());
		return false;
	}

	LocalPresentationStates.Add(SwapItem.BlockWorldPos, MoveTemp(PresentationStates));
	return true;
}

bool UChunkWorldBlockSwapComponent::RestoreRepresentedInstance(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	TArray<FLocalSwapPresentationState> PresentationStates;
	if (!LocalPresentationStates.RemoveAndCopyValue(SwapItem.BlockWorldPos, PresentationStates))
	{
		return false;
	}

	if (!HasRepresentedMeshAt(SwapItem.BlockWorldPos))
	{
		return false;
	}

	bool bRestoredAny = false;
	for (const FLocalSwapPresentationState& PresentationState : PresentationStates)
	{
		UInstancedStaticMeshComponent* MeshComponent = PresentationState.Component.Get();
		if (MeshComponent == nullptr)
		{
			UE_LOG(
				LogChunkWorldBlockSwapComponent,
				Warning,
				TEXT("BlockSwapComponent FailedToRestoreInstance Owner=%s Block=%s Reason=MissingMeshComponent"),
				*GetNameSafe(GetOwner()),
				*SwapItem.BlockWorldPos.ToString());
			continue;
		}

		if (!MeshComponent->UpdateInstanceTransform(PresentationState.InstanceIndex, PresentationState.OriginalTransform, true, true, true))
		{
			UE_LOG(
				LogChunkWorldBlockSwapComponent,
				Warning,
				TEXT("BlockSwapComponent FailedToRestoreInstance Owner=%s Block=%s Index=%d"),
				*GetNameSafe(GetOwner()),
				*SwapItem.BlockWorldPos.ToString(),
				PresentationState.InstanceIndex);
			continue;
		}

		bRestoredAny = true;
	}

	return bRestoredAny;
}

bool UChunkWorldBlockSwapComponent::HasRepresentedMeshAt(const FIntVector& BlockWorldPos) const
{
	if (bRepresentedInstanceIndexDirty)
	{
		RebuildRepresentedInstanceIndex();
	}

	if (const TArray<FResolvedSwapInstance>* IndexedInstances = RepresentedInstanceIndex.Find(BlockWorldPos))
	{
		return !IndexedInstances->IsEmpty();
	}

	return false;
}

bool UChunkWorldBlockSwapComponent::ResolveRepresentedInstance(const FIntVector& BlockWorldPos, FResolvedSwapInstance& OutResolvedInstance) const
{
	FReplicatedChunkWorldSwapItem LookupItem;
	LookupItem.BlockWorldPos = BlockWorldPos;

	if (const FReplicatedChunkWorldSwapItem* ExistingItem = FindActiveSwapByBlockWorldPos(BlockWorldPos))
	{
		LookupItem = *ExistingItem;
	}
	else if (AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner())
	{
		LookupItem.MeshId = ChunkWorld->GetMeshDataByBlockWorldPos(BlockWorldPos).MeshId;
	}

	TArray<FResolvedSwapInstance> ResolvedInstances;
	ResolveRepresentedInstances(LookupItem, ResolvedInstances);
	if (ResolvedInstances.IsEmpty())
	{
		return false;
	}

	OutResolvedInstance = ResolvedInstances[0];
	return true;
}

void UChunkWorldBlockSwapComponent::ResolveRepresentedInstances(const FReplicatedChunkWorldSwapItem& SwapItem, TArray<FResolvedSwapInstance>& OutResolvedInstances) const
{
	OutResolvedInstances.Reset();

	AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner();
	if (ChunkWorld == nullptr || ChunkWorld->PrimLayer == nullptr)
	{
		return;
	}

	if (bRepresentedInstanceIndexDirty)
	{
		RebuildRepresentedInstanceIndex();
	}

	const FIntVector& BlockWorldPos = SwapItem.BlockWorldPos;
	const FMeshData MeshData = ChunkWorld->GetMeshDataByBlockWorldPos(BlockWorldPos);
	const int32 ExpectedMeshId =
		(SwapItem.MeshId != INDEX_NONE && SwapItem.MeshId != EmptyMesh && SwapItem.MeshId != DefaultMesh)
			? SwapItem.MeshId
			: MeshData.MeshId;
	if (ExpectedMeshId == EmptyMesh || ExpectedMeshId == DefaultMesh)
	{
		return;
	}

	const TArray<FResolvedSwapInstance>* IndexedInstances = RepresentedInstanceIndex.Find(BlockWorldPos);
	if (IndexedInstances == nullptr)
	{
		return;
	}

	for (const FResolvedSwapInstance& IndexedInstance : *IndexedInstances)
	{
		if (IndexedInstance.MeshId != ExpectedMeshId)
		{
			continue;
		}

		OutResolvedInstances.Add(IndexedInstance);
	}

}

void UChunkWorldBlockSwapComponent::RebuildRepresentedInstanceIndex() const
{
	RepresentedInstanceIndex.Reset();

	AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner();
	if (ChunkWorld == nullptr || ChunkWorld->PrimLayer == nullptr)
	{
		bRepresentedInstanceIndexDirty = false;
		return;
	}

	TMap<TObjectPtr<UStaticMesh>, int32> MeshIdByStaticMesh;
	if (ChunkWorld->WorldGenDef != nullptr)
	{
		const int32 WorldMeshCount = ChunkWorld->WorldGenDef->WorldMeshes.Num();
		for (int32 MeshId = 0; MeshId < WorldMeshCount; ++MeshId)
		{
			if (UStaticMesh* StaticMesh = ChunkWorld->WorldGenDef->WorldMeshes[MeshId].Mesh)
			{
				MeshIdByStaticMesh.FindOrAdd(StaticMesh, MeshId);
			}
		}
	}

	const float HalfExtent = static_cast<float>(ChunkWorld->PrimLayer->BlockSize) * 0.5f;
	TInlineComponentArray<UInstancedStaticMeshComponent*> InstancedMeshComponents;
	ChunkWorld->GetComponents(InstancedMeshComponents);
	int32 IndexedInstanceCount = 0;
	for (UInstancedStaticMeshComponent* MeshComponent : InstancedMeshComponents)
	{
		if (MeshComponent == nullptr || MeshComponent->GetOwner() != ChunkWorld)
		{
			continue;
		}

		const int32* MeshIdPtr = MeshIdByStaticMesh.Find(MeshComponent->GetStaticMesh());
		if (MeshIdPtr == nullptr)
		{
			continue;
		}

		const int32 InstanceCount = MeshComponent->GetInstanceCount();
		for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
		{
			FTransform InstanceTransform;
			if (!MeshComponent->GetInstanceTransform(InstanceIndex, InstanceTransform, true))
			{
				continue;
			}

			const FVector RepresentativeWorldPos = InstanceTransform.GetLocation() + FVector(0.0f, 0.0f, HalfExtent);
			const FIntVector BlockWorldPos = ChunkWorld->UEWorldPosToBlockWorldPos(RepresentativeWorldPos);

			FResolvedSwapInstance& IndexedInstance = RepresentedInstanceIndex.FindOrAdd(BlockWorldPos).AddDefaulted_GetRef();
			IndexedInstance.Component = MeshComponent;
			IndexedInstance.InstanceIndex = InstanceIndex;
			IndexedInstance.MeshId = *MeshIdPtr;
			IndexedInstance.InstanceTransform = InstanceTransform;
			++IndexedInstanceCount;
		}
	}

	bRepresentedInstanceIndexDirty = false;
}

FTransform UChunkWorldBlockSwapComponent::BuildParkingTransform(const FIntVector& BlockWorldPos, const FTransform& OriginalTransform) const
{
	FTransform ParkingTransform = OriginalTransform;
	ParkingTransform.SetLocation(BuildParkingLocation(BlockWorldPos));
	return ParkingTransform;
}

FVector UChunkWorldBlockSwapComponent::BuildParkingLocation(const FIntVector& BlockWorldPos) const
{
	const int32 SafeCellsPerAxis = FMath::Max(ParkingCellsPerAxis, 1);
	const int32 CellCountPerLayer = SafeCellsPerAxis * SafeCellsPerAxis;
	const uint32 Hash = HashCombineFast(HashCombineFast(GetTypeHash(BlockWorldPos.X), GetTypeHash(BlockWorldPos.Y)), GetTypeHash(BlockWorldPos.Z));
	const int32 LinearIndex = static_cast<int32>(Hash % static_cast<uint32>(FMath::Max(CellCountPerLayer * SafeCellsPerAxis, 1)));
	const int32 CellX = LinearIndex % SafeCellsPerAxis;
	const int32 CellY = (LinearIndex / SafeCellsPerAxis) % SafeCellsPerAxis;
	const int32 LayerIndex = LinearIndex / CellCountPerLayer;
	return GetParkingOrigin()
		+ FVector(
			static_cast<float>(CellX) * ParkingCellSize,
			static_cast<float>(CellY) * ParkingCellSize,
			static_cast<float>(LayerIndex) * ParkingLayerHeight);
}

void UChunkWorldBlockSwapComponent::HandleReplicatedSwapAdded(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	UpsertActiveSwap(SwapItem);
	(void)ApplySwapPresentationState(SwapItem, true);
	ReconcileSwapActorPresentation(SwapItem);
	QueueActiveSwapPresentationRefresh();
}

void UChunkWorldBlockSwapComponent::HandleReplicatedSwapRemoved(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	if (AActor* SwapActor = SwapItem.SwapActor.Get())
	{
		SwapActor->SetActorHiddenInGame(true);
		SwapActor->SetActorEnableCollision(false);
	}

	PendingSwapActorReconcileBlocks.Remove(SwapItem.BlockWorldPos);
	UpdatePendingSwapActorReconcileTimer();
	(void)ApplySwapPresentationState(SwapItem, false);
	ActiveSwaps.RemoveAllSwap(
		[&SwapItem](const FReplicatedChunkWorldSwapItem& Item)
		{
			return Item.BlockWorldPos == SwapItem.BlockWorldPos;
		});

	if (ActiveSwaps.IsEmpty())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PendingActiveSwapPresentationRefreshHandle);
		}
		RemainingActiveSwapPresentationRefreshAttempts = 0;
	}
}

void UChunkWorldBlockSwapComponent::HandleReplicatedSwapChanged(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	UpsertActiveSwap(SwapItem);
	(void)ApplySwapPresentationState(SwapItem, true);
	ReconcileSwapActorPresentation(SwapItem);
	QueueActiveSwapPresentationRefresh();
}

void UChunkWorldBlockSwapComponent::ReconcileSwapActorPresentation(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	const bool bHasLocalPresentationState = LocalPresentationStates.Contains(SwapItem.BlockWorldPos);
	AActor* SwapActor = SwapItem.SwapActor.Get();
	if (!bHasLocalPresentationState || SwapActor == nullptr)
	{
		PendingSwapActorReconcileBlocks.Add(SwapItem.BlockWorldPos);
		UpdatePendingSwapActorReconcileTimer();
		return;
	}

	SwapActor->SetActorHiddenInGame(false);
	SwapActor->SetActorEnableCollision(true);
	PendingSwapActorReconcileBlocks.Remove(SwapItem.BlockWorldPos);
	UpdatePendingSwapActorReconcileTimer();
}

void UChunkWorldBlockSwapComponent::ReconcilePendingSwapActors()
{
	TArray<FIntVector> PendingBlocks;
	PendingBlocks.Reserve(PendingSwapActorReconcileBlocks.Num());
	for (const FIntVector& BlockWorldPos : PendingSwapActorReconcileBlocks)
	{
		PendingBlocks.Add(BlockWorldPos);
	}
	for (const FIntVector& BlockWorldPos : PendingBlocks)
	{
		const FReplicatedChunkWorldSwapItem* SwapItem = FindActiveSwapByBlockWorldPos(BlockWorldPos);
		if (SwapItem == nullptr)
		{
			PendingSwapActorReconcileBlocks.Remove(BlockWorldPos);
			continue;
		}

		if (LocalPresentationStates.Contains(BlockWorldPos) && SwapItem->SwapActor.Get() != nullptr)
		{
			AActor* SwapActor = SwapItem->SwapActor.Get();
			SwapActor->SetActorHiddenInGame(false);
			SwapActor->SetActorEnableCollision(true);
			PendingSwapActorReconcileBlocks.Remove(BlockWorldPos);
		}
	}

	UpdatePendingSwapActorReconcileTimer();
}

void UChunkWorldBlockSwapComponent::UpdatePendingSwapActorReconcileTimer()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	FTimerManager& TimerManager = World->GetTimerManager();
	if (!PendingSwapActorReconcileBlocks.IsEmpty())
	{
		TimerManager.SetTimer(PendingSwapActorReconcileHandle, this, &UChunkWorldBlockSwapComponent::ReconcilePendingSwapActors, 0.05f, true);
		return;
	}

	TimerManager.ClearTimer(PendingSwapActorReconcileHandle);
}

void UChunkWorldBlockSwapComponent::ProcessQueuedActiveSwapPresentationRefresh()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	RefreshActiveSwapPresentationStates();

	if (ActiveSwaps.IsEmpty())
	{
		RemainingActiveSwapPresentationRefreshAttempts = 0;
		World->GetTimerManager().ClearTimer(PendingActiveSwapPresentationRefreshHandle);
		return;
	}

	--RemainingActiveSwapPresentationRefreshAttempts;
	if (RemainingActiveSwapPresentationRefreshAttempts <= 0)
	{
		World->GetTimerManager().ClearTimer(PendingActiveSwapPresentationRefreshHandle);
	}
}

void UChunkWorldBlockSwapComponent::HandleNetworkSwapEntered(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	HandleReplicatedSwapAdded(SwapItem);
}

void UChunkWorldBlockSwapComponent::HandleNetworkSwapChanged(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	HandleReplicatedSwapChanged(SwapItem);
}

void UChunkWorldBlockSwapComponent::HandleNetworkSwapExited(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	HandleReplicatedSwapRemoved(SwapItem);
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

FReplicatedChunkWorldSwapItem* UChunkWorldBlockSwapComponent::FindActiveSwapByBlockWorldPos(const FIntVector& BlockWorldPos)
{
	return ActiveSwaps.FindByPredicate(
		[&BlockWorldPos](const FReplicatedChunkWorldSwapItem& Item)
		{
			return Item.BlockWorldPos == BlockWorldPos;
		});
}

const FReplicatedChunkWorldSwapItem* UChunkWorldBlockSwapComponent::FindActiveSwapByBlockWorldPos(const FIntVector& BlockWorldPos) const
{
	return ActiveSwaps.FindByPredicate(
		[&BlockWorldPos](const FReplicatedChunkWorldSwapItem& Item)
		{
			return Item.BlockWorldPos == BlockWorldPos;
		});
}

void UChunkWorldBlockSwapComponent::UpsertActiveSwap(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	if (FReplicatedChunkWorldSwapItem* ExistingItem = FindActiveSwapByBlockWorldPos(SwapItem.BlockWorldPos))
	{
		*ExistingItem = SwapItem;
		return;
	}

	ActiveSwaps.Add(SwapItem);
}

FVector UChunkWorldBlockSwapComponent::GetParkingWorldOffsetValue() const
{
	return ParkingWorldOffset;
}
