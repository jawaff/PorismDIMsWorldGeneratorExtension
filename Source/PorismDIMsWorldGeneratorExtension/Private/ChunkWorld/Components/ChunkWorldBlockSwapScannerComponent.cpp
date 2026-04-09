// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Components/ChunkWorldBlockSwapScannerComponent.h"

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockHitBlueprintLibrary.h"
#include "ChunkWorld/ChunkWorld.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockSwapComponent.h"
#include "ChunkWorld/Actors/Components/ChunkWorldProximityComponent.h"
#include "ChunkWorld/Subsystems/ChunkWorldSwapRegistrationSubsystem.h"
#include "ChunkWorldStructs/ChunkWorldEnums.h"
#include "CollisionShape.h"
#include "DrawDebugHelpers.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "Engine/StreamableManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogChunkWorldBlockSwapScanner, Log, All);

namespace
{
	bool IsChunkWorldUsable(UWorld* ExpectedWorld, AChunkWorld* ChunkWorld)
	{
		return IsValid(ChunkWorld)
			&& !ChunkWorld->IsActorBeingDestroyed()
			&& ChunkWorld->GetWorld() == ExpectedWorld
			&& ChunkWorld->PrimLayer != nullptr
			&& ChunkWorld->IsRuning();
	}

	bool TryApplySharedBlockSwap(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const FGameplayTag& BlockTypeName, bool bEntering)
	{
		AChunkWorldExtended* ExtendedChunkWorld = Cast<AChunkWorldExtended>(ChunkWorld);
		if (ExtendedChunkWorld == nullptr)
		{
			return false;
		}

		UChunkWorldBlockSwapComponent* SwapComponent = ExtendedChunkWorld->GetBlockSwapComponent();
		return SwapComponent != nullptr && SwapComponent->TryApplySwapRequest(BlockWorldPos, BlockTypeName, bEntering);
	}
}

UChunkWorldBlockSwapScannerComponent::UChunkWorldBlockSwapScannerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UChunkWorldBlockSwapScannerComponent::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(
		LogChunkWorldBlockSwapScanner,
		Log,
		TEXT("SwapScanner BeginPlay Scanner=%s Owner=%s NetMode=%d"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		GetWorld() != nullptr ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE);

	if (UWorld* World = GetWorld())
	{
		if (UChunkWorldSwapRegistrationSubsystem* SwapRegistrationSubsystem = World->GetSubsystem<UChunkWorldSwapRegistrationSubsystem>())
		{
			SwapRegistrationSubsystem->RegisterSwapScanner(this);
		}
	}

	UpdateSwapScanTimer();
	LogSwapDiagnostics(TEXT("BeginPlay"));
}

void UChunkWorldBlockSwapScannerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(
		LogChunkWorldBlockSwapScanner,
		Log,
		TEXT("SwapScanner EndPlay Scanner=%s Owner=%s Reason=%d ActiveSwaps=%d Sources=%d"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		static_cast<int32>(EndPlayReason),
		ActiveSwaps.Num(),
		ProximitySources.Num());

	if (UWorld* World = GetWorld())
	{
		if (UChunkWorldSwapRegistrationSubsystem* SwapRegistrationSubsystem = World->GetSubsystem<UChunkWorldSwapRegistrationSubsystem>())
		{
			SwapRegistrationSubsystem->UnregisterSwapScanner(this);
		}
	}

	if (SwapScanHandle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(SwapScanHandle);
	}

	RestoreActiveSwapsForShutdown();

	for (TPair<FIntVector, FPendingSwapActorLoad>& PendingLoad : PendingSwapActorLoads)
	{
		if (PendingLoad.Value.Handle.IsValid())
		{
			PendingLoad.Value.Handle->CancelHandle();
		}
	}

	PendingSwapActorLoads.Reset();
	ProximitySources.Reset();
	ActiveSwaps.Reset();
	LogSwapDiagnostics(TEXT("EndPlay"));

	Super::EndPlay(EndPlayReason);
}

void UChunkWorldBlockSwapScannerComponent::RegisterProximitySource(UChunkWorldProximityComponent* Source)
{
	if (Source == nullptr)
	{
		return;
	}

	ProximitySources.AddUnique(Source);
	UE_LOG(
		LogChunkWorldBlockSwapScanner,
		Log,
		TEXT("SwapScanner RegisterSource Scanner=%s Owner=%s Source=%s SourceOwner=%s Sources=%d"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(Source),
		*GetNameSafe(Source->GetOwner()),
		ProximitySources.Num());
	UpdateSwapScanTimer();
	LogSwapDiagnostics(TEXT("RegisterProximitySource"));
}

void UChunkWorldBlockSwapScannerComponent::UnregisterProximitySource(UChunkWorldProximityComponent* Source)
{
	if (Source == nullptr)
	{
		return;
	}

	ProximitySources.RemoveSingleSwap(Source);
	UE_LOG(
		LogChunkWorldBlockSwapScanner,
		Log,
		TEXT("SwapScanner UnregisterSource Scanner=%s Owner=%s Source=%s SourceOwner=%s Sources=%d"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(Source),
		*GetNameSafe(Source->GetOwner()),
		ProximitySources.Num());
	UpdateSwapScanTimer();
	LogSwapDiagnostics(TEXT("UnregisterProximitySource"));
}

bool UChunkWorldBlockSwapScannerComponent::ForceRemoveSwapForDestroyedBlock(const FIntVector& BlockWorldPos)
{
	UWorld* World = GetWorld();
	AChunkWorld* ChunkWorld = GetOwningChunkWorld();
	if (World == nullptr || !IsChunkWorldUsable(World, ChunkWorld))
	{
		return false;
	}

	return ForceRemoveSwap(ChunkWorld, BlockWorldPos, TEXT("DestroyedBlock"));
}

void UChunkWorldBlockSwapScannerComponent::ScanForSwaps()
{
	if (!ShouldRunSwapScan())
	{
		UE_LOG(LogChunkWorldBlockSwapScanner, Log, TEXT("SwapScanner SkipScan Scanner=%s Owner=%s Reason=ShouldRunSwapScanFalse"), *GetNameSafe(this), *GetNameSafe(GetOwner()));
		return;
	}

	CleanupInvalidSources();
	if (ProximitySources.IsEmpty())
	{
		UE_LOG(LogChunkWorldBlockSwapScanner, Log, TEXT("SwapScanner SkipScan Scanner=%s Owner=%s Reason=NoSources"), *GetNameSafe(this), *GetNameSafe(GetOwner()));
		return;
	}

	UWorld* World = GetWorld();
	AChunkWorld* ChunkWorld = GetOwningChunkWorld();
	if (World == nullptr || !IsChunkWorldUsable(World, ChunkWorld))
	{
		UE_LOG(LogChunkWorldBlockSwapScanner, Log, TEXT("SwapScanner SkipScan Scanner=%s Owner=%s Reason=ChunkWorldNotUsable"), *GetNameSafe(this), *GetNameSafe(GetOwner()));
		return;
	}

	AChunkWorldExtended* ExtendedChunkWorld = Cast<AChunkWorldExtended>(ChunkWorld);
	UBlockTypeSchemaComponent* SchemaComponent = ExtendedChunkWorld != nullptr
		? ExtendedChunkWorld->GetBlockTypeSchemaComponent()
		: nullptr;
	if (SchemaComponent == nullptr)
	{
		UE_LOG(LogChunkWorldBlockSwapScanner, Log, TEXT("SwapScanner SkipScan Scanner=%s Owner=%s Reason=MissingSchemaComponent"), *GetNameSafe(this), *GetNameSafe(GetOwner()));
		return;
	}

	const int32 BlockSize = ChunkWorld->PrimLayer->BlockSize;
	if (BlockSize <= 0)
	{
		UE_LOG(LogChunkWorldBlockSwapScanner, Log, TEXT("SwapScanner SkipScan Scanner=%s Owner=%s Reason=InvalidBlockSize BlockSize=%d"), *GetNameSafe(this), *GetNameSafe(GetOwner()), BlockSize);
		return;
	}

	UE_LOG(
		LogChunkWorldBlockSwapScanner,
		Log,
		TEXT("SwapScanner StartScan Scanner=%s Owner=%s Sources=%d ActiveSwaps=%d Interval=%.3f Budget=%d"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		ProximitySources.Num(),
		ActiveSwaps.Num(),
		SwapScanInterval,
		MaxBlocksPerScan);

	int32 ProcessedBlocks = 0;
	int32 SwapsEntered = 0;
	int32 SwapsExited = 0;
	FSwapScanDiagnostics Diagnostics;
	TSet<FIntVector> EvaluatedBlockPositions;
	for (const TWeakObjectPtr<UChunkWorldProximityComponent>& SourcePtr : ProximitySources)
	{
		if (UChunkWorldProximityComponent* Source = SourcePtr.Get())
		{
			GatherSourceCandidateBlocks(ChunkWorld, Source, EvaluatedBlockPositions, ProcessedBlocks, SwapsEntered, Diagnostics);
		}
	}

	if (ActiveSwaps.Num() > 0)
	{
		TArray<FIntVector> ActiveKeys;
		ActiveSwaps.GenerateKeyArray(ActiveKeys);
		for (const FIntVector& BlockWorldPos : ActiveKeys)
		{
			const FActiveBlockSwap* Active = ActiveSwaps.Find(BlockWorldPos);
			if (Active != nullptr)
			{
				if (!Active->Definition.SwapActorClass.IsNull() && !Active->SpawnedActor.IsValid())
				{
					UE_LOG(
						LogChunkWorldBlockSwapScanner,
						Log,
						TEXT("SwapScanner ForceSwapOut Scanner=%s Owner=%s Block=%s BlockType=%s Reason=SpawnedActorDestroyed"),
						*GetNameSafe(this),
						*GetNameSafe(GetOwner()),
						*BlockWorldPos.ToString(),
						*Active->BlockTypeName.ToString());
					if (TryApplySharedBlockSwap(ChunkWorld, BlockWorldPos, Active->BlockTypeName, false))
					{
						ActiveSwaps.Remove(BlockWorldPos);
						++SwapsExited;
					}
					continue;
				}

				TrySwapOutBlock(ChunkWorld, BlockWorldPos, *Active);
				if (!ActiveSwaps.Contains(BlockWorldPos))
				{
					++SwapsExited;
				}
			}
		}
	}

	DrawDebugActiveSwapBlocks(ChunkWorld);

	LogSwapDiagnostics(TEXT("ScanForSwaps"), ProcessedBlocks, SwapsEntered, SwapsExited, &Diagnostics);
}

void UChunkWorldBlockSwapScannerComponent::GatherSourceCandidateBlocks(AChunkWorld* ChunkWorld, UChunkWorldProximityComponent* Source, TSet<FIntVector>& EvaluatedBlockPositions, int32& InOutProcessedBlocks, int32& InOutSwapsEntered, FSwapScanDiagnostics& Diagnostics)
{
	if (ChunkWorld == nullptr || Source == nullptr || InOutProcessedBlocks >= MaxBlocksPerScan)
	{
		return;
	}

	const FVector Origin = Source->GetScanOrigin();
	const float Radius = Source->GetProximityRadius();
	if (Radius <= 0.0f)
	{
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ChunkWorldSwapScan), false);
	if (AActor* SourceOwner = Source->GetOwner())
	{
		QueryParams.AddIgnoredActor(SourceOwner);
	}

	TArray<FHitResult> QueryHits;
	GetWorld()->SweepMultiByChannel(
		QueryHits,
		Origin,
		Origin,
		FQuat::Identity,
		Source->GetProximityCollisionChannel(),
		FCollisionShape::MakeSphere(Radius),
		QueryParams);

	Diagnostics.QueryHitCount += QueryHits.Num();
	if (QueryHits.IsEmpty())
	{
		UE_LOG(
			LogChunkWorldBlockSwapScanner,
			Verbose,
			TEXT("SwapScanner SourceQuery Scanner=%s Owner=%s Source=%s Origin=%s Radius=%.2f QueryHits=0"),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(Source),
			*Origin.ToString(),
			Radius);
		return;
	}

	AChunkWorldExtended* ExtendedChunkWorld = Cast<AChunkWorldExtended>(ChunkWorld);
	UChunkWorldBlockSwapComponent* SwapComponent = ExtendedChunkWorld != nullptr
		? ExtendedChunkWorld->GetBlockSwapComponent()
		: nullptr;

	for (const FHitResult& QueryHit : QueryHits)
	{
		if (InOutProcessedBlocks >= MaxBlocksPerScan)
		{
			break;
		}

		FVector TraceDirection = QueryHit.ImpactPoint - Origin;
		if (TraceDirection.IsNearlyZero())
		{
			TraceDirection = QueryHit.ImpactNormal.IsNearlyZero() ? FVector::UpVector : -QueryHit.ImpactNormal;
		}

		FChunkWorldResolvedBlockHit ResolvedHit;
		if (!UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromHitResult(QueryHit, TraceDirection, ResolvedHit)
			|| !ResolvedHit.bHasBlock
			|| ResolvedHit.ChunkWorld != ChunkWorld)
		{
			continue;
		}

		++Diagnostics.ResolvedBlockHits;

		const FIntVector BlockWorldPos = ResolvedHit.BlockWorldPos;
		if (EvaluatedBlockPositions.Contains(BlockWorldPos))
		{
			continue;
		}
		EvaluatedBlockPositions.Add(BlockWorldPos);

		++InOutProcessedBlocks;
		++Diagnostics.CandidateBlocksInRadius;

		const int32 MeshIndex = ResolvedHit.MeshIndex;
		if (MeshIndex == EmptyMesh || MeshIndex == DefaultMesh)
		{
			UE_LOG(
				LogChunkWorldBlockSwapScanner,
				Log,
				TEXT("SwapScanner SkipCandidate Scanner=%s Owner=%s Block=%s Reason=NotMeshBacked MeshIndex=%d"),
				*GetNameSafe(this),
				*GetNameSafe(GetOwner()),
				*BlockWorldPos.ToString(),
				MeshIndex);
			continue;
		}

		FGameplayTag BlockTypeName;
		FBlockDefinitionBase Definition;
		if (!ResolvedHit.BlockTypeSchemaComponent
			|| !ResolvedHit.BlockTypeSchemaComponent->GetBlockDefinitionForBlockWorldPos(BlockWorldPos, BlockTypeName, Definition))
		{
			continue;
		}

		++Diagnostics.SchemaHits;
		if (Definition.SwapActorClass.IsNull())
		{
			continue;
		}

		++Diagnostics.SwapAuthoredBlocks;

		FTransform SwapTransform(ResolvedHit.RepresentativeWorldPos);
		if (SwapComponent != nullptr)
		{
			(void)SwapComponent->TryGetSwapTransformForBlock(BlockWorldPos, SwapTransform);
		}

		bool bEnteredSwap = false;
		TrySwapInBlock(ChunkWorld, BlockWorldPos, SwapTransform, BlockTypeName, Definition, Diagnostics, bEnteredSwap);
		if (bEnteredSwap)
		{
			++InOutSwapsEntered;
		}
	}
}

void UChunkWorldBlockSwapScannerComponent::CleanupInvalidSources()
{
	ProximitySources.RemoveAllSwap([](const TWeakObjectPtr<UChunkWorldProximityComponent>& Source)
	{
		return !Source.IsValid();
	});
}

AChunkWorld* UChunkWorldBlockSwapScannerComponent::GetOwningChunkWorld() const
{
	return Cast<AChunkWorld>(GetOwner());
}

bool UChunkWorldBlockSwapScannerComponent::ShouldRunSwapScan() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->GetNetMode() != NM_Client;
}

void UChunkWorldBlockSwapScannerComponent::UpdateSwapScanTimer()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	FTimerManager& TimerManager = World->GetTimerManager();
	if (ShouldRunSwapScan() && !ProximitySources.IsEmpty())
	{
		TimerManager.SetTimer(SwapScanHandle, this, &UChunkWorldBlockSwapScannerComponent::ScanForSwaps, SwapScanInterval, true);
		UE_LOG(
			LogChunkWorldBlockSwapScanner,
			Log,
			TEXT("SwapScanner UpdateTimer Scanner=%s Owner=%s Action=Set Interval=%.3f Sources=%d ShouldRun=%d"),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()),
			SwapScanInterval,
			ProximitySources.Num(),
			ShouldRunSwapScan() ? 1 : 0);
		return;
	}

	TimerManager.ClearTimer(SwapScanHandle);
	UE_LOG(
		LogChunkWorldBlockSwapScanner,
		Log,
		TEXT("SwapScanner UpdateTimer Scanner=%s Owner=%s Action=Clear Sources=%d ShouldRun=%d"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		ProximitySources.Num(),
		ShouldRunSwapScan() ? 1 : 0);
}

void UChunkWorldBlockSwapScannerComponent::TrySwapInBlock(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const FTransform& SwapTransform, const FGameplayTag& BlockTypeName, const FBlockDefinitionBase& Definition, FSwapScanDiagnostics& Diagnostics, bool& bOutEnteredSwap)
{
	bOutEnteredSwap = false;

	if (ChunkWorld == nullptr)
	{
		return;
	}

	if (ActiveSwaps.Contains(BlockWorldPos))
	{
		++Diagnostics.AlreadyActiveBlocks;
		return;
	}

	if (PendingSwapActorLoads.Contains(BlockWorldPos))
	{
		return;
	}

	const FVector BlockLocation = ChunkWorld->BlockWorldPosToUEWorldPos(BlockWorldPos);
	float MinDistance = TNumericLimits<float>::Max();
	float RelevantDistance = TNumericLimits<float>::Max();
	if (!IsWithinAnySourceSwapDistance(BlockLocation, false, MinDistance, RelevantDistance))
	{
		++Diagnostics.SwapInRejectedByDistance;
		UE_LOG(
			LogChunkWorldBlockSwapScanner,
			Log,
			TEXT("SwapScanner RejectSwapIn Scanner=%s Owner=%s Block=%s BlockType=%s MinDistance=%.2f SwapInDistance=%.2f"),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()),
			*BlockWorldPos.ToString(),
			*BlockTypeName.ToString(),
			MinDistance,
			RelevantDistance);
		return;
	}

	if (Definition.SwapActorClass.IsValid())
	{
		bOutEnteredSwap = FinalizeSwapIn(ChunkWorld, BlockWorldPos, SwapTransform, BlockTypeName, Definition, MinDistance, RelevantDistance, &Diagnostics);
		return;
	}

	FPendingSwapActorLoad& PendingLoad = PendingSwapActorLoads.Add(BlockWorldPos);
	PendingLoad.BlockTypeName = BlockTypeName;
	PendingLoad.Definition = Definition;
	PendingLoad.SwapTransform = SwapTransform;
	PendingLoad.Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Definition.SwapActorClass.ToSoftObjectPath(),
		FStreamableDelegate::CreateUObject(this, &UChunkWorldBlockSwapScannerComponent::HandlePendingSwapActorClassLoaded, BlockWorldPos));

	UE_LOG(
		LogChunkWorldBlockSwapScanner,
		Log,
		TEXT("SwapScanner QueueSwapActorLoad Scanner=%s Owner=%s Block=%s BlockType=%s SwapActorClass=%s"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		*BlockWorldPos.ToString(),
		*BlockTypeName.ToString(),
		*Definition.SwapActorClass.ToString());
}

void UChunkWorldBlockSwapScannerComponent::TrySwapOutBlock(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const FActiveBlockSwap& Active)
{
	if (ChunkWorld == nullptr)
	{
		return;
	}

	const FVector BlockLocation = ChunkWorld->BlockWorldPosToUEWorldPos(BlockWorldPos);
	float MinDistance = TNumericLimits<float>::Max();
	float RelevantDistance = TNumericLimits<float>::Max();
	if (IsWithinAnySourceSwapDistance(BlockLocation, true, MinDistance, RelevantDistance))
	{
		return;
	}

	if (Active.SpawnedActor.IsValid())
	{
		Active.SpawnedActor->Destroy();
	}

	if (!TryApplySharedBlockSwap(ChunkWorld, BlockWorldPos, Active.BlockTypeName, false))
	{
		return;
	}

	ActiveSwaps.Remove(BlockWorldPos);

	UE_LOG(
		LogChunkWorldBlockSwapScanner,
		Log,
		TEXT("SwapScanner ExitSwap Scanner=%s Owner=%s Block=%s BlockType=%s RemainingActiveSwaps=%d"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		*BlockWorldPos.ToString(),
		*Active.BlockTypeName.ToString(),
		ActiveSwaps.Num());

	FChunkWorldBlockSwapRequest Request;
	Request.BlockWorldPos = BlockWorldPos;
	Request.BlockTypeName = Active.BlockTypeName;
	Request.SwapActorClass = Active.Definition.SwapActorClass;
	Request.bEntering = false;
	OnBlockSwapRequested.Broadcast(Request);
}

bool UChunkWorldBlockSwapScannerComponent::ForceRemoveSwap(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const TCHAR* Reason)
{
	if (ChunkWorld == nullptr)
	{
		return false;
	}

	if (FPendingSwapActorLoad PendingLoad; PendingSwapActorLoads.RemoveAndCopyValue(BlockWorldPos, PendingLoad))
	{
		if (PendingLoad.Handle.IsValid())
		{
			PendingLoad.Handle->CancelHandle();
		}
	}

	FActiveBlockSwap Active;
	if (!ActiveSwaps.RemoveAndCopyValue(BlockWorldPos, Active))
	{
		return false;
	}

	if (Active.SpawnedActor.IsValid())
	{
		Active.SpawnedActor->Destroy();
	}

	(void)TryApplySharedBlockSwap(ChunkWorld, BlockWorldPos, Active.BlockTypeName, false);

	UE_LOG(
		LogChunkWorldBlockSwapScanner,
		Log,
		TEXT("SwapScanner ForceRemoveSwap Scanner=%s Owner=%s Block=%s BlockType=%s Reason=%s RemainingActiveSwaps=%d"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		*BlockWorldPos.ToString(),
		*Active.BlockTypeName.ToString(),
		Reason != nullptr ? Reason : TEXT("None"),
		ActiveSwaps.Num());

	FChunkWorldBlockSwapRequest Request;
	Request.BlockWorldPos = BlockWorldPos;
	Request.BlockTypeName = Active.BlockTypeName;
	Request.SwapActorClass = Active.Definition.SwapActorClass;
	Request.bEntering = false;
	OnBlockSwapRequested.Broadcast(Request);
	return true;
}

void UChunkWorldBlockSwapScannerComponent::HandlePendingSwapActorClassLoaded(FIntVector BlockWorldPos)
{
	FPendingSwapActorLoad PendingLoad;
	if (!PendingSwapActorLoads.RemoveAndCopyValue(BlockWorldPos, PendingLoad))
	{
		return;
	}

	UWorld* World = GetWorld();
	AChunkWorld* ChunkWorld = GetOwningChunkWorld();
	if (World == nullptr || !IsChunkWorldUsable(World, ChunkWorld) || ActiveSwaps.Contains(BlockWorldPos))
	{
		return;
	}

	const FVector BlockLocation = ChunkWorld->BlockWorldPosToUEWorldPos(BlockWorldPos);
	float MinDistance = TNumericLimits<float>::Max();
	float RelevantDistance = TNumericLimits<float>::Max();
	if (!IsWithinAnySourceSwapDistance(BlockLocation, false, MinDistance, RelevantDistance))
	{
		UE_LOG(
			LogChunkWorldBlockSwapScanner,
			Log,
			TEXT("SwapScanner DropPendingSwap Scanner=%s Owner=%s Block=%s BlockType=%s Reason=NoLongerRelevant"),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()),
			*BlockWorldPos.ToString(),
			*PendingLoad.BlockTypeName.ToString());
		return;
	}

	FSwapScanDiagnostics Diagnostics;
	FinalizeSwapIn(ChunkWorld, BlockWorldPos, PendingLoad.SwapTransform, PendingLoad.BlockTypeName, PendingLoad.Definition, MinDistance, RelevantDistance, &Diagnostics);
}

bool UChunkWorldBlockSwapScannerComponent::FinalizeSwapIn(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const FTransform& SwapTransform, const FGameplayTag& BlockTypeName, const FBlockDefinitionBase& Definition, float MinDistance, float RelevantDistance, FSwapScanDiagnostics* Diagnostics)
{
	if (ChunkWorld == nullptr || ActiveSwaps.Contains(BlockWorldPos))
	{
		return false;
	}

	AChunkWorldExtended* ExtendedChunkWorld = Cast<AChunkWorldExtended>(ChunkWorld);
	UChunkWorldBlockSwapComponent* SwapComponent = ExtendedChunkWorld != nullptr
		? ExtendedChunkWorld->GetBlockSwapComponent()
		: nullptr;

	FActiveBlockSwap NewActive;
	NewActive.BlockTypeName = BlockTypeName;
	NewActive.Definition = Definition;
	NewActive.SwapTransform = SwapTransform;

	if (!TryApplySharedBlockSwap(ChunkWorld, BlockWorldPos, BlockTypeName, true))
	{
		if (Diagnostics != nullptr)
		{
			++Diagnostics->SharedApplyFailures;
		}

		UE_LOG(
			LogChunkWorldBlockSwapScanner,
			Warning,
			TEXT("SwapScanner rejected swap-in after schema resolution. Owner=%s Block=%s BlockType=%s MinDistance=%.2f SwapInDistance=%.2f"),
			*GetNameSafe(GetOwner()),
			*BlockWorldPos.ToString(),
			*BlockTypeName.ToString(),
			MinDistance,
			RelevantDistance);
		return false;
	}

	if (!Definition.SwapActorClass.IsNull())
	{
		UClass* SwapActorClass = Definition.SwapActorClass.Get();
		if (SwapActorClass == nullptr)
		{
			if (Diagnostics != nullptr)
			{
				++Diagnostics->SwapActorSpawnFailures;
			}

			UE_LOG(
				LogChunkWorldBlockSwapScanner,
				Warning,
				TEXT("SwapScanner failed to resolve loaded swap actor class. Owner=%s Block=%s BlockType=%s SwapActorClass=%s"),
				*GetNameSafe(GetOwner()),
				*BlockWorldPos.ToString(),
				*BlockTypeName.ToString(),
				*Definition.SwapActorClass.ToString());
			(void)TryApplySharedBlockSwap(ChunkWorld, BlockWorldPos, BlockTypeName, false);
			return false;
		}

		AActor* DeferredActor = GetWorld()->SpawnActorDeferred<AActor>(
			SwapActorClass,
			SwapTransform,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (DeferredActor != nullptr)
		{
			DeferredActor->SetReplicates(true);
			DeferredActor->SetActorHiddenInGame(true);
			DeferredActor->SetActorEnableCollision(false);
			UGameplayStatics::FinishSpawningActor(DeferredActor, SwapTransform);
			NewActive.SpawnedActor = DeferredActor;
		}
		if (!NewActive.SpawnedActor.IsValid())
		{
			if (Diagnostics != nullptr)
			{
				++Diagnostics->SwapActorSpawnFailures;
			}

			UE_LOG(
				LogChunkWorldBlockSwapScanner,
				Warning,
				TEXT("SwapScanner failed to spawn swap actor. Owner=%s Block=%s BlockType=%s SwapActorClass=%s"),
				*GetNameSafe(GetOwner()),
				*BlockWorldPos.ToString(),
				*BlockTypeName.ToString(),
				*Definition.SwapActorClass.ToString());
			(void)TryApplySharedBlockSwap(ChunkWorld, BlockWorldPos, BlockTypeName, false);
			return false;
		}

		if (SwapComponent == nullptr || !SwapComponent->TrySetSwapActor(BlockWorldPos, NewActive.SpawnedActor.Get()))
		{
			if (Diagnostics != nullptr)
			{
				++Diagnostics->SharedApplyFailures;
			}

			UE_LOG(
				LogChunkWorldBlockSwapScanner,
				Warning,
				TEXT("SwapScanner failed to associate replicated swap actor. Owner=%s Block=%s BlockType=%s SpawnedActor=%s"),
				*GetNameSafe(GetOwner()),
				*BlockWorldPos.ToString(),
				*BlockTypeName.ToString(),
				*GetNameSafe(NewActive.SpawnedActor.Get()));
			NewActive.SpawnedActor->Destroy();
			(void)TryApplySharedBlockSwap(ChunkWorld, BlockWorldPos, BlockTypeName, false);
			return false;
		}
	}

	ActiveSwaps.Add(BlockWorldPos, NewActive);

	UE_LOG(
		LogChunkWorldBlockSwapScanner,
		Log,
		TEXT("SwapScanner EnterSwap Scanner=%s Owner=%s Block=%s BlockType=%s MinDistance=%.2f ActiveSwaps=%d SpawnedActor=%s SpawnLocation=%s"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		*BlockWorldPos.ToString(),
		*BlockTypeName.ToString(),
		MinDistance,
		ActiveSwaps.Num(),
		*GetNameSafe(NewActive.SpawnedActor.Get()),
		*SwapTransform.GetLocation().ToString());

	FChunkWorldBlockSwapRequest Request;
	Request.BlockWorldPos = BlockWorldPos;
	Request.BlockTypeName = BlockTypeName;
	Request.SwapActorClass = Definition.SwapActorClass;
	Request.bEntering = true;
	OnBlockSwapRequested.Broadcast(Request);
	return true;
}

bool UChunkWorldBlockSwapScannerComponent::IsWithinAnySourceSwapDistance(const FVector& Location, bool bUseSwapOutDistance, float& OutMinDistance, float& OutRelevantDistance) const
{
	OutMinDistance = TNumericLimits<float>::Max();
	OutRelevantDistance = TNumericLimits<float>::Max();
	bool bWithinAnySourceDistance = false;

	for (const TWeakObjectPtr<UChunkWorldProximityComponent>& SourcePtr : ProximitySources)
	{
		const UChunkWorldProximityComponent* Source = SourcePtr.Get();
		if (Source != nullptr)
		{
			const float Distance = FVector::Dist(Source->GetScanOrigin(), Location);
			const float RelevantDistance = bUseSwapOutDistance ? Source->GetSwapOutDistance() : Source->GetSwapInDistance();
			if (Distance < OutMinDistance)
			{
				OutMinDistance = Distance;
				OutRelevantDistance = RelevantDistance;
			}

			if (Distance <= RelevantDistance)
			{
				bWithinAnySourceDistance = true;
			}
		}
	}

	return bWithinAnySourceDistance;
}

void UChunkWorldBlockSwapScannerComponent::RestoreActiveSwapsForShutdown()
{
	UWorld* World = GetWorld();
	AChunkWorld* ChunkWorld = GetOwningChunkWorld();
	const bool bCanRestoreBlockState = World != nullptr && IsChunkWorldUsable(World, ChunkWorld);

	TArray<FIntVector> ActiveKeys;
	ActiveSwaps.GenerateKeyArray(ActiveKeys);
	for (const FIntVector& BlockWorldPos : ActiveKeys)
	{
		if (const FActiveBlockSwap* Active = ActiveSwaps.Find(BlockWorldPos))
		{
			if (Active->SpawnedActor.IsValid())
			{
				Active->SpawnedActor->Destroy();
			}

			if (bCanRestoreBlockState)
			{
				TryApplySharedBlockSwap(ChunkWorld, BlockWorldPos, Active->BlockTypeName, false);
			}
		}
	}
}

void UChunkWorldBlockSwapScannerComponent::DrawDebugActiveSwapBlocks(AChunkWorld* ChunkWorld) const
{
	if (ChunkWorld == nullptr || ChunkWorld->PrimLayer == nullptr || ActiveSwaps.IsEmpty())
	{
		return;
	}

	const UChunkWorldProximityComponent* DebugSource = nullptr;
	for (const TWeakObjectPtr<UChunkWorldProximityComponent>& SourcePtr : ProximitySources)
	{
		const UChunkWorldProximityComponent* Source = SourcePtr.Get();
		if (Source != nullptr && Source->ShouldDebugDrawActiveSwapBlocks())
		{
			DebugSource = Source;
			break;
		}
	}

	if (DebugSource == nullptr)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr || !World->IsGameWorld())
	{
		return;
	}

	const float HalfExtent = static_cast<float>(ChunkWorld->PrimLayer->BlockSize) * 0.5f;
	const FVector Extent(HalfExtent, HalfExtent, HalfExtent);
	for (const TPair<FIntVector, FActiveBlockSwap>& ActiveSwap : ActiveSwaps)
	{
		DrawDebugBox(
			World,
			ChunkWorld->BlockWorldPosToUEWorldPos(ActiveSwap.Key),
			Extent,
			DebugSource->GetDebugActiveSwapBlockColor(),
			false,
			0.0f,
			0,
			DebugSource->GetDebugActiveSwapBlockThickness());
	}
}

int32 UChunkWorldBlockSwapScannerComponent::GetSpawnedSwapActorCount() const
{
	int32 SpawnedActorCount = 0;
	for (const TPair<FIntVector, FActiveBlockSwap>& ActiveSwap : ActiveSwaps)
	{
		if (ActiveSwap.Value.SpawnedActor.IsValid())
		{
			++SpawnedActorCount;
		}
	}

	return SpawnedActorCount;
}

void UChunkWorldBlockSwapScannerComponent::LogSwapDiagnostics(const TCHAR* Stage, int32 ProcessedBlocks, int32 SwapsEntered, int32 SwapsExited, const FSwapScanDiagnostics* Diagnostics) const
{
	UE_LOG(
		LogChunkWorldBlockSwapScanner,
		Log,
		TEXT("SwapScanner Stage=%s Owner=%s Sources=%d ActiveSwaps=%d SpawnedActors=%d ProcessedBlocks=%d SwapsEntered=%d SwapsExited=%d QueryHits=%d ResolvedBlockHits=%d CandidatesInRadius=%d SchemaHits=%d SwapAuthored=%d DistanceRejected=%d AlreadyActive=%d SharedApplyFailures=%d SpawnFailures=%d Interval=%.3f Budget=%d"),
		Stage,
		*GetNameSafe(GetOwner()),
		ProximitySources.Num(),
		ActiveSwaps.Num(),
		GetSpawnedSwapActorCount(),
		ProcessedBlocks,
		SwapsEntered,
		SwapsExited,
		Diagnostics != nullptr ? Diagnostics->QueryHitCount : INDEX_NONE,
		Diagnostics != nullptr ? Diagnostics->ResolvedBlockHits : INDEX_NONE,
		Diagnostics != nullptr ? Diagnostics->CandidateBlocksInRadius : INDEX_NONE,
		Diagnostics != nullptr ? Diagnostics->SchemaHits : INDEX_NONE,
		Diagnostics != nullptr ? Diagnostics->SwapAuthoredBlocks : INDEX_NONE,
		Diagnostics != nullptr ? Diagnostics->SwapInRejectedByDistance : INDEX_NONE,
		Diagnostics != nullptr ? Diagnostics->AlreadyActiveBlocks : INDEX_NONE,
		Diagnostics != nullptr ? Diagnostics->SharedApplyFailures : INDEX_NONE,
		Diagnostics != nullptr ? Diagnostics->SwapActorSpawnFailures : INDEX_NONE,
		SwapScanInterval,
		MaxBlocksPerScan);
}
