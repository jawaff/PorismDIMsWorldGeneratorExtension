// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Components/ChunkWorldBlockSwapComponent.h"

#include "Block/BlockTypeSchemaRegistry.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "ChunkWorldStructs/ChunkWorldEnums.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogChunkWorldBlockSwapComponent, Log, All);

UChunkWorldBlockSwapComponent::UChunkWorldBlockSwapComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	ReplicatedActiveSwaps.Owner = this;
}

void UChunkWorldBlockSwapComponent::BeginPlay()
{
	Super::BeginPlay();

	ReplicatedActiveSwaps.Owner = this;
}

void UChunkWorldBlockSwapComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingSwapActorReconcileHandle);
	}

	PendingSwapActorReconcileBlocks.Reset();
	LocalPresentationStates.Reset();
	Super::EndPlay(EndPlayReason);
}

void UChunkWorldBlockSwapComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UChunkWorldBlockSwapComponent, ReplicatedActiveSwaps);
}

bool UChunkWorldBlockSwapComponent::TryApplySwapRequest(const FIntVector& BlockWorldPos, const FGameplayTag& BlockTypeName, bool bEntering)
{
	AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner();
	if (ChunkWorld == nullptr || !ChunkWorld->HasAuthority())
	{
		UE_LOG(
			LogChunkWorldBlockSwapComponent,
			Log,
			TEXT("BlockSwapComponent RejectApply Owner=%s Block=%s Entering=%d Reason=MissingAuthorityOrOwner"),
			*GetNameSafe(GetOwner()),
			*BlockWorldPos.ToString(),
			bEntering ? 1 : 0);
		return false;
	}

	if (!CanApplySwapRequest(BlockWorldPos, bEntering))
	{
		UE_LOG(
			LogChunkWorldBlockSwapComponent,
			Log,
			TEXT("BlockSwapComponent RejectApply Owner=%s Block=%s Entering=%d Reason=CanApplySwapRequestFailed ActiveStates=%d ReplicatedItems=%d"),
			*GetNameSafe(GetOwner()),
			*BlockWorldPos.ToString(),
			bEntering ? 1 : 0,
			LocalPresentationStates.Num(),
			ReplicatedActiveSwaps.Items.Num());
		return false;
	}

	const bool bApplied = bEntering
		? AddActiveSwap(BlockWorldPos, BlockTypeName)
		: RemoveActiveSwap(BlockWorldPos);
	UE_LOG(
		LogChunkWorldBlockSwapComponent,
		Log,
		TEXT("BlockSwapComponent ApplyRequest Owner=%s Block=%s BlockType=%s Entering=%d Result=%d ActiveStates=%d ReplicatedItems=%d"),
		*GetNameSafe(GetOwner()),
		*BlockWorldPos.ToString(),
		*BlockTypeName.ToString(),
		bEntering ? 1 : 0,
		bApplied ? 1 : 0,
		LocalPresentationStates.Num(),
		ReplicatedActiveSwaps.Items.Num());
	return bApplied;
}

FVector UChunkWorldBlockSwapComponent::GetParkingOrigin() const
{
	const AActor* OwnerActor = GetOwner();
	return OwnerActor != nullptr
		? OwnerActor->GetActorLocation() + ParkingWorldOffset
		: ParkingWorldOffset;
}

void UChunkWorldBlockSwapComponent::SetParkingWorldOffset(const FVector& InParkingWorldOffset)
{
	// Project code configures this at startup so swap parking can stay generic in the plugin.
	ParkingWorldOffset = InParkingWorldOffset;
}

bool UChunkWorldBlockSwapComponent::TrySetSwapActor(const FIntVector& BlockWorldPos, AActor* SwapActor)
{
	AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner();
	if (ChunkWorld == nullptr || !ChunkWorld->HasAuthority())
	{
		return false;
	}

	FReplicatedChunkWorldSwapItem* ExistingItem = ReplicatedActiveSwaps.FindByBlockWorldPos(BlockWorldPos);
	if (ExistingItem == nullptr)
	{
		return false;
	}

	ExistingItem->SwapActor = SwapActor;
	ReplicatedActiveSwaps.MarkItemDirty(*ExistingItem);
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

bool UChunkWorldBlockSwapComponent::CanApplySwapRequest(const FIntVector& BlockWorldPos, bool bEntering) const
{
	const bool bAlreadyActive = ReplicatedActiveSwaps.FindByBlockWorldPos(BlockWorldPos) != nullptr;
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

bool UChunkWorldBlockSwapComponent::AddActiveSwap(const FIntVector& BlockWorldPos, const FGameplayTag& BlockTypeName)
{
	FReplicatedChunkWorldSwapItem NewItem;
	NewItem.BlockWorldPos = BlockWorldPos;
	NewItem.BlockTypeName = BlockTypeName;
	if (!ApplySwapPresentationState(NewItem, true))
	{
		return false;
	}

	FReplicatedChunkWorldSwapItem& AddedItem = ReplicatedActiveSwaps.Items.Add_GetRef(NewItem);
	ReplicatedActiveSwaps.MarkItemDirty(AddedItem);
	return true;
}

bool UChunkWorldBlockSwapComponent::RemoveActiveSwap(const FIntVector& BlockWorldPos)
{
	const int32 ExistingIndex = ReplicatedActiveSwaps.Items.IndexOfByPredicate(
		[&BlockWorldPos](const FReplicatedChunkWorldSwapItem& Item)
		{
			return Item.BlockWorldPos == BlockWorldPos;
		});
	if (!ReplicatedActiveSwaps.Items.IsValidIndex(ExistingIndex))
	{
		return false;
	}

	const FReplicatedChunkWorldSwapItem ExistingItem = ReplicatedActiveSwaps.Items[ExistingIndex];
	(void)ApplySwapPresentationState(ExistingItem, false);
	ReplicatedActiveSwaps.Items.RemoveAt(ExistingIndex);
	ReplicatedActiveSwaps.MarkArrayDirty();
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

	FResolvedSwapInstance ResolvedInstance;
	if (!ResolveRepresentedInstance(SwapItem.BlockWorldPos, ResolvedInstance))
	{
		UE_LOG(
			LogChunkWorldBlockSwapComponent,
			Warning,
			TEXT("BlockSwapComponent FailedToResolveInstanceForParking Owner=%s Block=%s"),
			*GetNameSafe(GetOwner()),
			*SwapItem.BlockWorldPos.ToString());
		return false;
	}

	UInstancedStaticMeshComponent* MeshComponent = ResolvedInstance.Component.Get();
	if (MeshComponent == nullptr)
	{
		return false;
	}

	FLocalSwapPresentationState& PresentationState = LocalPresentationStates.Add(SwapItem.BlockWorldPos);
	PresentationState.Component = MeshComponent;
	PresentationState.InstanceIndex = ResolvedInstance.InstanceIndex;
	PresentationState.MeshId = ResolvedInstance.MeshId;
	PresentationState.OriginalTransform = ResolvedInstance.InstanceTransform;
	PresentationState.ParkedTransform = BuildParkingTransform(SwapItem.BlockWorldPos, ResolvedInstance.InstanceTransform);

	if (!MeshComponent->UpdateInstanceTransform(PresentationState.InstanceIndex, PresentationState.ParkedTransform, true, true, true))
	{
		LocalPresentationStates.Remove(SwapItem.BlockWorldPos);
		UE_LOG(
			LogChunkWorldBlockSwapComponent,
			Warning,
			TEXT("BlockSwapComponent FailedToParkInstance Owner=%s Block=%s Index=%d"),
			*GetNameSafe(GetOwner()),
			*SwapItem.BlockWorldPos.ToString(),
			PresentationState.InstanceIndex);
		return false;
	}

	UE_LOG(
		LogChunkWorldBlockSwapComponent,
		Log,
		TEXT("BlockSwapComponent ParkInstance Owner=%s Block=%s MeshId=%d Index=%d ParkingLocation=%s"),
		*GetNameSafe(GetOwner()),
		*SwapItem.BlockWorldPos.ToString(),
		PresentationState.MeshId,
		PresentationState.InstanceIndex,
		*PresentationState.ParkedTransform.GetLocation().ToString());
	return true;
}

bool UChunkWorldBlockSwapComponent::RestoreRepresentedInstance(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	FLocalSwapPresentationState PresentationState;
	if (!LocalPresentationStates.RemoveAndCopyValue(SwapItem.BlockWorldPos, PresentationState))
	{
		UE_LOG(
			LogChunkWorldBlockSwapComponent,
			Log,
			TEXT("BlockSwapComponent SkipRestore Owner=%s Block=%s Reason=MissingLocalPresentationState"),
			*GetNameSafe(GetOwner()),
			*SwapItem.BlockWorldPos.ToString());
		return false;
	}

	UInstancedStaticMeshComponent* MeshComponent = PresentationState.Component.Get();
	if (MeshComponent == nullptr)
	{
		UE_LOG(
			LogChunkWorldBlockSwapComponent,
			Warning,
			TEXT("BlockSwapComponent FailedToRestoreInstance Owner=%s Block=%s Reason=MissingMeshComponent"),
			*GetNameSafe(GetOwner()),
			*SwapItem.BlockWorldPos.ToString());
		return false;
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
		return false;
	}

	UE_LOG(
		LogChunkWorldBlockSwapComponent,
		Log,
		TEXT("BlockSwapComponent RestoreInstance Owner=%s Block=%s MeshId=%d Index=%d"),
		*GetNameSafe(GetOwner()),
		*SwapItem.BlockWorldPos.ToString(),
		PresentationState.MeshId,
		PresentationState.InstanceIndex);
	return true;
}

bool UChunkWorldBlockSwapComponent::ResolveRepresentedInstance(const FIntVector& BlockWorldPos, FResolvedSwapInstance& OutResolvedInstance) const
{
	AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner();
	if (ChunkWorld == nullptr || ChunkWorld->PrimLayer == nullptr)
	{
		return false;
	}

	const FMeshData MeshData = ChunkWorld->GetMeshDataByBlockWorldPos(BlockWorldPos);
	if (MeshData.MeshId == EmptyMesh || MeshData.MeshId == DefaultMesh)
	{
		return false;
	}

	UStaticMesh* ExpectedMesh = ChunkWorld->MeshIndexToMesh(MeshData.MeshId);
	if (ExpectedMesh == nullptr)
	{
		return false;
	}

	TInlineComponentArray<UInstancedStaticMeshComponent*> InstancedMeshComponents;
	ChunkWorld->GetComponents(InstancedMeshComponents);

	const float HalfExtent = static_cast<float>(ChunkWorld->PrimLayer->BlockSize) * 0.5f;
	const FBox QueryBox = FBox::BuildAABB(ChunkWorld->BlockWorldPosToUEWorldPos(BlockWorldPos), FVector(HalfExtent + 1.0f));
	for (UInstancedStaticMeshComponent* MeshComponent : InstancedMeshComponents)
	{
		if (MeshComponent == nullptr || MeshComponent->GetOwner() != ChunkWorld || MeshComponent->GetStaticMesh() != ExpectedMesh)
		{
			continue;
		}

		const TArray<int32> CandidateIndices = MeshComponent->GetInstancesOverlappingBox(QueryBox, true);
		for (const int32 CandidateIndex : CandidateIndices)
		{
			FTransform InstanceTransform;
			if (!MeshComponent->GetInstanceTransform(CandidateIndex, InstanceTransform, true))
			{
				continue;
			}

			const FVector RepresentativeWorldPos = InstanceTransform.GetLocation() + FVector(0.0f, 0.0f, HalfExtent);
			if (ChunkWorld->UEWorldPosToBlockWorldPos(RepresentativeWorldPos) != BlockWorldPos)
			{
				continue;
			}

			OutResolvedInstance.Component = MeshComponent;
			OutResolvedInstance.InstanceIndex = CandidateIndex;
			OutResolvedInstance.MeshId = MeshData.MeshId;
			OutResolvedInstance.InstanceTransform = InstanceTransform;
			return true;
		}
	}

	return false;
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
	(void)ApplySwapPresentationState(SwapItem, true);
	ReconcileSwapActorPresentation(SwapItem);
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
}

void UChunkWorldBlockSwapComponent::HandleReplicatedSwapChanged(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	(void)ApplySwapPresentationState(SwapItem, true);
	ReconcileSwapActorPresentation(SwapItem);
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
		const FReplicatedChunkWorldSwapItem* SwapItem = ReplicatedActiveSwaps.FindByBlockWorldPos(BlockWorldPos);
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

AChunkWorldExtended* UChunkWorldBlockSwapComponent::GetChunkWorldOwner() const
{
	return Cast<AChunkWorldExtended>(GetOwner());
}

UBlockTypeSchemaComponent* UChunkWorldBlockSwapComponent::GetSchemaComponent() const
{
	AChunkWorldExtended* ChunkWorld = GetChunkWorldOwner();
	return ChunkWorld != nullptr ? ChunkWorld->GetBlockTypeSchemaComponent() : nullptr;
}
