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
#include "ChunkWorld/Spawn/ChunkWorldSpawnComponent.h"
#include "ChunkWorldStructs/ChunkWorldRuntimeStructs.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Engine/DataTable.h"
#include "FastNoise/FastNoiseEditor.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "Kismet/GameplayStatics.h"
#include "PorismDIMsWorldGeneratorExtension.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "UObject/UnrealType.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogChunkWorldExtended, Log, All);

namespace
{
	int32 ResolveSingleChunkFlatAxis(const UWorldGenDef* WorldGenDef)
	{
		if (WorldGenDef == nullptr)
		{
			return INDEX_NONE;
		}

		int32 SingleChunkCount = 0;
		int32 FlatAxis = INDEX_NONE;
		const EAxisBehavior AxisBehaviors[] = {
			WorldGenDef->AxisBehaviorX,
			WorldGenDef->AxisBehaviorY,
			WorldGenDef->AxisBehaviorZ
		};
		for (int32 Axis = 0; Axis < UE_ARRAY_COUNT(AxisBehaviors); ++Axis)
		{
			if (AxisBehaviors[Axis] == EAxisBehavior::SingleChunk)
			{
				++SingleChunkCount;
				FlatAxis = Axis;
			}
		}

		return SingleChunkCount == 1 ? FlatAxis : INDEX_NONE;
	}

	double GetVectorAxis(const FVector& Value, const int32 Axis)
	{
		return Axis == 0 ? Value.X : (Axis == 1 ? Value.Y : Value.Z);
	}

	double& GetMutableVectorAxis(FVector& Value, const int32 Axis)
	{
		return Axis == 0 ? Value.X : (Axis == 1 ? Value.Y : Value.Z);
	}

	int32 GetIntVectorAxis(const FIntVector& Value, const int32 Axis)
	{
		return Axis == 0 ? Value.X : (Axis == 1 ? Value.Y : Value.Z);
	}

	int32 CalculateLayerFlatAxisChunkSize(
		const UWorldGenDef* WorldGenDef,
		const FChunkDataParams& LayerParams,
		const int32 FlatAxis)
	{
		if (WorldGenDef == nullptr || FlatAxis == INDEX_NONE)
		{
			return 0;
		}

		const double BlockSize = FMath::Max(1.0, LayerParams.BlockSizeMulti * static_cast<double>(FMath::Max(1, WorldGenDef->BaseBlockSize)));
		const double ChunkBlockCount = static_cast<double>(FMath::Max(1, GetIntVectorAxis(WorldGenDef->ChunkBlockSize, FlatAxis)));
		const double ChunkSize = GetVectorAxis(LayerParams.ChunkSizeMulti, FlatAxis) * ChunkBlockCount * BlockSize;
		return FMath::Max(1, FMath::RoundToInt(ChunkSize));
	}

	bool CopyWorldChunkLayers(const UWorldGenDef* WorldGenDef, TArray<FChunkDataParams>& OutLayers, bool& bOutCopiedFromDataTable)
	{
		OutLayers.Reset();
		bOutCopiedFromDataTable = false;
		if (WorldGenDef == nullptr)
		{
			return false;
		}

		if (WorldGenDef->WorldChunksDT != nullptr && WorldGenDef->WorldChunksDT->GetRowMap().Num() > 0)
		{
			bOutCopiedFromDataTable = true;
			for (const TPair<FName, uint8*>& Row : WorldGenDef->WorldChunksDT->GetRowMap())
			{
				if (Row.Value != nullptr)
				{
					OutLayers.Add(*reinterpret_cast<const FChunkDataParams*>(Row.Value));
				}
			}
		}
		else
		{
			OutLayers = WorldGenDef->WorldChunks;
		}

		return !OutLayers.IsEmpty();
	}

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

	TArray<const FBiomeDualData*> GetWorldBiomeRows(const UWorldGenDef* WorldGenDef)
	{
		TArray<const FBiomeDualData*> Rows;
		if (WorldGenDef == nullptr)
		{
			return Rows;
		}

		if (WorldGenDef->WorldBiomesDT != nullptr && WorldGenDef->WorldBiomesDT->GetRowMap().Num() > 0)
		{
			for (const TPair<FName, uint8*>& Row : WorldGenDef->WorldBiomesDT->GetRowMap())
			{
				if (Row.Value != nullptr)
				{
					Rows.Add(reinterpret_cast<const FBiomeDualData*>(Row.Value));
				}
			}
			return Rows;
		}

		Rows.Reserve(WorldGenDef->WorldBiomes.Num());
		for (const FBiomeDualData& Row : WorldGenDef->WorldBiomes)
		{
			Rows.Add(&Row);
		}
		return Rows;
	}

	const UBiomeFastNoiseEditor* GetBiomeEditorDefaultObject(const TSubclassOf<UFastNoiseEditor>& EditorClass)
	{
		return EditorClass != nullptr
			? Cast<UBiomeFastNoiseEditor>(EditorClass->GetDefaultObject())
			: nullptr;
	}

	const UBiomeFastNoiseEditor* ResolveBiomeSlotEditor(const FBiomeDualData& Row, const EBiomeNoiseSlot DesiredSlot)
	{
		if (const UBiomeFastNoiseEditor* RuntimeGenA = Cast<UBiomeFastNoiseEditor>(Row.GenARun))
		{
			if (RuntimeGenA->NoiseSlot == DesiredSlot)
			{
				return RuntimeGenA;
			}
		}
		if (const UBiomeFastNoiseEditor* ClassGenA = GetBiomeEditorDefaultObject(Row.GenABP))
		{
			if (ClassGenA->NoiseSlot == DesiredSlot)
			{
				return ClassGenA;
			}
		}
		if (const UBiomeFastNoiseEditor* RuntimeDomain = Cast<UBiomeFastNoiseEditor>(Row.DomainRun))
		{
			if (RuntimeDomain->NoiseSlot == DesiredSlot)
			{
				return RuntimeDomain;
			}
		}
		if (const UBiomeFastNoiseEditor* ClassDomain = GetBiomeEditorDefaultObject(Row.DomainBP))
		{
			if (ClassDomain->NoiseSlot == DesiredSlot)
			{
				return ClassDomain;
			}
		}
		return nullptr;
	}

	const UBiomeFastNoiseEditor* FindFirstBiomeGenAEditor(const UWorldGenDef* WorldGenDef, int32& OutBiomeRowIndex)
	{
		OutBiomeRowIndex = INDEX_NONE;
		const TArray<const FBiomeDualData*> Rows = GetWorldBiomeRows(WorldGenDef);
		for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
		{
			const FBiomeDualData* Row = Rows[RowIndex];
			if (Row == nullptr || Row->BiomeHidden)
			{
				continue;
			}

			if (const UBiomeFastNoiseEditor* BiomeEditor = ResolveBiomeSlotEditor(*Row, EBiomeNoiseSlot::GenA))
			{
				OutBiomeRowIndex = RowIndex;
				return BiomeEditor;
			}
		}

		return nullptr;
	}

}

AChunkWorldExtended::AChunkWorldExtended()
{
	BlockTypeSchemaComponent = CreateDefaultSubobject<UBlockTypeSchemaComponent>(TEXT("BlockTypeSchemaComponent"));
	BlockFeedbackComponent = CreateDefaultSubobject<UChunkWorldBlockFeedbackComponent>(TEXT("BlockFeedbackComponent"));
	BlockSwapScannerComponent = CreateDefaultSubobject<UChunkWorldBlockSwapScannerComponent>(TEXT("BlockSwapScannerComponent"));
	BlockSwapComponent = CreateDefaultSubobject<UChunkWorldBlockSwapComponent>(TEXT("BlockSwapComponent"));
	LayoutRuntimeComponent = CreateDefaultSubobject<UChunkWorldLayoutRuntimeComponent>(TEXT("LayoutRuntimeComponent"));
	SpawnComponent = CreateDefaultSubobject<UChunkWorldSpawnComponent>(TEXT("SpawnComponent"));
	BlockFeedbackComponent->SetIsReplicated(true);
	SyncBlockTypeSchemaRegistry();
}

TArray<int> AChunkWorldExtended::GetBlockValuesByBlockWorldPosLevel(
	const TArray<FIntVector>& Positions, const CChunkData* DetailLevel,
	const ERessourceType ResourceType, const int CustomDataIndex)
{
	if (ResourceType != ERessourceType::MaterialIndex && ResourceType != ERessourceType::BiomeSwitchIndex)
	{
		return Super::GetBlockValuesByBlockWorldPosLevel(Positions, DetailLevel, ResourceType, CustomDataIndex);
	}
	TArray<int> Result;
	if (!IsRunning() || DetailLevel == nullptr) return Result;
	Result.Reserve(Positions.Num());
	TArray<FIntVector> ChunkPositions;
	for (int32 Begin = 0; Begin < Positions.Num();)
	{
		const FIntVector Chunk = BlockWorldPosToChunkGridPos(Positions[Begin], DetailLevel);
		int32 End = Begin + 1;
		while (End < Positions.Num() && BlockWorldPosToChunkGridPos(Positions[End], DetailLevel) == Chunk) ++End;
		ChunkPositions.Reset(End - Begin);
		ChunkPositions.Append(Positions.GetData() + Begin, End - Begin);
		Result.Append(Super::GetBlockValuesByBlockWorldPosLevel(ChunkPositions, DetailLevel, ResourceType, CustomDataIndex));
		Begin = End;
	}
	return Result;
}

SCacheKey AChunkWorldExtended::StartGenDTs()
{
	SyncBlockTypeSchemaRegistry();
	EnsureSchemaCustomDataCapacity();
	PrepareRuntimeWorldGenDefForGeneration();
	return Super::StartGenDTs();
}

bool AChunkWorldExtended::PrepareRuntimeWorldGenDefForGeneration()
{
	if (WorldGenDef == nullptr)
	{
		return false;
	}

	TArray<FChunkDataParams> RuntimeLayers;
	bool bCopiedFromDataTable = false;
	bool bChangedLayer = false;
	const int32 FlatAxis = ResolveSingleChunkFlatAxis(WorldGenDef);
	if (FlatAxis != INDEX_NONE
		&& CopyWorldChunkLayers(WorldGenDef, RuntimeLayers, bCopiedFromDataTable)
		&& RuntimeLayers.Num() >= 2)
	{
		const int32 LastLayerIndex = RuntimeLayers.Num() - 1;
		const int32 LastFlatChunkSize = CalculateLayerFlatAxisChunkSize(WorldGenDef, RuntimeLayers[LastLayerIndex], FlatAxis);
		if (LastFlatChunkSize > 0)
		{
			for (int32 LayerIndex = 0; LayerIndex < LastLayerIndex; ++LayerIndex)
			{
				FChunkDataParams& LayerParams = RuntimeLayers[LayerIndex];
				const int32 CurrentFlatChunkSize = CalculateLayerFlatAxisChunkSize(WorldGenDef, LayerParams, FlatAxis);
				if (CurrentFlatChunkSize == LastFlatChunkSize)
				{
					continue;
				}

				const double BaseBlockSize = static_cast<double>(FMath::Max(1, WorldGenDef->BaseBlockSize));
				const double BlockSize = FMath::Max(1.0, LayerParams.BlockSizeMulti * BaseBlockSize);
				const double ChunkBlockCount = static_cast<double>(FMath::Max(1, GetIntVectorAxis(WorldGenDef->ChunkBlockSize, FlatAxis)));
				const double DesiredChunkSizeMulti = static_cast<double>(LastFlatChunkSize) / (ChunkBlockCount * BlockSize);
				const double PreviousChunkSizeMulti = GetVectorAxis(LayerParams.ChunkSizeMulti, FlatAxis);
				GetMutableVectorAxis(LayerParams.ChunkSizeMulti, FlatAxis) = DesiredChunkSizeMulti;
				bChangedLayer = true;

				UE_LOG(
					LogChunkWorldExtended,
					Verbose,
					TEXT("Chunk world '%s' normalized 2D flat-axis layer span before generation: Layer=%d Axis=%d ChunkSize=%d -> %d ChunkSizeMulti=%.6f -> %.6f Source=%s WorldGenDef=%s."),
					*GetNameSafe(this),
					LayerIndex,
					FlatAxis,
					CurrentFlatChunkSize,
					LastFlatChunkSize,
					PreviousChunkSizeMulti,
					DesiredChunkSizeMulti,
					bCopiedFromDataTable ? TEXT("WorldChunksDT") : TEXT("InlineWorldChunks"),
					*GetNameSafe(WorldGenDef));
			}
		}
	}

	int32 FirstBiomeRowIndex = INDEX_NONE;
	const bool bUsesBiomeStrategy = FindFirstBiomeGenAEditor(WorldGenDef, FirstBiomeRowIndex) != nullptr;
	const bool bNeedsExplicitZeroWorldGen = bUsesBiomeStrategy
		&& WorldGenDef->WorldGen.IsEmpty()
		&& WorldGenDef->WorldGenBP == nullptr
		&& WorldGenDef->WorldGenRun == nullptr;
	if (!bChangedLayer && !bNeedsExplicitZeroWorldGen)
	{
		return false;
	}

	// Use a runtime copy so extension fixes do not mutate authored Porism assets.
	if (UWorldGenDef* RuntimeWorldGenDef = DuplicateObject<UWorldGenDef>(WorldGenDef, this))
	{
		if (bChangedLayer)
		{
			RuntimeWorldGenDef->WorldChunks = MoveTemp(RuntimeLayers);
			RuntimeWorldGenDef->WorldChunksDT = nullptr;
		}
		if (bNeedsExplicitZeroWorldGen)
		{
			RuntimeWorldGenDef->WorldGenRun = NewObject<UFastNoiseEditor>(
				RuntimeWorldGenDef,
				UFastNoiseEditor::StaticClass(),
				NAME_None,
				RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
			UE_LOG(
				LogChunkWorldExtended,
				Verbose,
				TEXT("Chunk world '%s' installed explicit zero WorldGen on runtime copy because authored WorldGen was empty. This avoids Porism's constant-positive fallback from shifting biome density surfaces downward. WorldGenDef=%s."),
				*GetNameSafe(this),
				*GetNameSafe(RuntimeWorldGenDef));
		}
		WorldGenDef = RuntimeWorldGenDef;
		return true;
	}

	return false;
}

void AChunkWorldExtended::ProcessEvent(UFunction* Function, void* Parms)
{
	static const FName CreateEvent = GET_FUNCTION_NAME_CHECKED(AChunkWorldBase, OnChunkCreate);
	static const FName UpdateEvent = GET_FUNCTION_NAME_CHECKED(AChunkWorldBase, OnChunkUpdate);
	static const FName DeleteEvent = GET_FUNCTION_NAME_CHECKED(AChunkWorldBase, OnChunkDelete);
	const FName EventName = Function != nullptr ? Function->GetFName() : NAME_None;
	if (Parms != nullptr && LayoutRuntimeComponent != nullptr
		&& (EventName == CreateEvent || EventName == UpdateEvent || EventName == DeleteEvent))
	{
		const FStructProperty* Position = FindFProperty<FStructProperty>(Function, TEXT("chunkBlockWorldPos"));
		const FIntProperty* Detail = FindFProperty<FIntProperty>(Function, TEXT("detailLevel"));
		if (Position != nullptr && Detail != nullptr)
		{
			const FIntVector Origin = *Position->ContainerPtrToValuePtr<FIntVector>(Parms);
			const int32 Level = Detail->GetPropertyValue_InContainer(Parms);
			if (EventName == DeleteEvent)
			{
				LayoutRuntimeComponent->QueueObservedUnloadedChunk(Origin, Level);
			}
			else
			{
				HandleObservedChunkLifecycle(Origin, Level, EventName == CreateEvent
					? EChunkWorldChunkLifecycleEventType::Created : EChunkWorldChunkLifecycleEventType::Updated);
			}
		}
	}
	Super::ProcessEvent(Function, Parms);
}

void AChunkWorldExtended::StopGen()
{
	Super::StopGen();
	if (LayoutRuntimeComponent != nullptr)
	{
		LayoutRuntimeComponent->ResetResolvedLayoutRecords(true);
	}
}

void AChunkWorldExtended::StartGen()
{
	SyncBlockTypeSchemaRegistry();
	ResetWorldReadyStateTracking();
	if (!bStartingForCachedLayoutApply && LayoutRuntimeComponent != nullptr)
	{
		LayoutRuntimeComponent->ResetResolvedLayoutRecords(true);
	}
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

bool AChunkWorldExtended::RestartGenerationPreservingLayoutRecords()
{
	check(!IsRunning());
	TGuardValue<bool> PreserveCachedApply(bStartingForCachedLayoutApply, true);
	if (LayoutRuntimeComponent != nullptr)
	{
		// Restarted generation must rebuild its loaded-chunk picture from fresh
		// lifecycle events instead of trusting the previous run's observations.
		LayoutRuntimeComponent->ResetObservedChunkLoadStateForGenerationRestart();
	}
	if (bForceNextRestartGenerationFailureForTesting)
	{
		bForceNextRestartGenerationFailureForTesting = false;
		return false;
	}
	StartGen();
	return IsRunning();
}

void AChunkWorldExtended::SetForceNextRestartGenerationFailureForTesting(const bool bInForceFailure)
{
	bForceNextRestartGenerationFailureForTesting = bInForceFailure;
}

bool AChunkWorldExtended::TryGetEditorViewportCameraLocation(FVector& OutCameraLocation) const
{
#if WITH_EDITOR
	const UWorld* const OwningWorld = GetWorld();
	const auto IsEligibleViewport = [OwningWorld](const FLevelEditorViewportClient* Client)
	{
		return OwningWorld != nullptr && Client != nullptr && Client->IsPerspective()
			&& Client->GetWorld() == OwningWorld;
	};
	FLevelEditorViewportClient* EditorViewportClient = GCurrentLevelEditingViewportClient;
	if (!IsEligibleViewport(EditorViewportClient))
	{
		EditorViewportClient = nullptr;
	}
	if (EditorViewportClient == nullptr && GEditor != nullptr)
	{
		for (FLevelEditorViewportClient* CandidateClient : GEditor->GetLevelViewportClients())
		{
			if (IsEligibleViewport(CandidateClient))
			{
				EditorViewportClient = CandidateClient;
				break;
			}
		}
	}

	if (EditorViewportClient == nullptr)
	{
		OutCameraLocation = FVector::ZeroVector;
		return false;
	}

	OutCameraLocation = EditorViewportClient->GetViewTransform().GetLocation();
	return true;
#else
	OutCameraLocation = FVector::ZeroVector;
	return false;
#endif
}

void AChunkWorldExtended::Tick(float DeltaTime)
{
	const bool bNeedsWalkerReadyUpdates = bStartupWorldReadyTrackingActive || !RuntimeWalkerReadyStates.IsEmpty();
	if (!bNeedsWalkerReadyUpdates)
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
	for (auto It = RuntimeWalkerReadyStates.CreateIterator(); It; ++It)
	{
		if (!It.Value().Walker.IsValid() || !HasRegisteredChunkWorldWalker(It.Value().Walker.Get()))
		{
			It.RemoveCurrent();
		}
	}
	if (bStartupWorldReadyTrackingActive)
	{
		PruneWalkerReadyStates();
		RefreshWorldReadyState();
	}
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
	RuntimeWalkerReadyStates.Reset();
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

bool AChunkWorldExtended::StartRuntimeReadinessTracking(UObject* Walker, const FGuid SessionId)
{
	if (!IsValid(Walker) || !SessionId.IsValid() || !HasRegisteredChunkWorldWalker(Walker))
	{
		return false;
	}

	FChunkWorldRuntimeWalkerReadyState& RuntimeState = RuntimeWalkerReadyStates.FindOrAdd(FObjectKey(Walker));
	const bool bSessionChanged = RuntimeState.SessionId != SessionId;
	RuntimeState.Walker = Walker;
	RuntimeState.SessionId = SessionId;
	if (bSessionChanged)
	{
		RuntimeState.bReadyBroadcast = false;
	}
	return true;
}

void AChunkWorldExtended::StopRuntimeReadinessTracking(UObject* Walker, const FGuid SessionId)
{
	if (!Walker)
	{
		return;
	}

	const FObjectKey WalkerKey(Walker);
	const FChunkWorldRuntimeWalkerReadyState* RuntimeState = RuntimeWalkerReadyStates.Find(WalkerKey);
	if (RuntimeState != nullptr && RuntimeState->SessionId == SessionId)
	{
		RuntimeWalkerReadyStates.Remove(WalkerKey);
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

UChunkWorldLayoutRuntimeComponent* AChunkWorldExtended::GetLayoutRuntimeComponent() const
{
	return LayoutRuntimeComponent;
}

UChunkWorldSpawnComponent* AChunkWorldExtended::GetSpawnComponent() const
{
	return SpawnComponent;
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

TArray<FInstanceMeshInfos> AChunkWorldExtended::OnChunkCreateEdit_Implementation(
	FIntVector chunkBlockWorldPos,
	int detailLevel,
	TArray<FInstanceMeshInfos>& newMeshInstances)
{
	return Super::OnChunkCreateEdit_Implementation(chunkBlockWorldPos, detailLevel, newMeshInstances);
}

TArray<FInstanceMeshInfos> AChunkWorldExtended::OnChunkUpdateEdit_Implementation(
	FIntVector chunkBlockWorldPos,
	int detailLevel,
	TArray<FInstanceMeshInfos>& newMeshInstances)
{
	return Super::OnChunkUpdateEdit_Implementation(chunkBlockWorldPos, detailLevel, newMeshInstances);
}

void AChunkWorldExtended::PostLoad()
{
	Super::PostLoad();
	SyncBlockTypeSchemaRegistry();
}

#if WITH_EDITOR
void AChunkWorldExtended::PreEditChange(FProperty* PropertyThatWillChange)
{
	const FName PropertyName = PropertyThatWillChange != nullptr ? PropertyThatWillChange->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AChunkWorldExtended, ShowDebugData)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(AChunkWorldExtended, bDetailedDiagnostics))
	{
		// AActor pre-edit unregisters every component. These non-reconstructing edits
		// must preserve registration, including native terrain tick and layout HUD delegates.
		UObject::PreEditChange(PropertyThatWillChange);
		return;
	}
	Super::PreEditChange(PropertyThatWillChange);
}

void AChunkWorldExtended::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FProperty* const Property = PropertyChangedEvent.Property;
	const UClass* const PropertyOwner = Property != nullptr ? Property->GetOwnerClass() : nullptr;
	const UClass* const MemberOwner = PropertyChangedEvent.MemberProperty != nullptr
		? PropertyChangedEvent.MemberProperty->GetOwnerClass() : nullptr;
	const bool bLayoutComponentEdit = (PropertyOwner != nullptr
		&& PropertyOwner->IsChildOf(UChunkWorldLayoutRuntimeComponent::StaticClass()))
		|| (MemberOwner != nullptr && MemberOwner->IsChildOf(UChunkWorldLayoutRuntimeComponent::StaticClass()))
		|| (PropertyChangedEvent.MemberProperty != nullptr
			&& PropertyChangedEvent.MemberProperty != Property
			&& PropertyChangedEvent.MemberProperty->GetFName()
				== GET_MEMBER_NAME_CHECKED(AChunkWorldExtended, LayoutRuntimeComponent));
	// The native superclass handler only restarts terrain; it does not call AActor editor
	// bookkeeping. Component layout edits are consumed by the component, not terrain generation.
	const FName PropertyName = Property != nullptr ? Property->GetFName() : NAME_None;
	const bool bStatsEdit = PropertyName == GET_MEMBER_NAME_CHECKED(AChunkWorldExtended, ShowDebugData)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(AChunkWorldExtended, bDetailedDiagnostics);
	if (bStatsEdit && LayoutRuntimeComponent != nullptr)
	{
		// Retain native ShowDebugData as the single serialized HUD switch.
		LayoutRuntimeComponent->SetDebugGenerationStats(ShowDebugData);
	}
	if (bStatsEdit)
	{
		// Preserve property-change broadcasts without actor construction or terrain restart.
		UObject::PostEditChangeProperty(PropertyChangedEvent);
	}
	else
	{
		if (!bLayoutComponentEdit)
		{
			Super::PostEditChangeProperty(PropertyChangedEvent);
		}
		// Native post-edit restarts generation but never pairs AActor's pre-edit
		// unregistration. Restore ticks/HUD without rerunning construction scripts:
		// reconstruction discards the native generator's runtime WorldGenDef instance.
		if (!IsTemplate())
		{
			RegisterAllComponents();
		}
		UObject::PostEditChangeProperty(PropertyChangedEvent);
	}
	SyncBlockTypeSchemaRegistry();
}
#endif

void AChunkWorldExtended::SyncBlockTypeSchemaRegistry()
{
	if (BlockTypeSchemaComponent == nullptr)
	{
		return;
	}

	BlockTypeSchemaComponent->SetBlockTypeSchemaRegistry(BlockTypeSchemaRegistry);
}

void AChunkWorldExtended::HandlePendingWalkerInfo(UObject* Walker, const FChunkWorldWalkerInfo& Info)
{
	if (!Walker)
	{
		return;
	}

	if (bStartupWorldReadyTrackingActive)
	{
		FChunkWorldWalkerReadyState& ReadyState = WalkerReadyStates.FindOrAdd(FObjectKey(Walker));
		ReadyState.Walker = Walker;
		ReadyState.bHasReceivedReadyInfo = true;
		ReadyState.bIsReady = IsWalkerReadyForWorld(Info);
		ReadyState.ReadyDetailLevel = Info.DetailLevel;
		ReadyState.LastWalkerInfo = Info;
		RefreshWorldReadyState();
	}

	RefreshRuntimeWalkerReadyState(Walker, Info);
}

void AChunkWorldExtended::RefreshRuntimeWalkerReadyState(UObject* Walker, const FChunkWorldWalkerInfo& Info)
{
	const FObjectKey WalkerKey(Walker);
	FChunkWorldRuntimeWalkerReadyState* RuntimeState = RuntimeWalkerReadyStates.Find(WalkerKey);
	if (RuntimeState == nullptr)
	{
		return;
	}

	if (!RuntimeState->Walker.IsValid() || !HasRegisteredChunkWorldWalker(Walker))
	{
		RuntimeWalkerReadyStates.Remove(WalkerKey);
		return;
	}

	if (!RuntimeState->bReadyBroadcast && IsWalkerReadyForWorld(Info))
	{
		RuntimeState->bReadyBroadcast = true;
		OnRuntimeWalkerReady.Broadcast(this, Walker, RuntimeState->SessionId);
	}
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

void AChunkWorldExtended::HandleObservedChunkLifecycle(
	const FIntVector& ChunkBlockWorldPos,
	const int32 DetailLevel,
	const EChunkWorldChunkLifecycleEventType EventType)
{
	FChunkWorldObservedChunkLifecycleEvent Event;
	Event.ChunkBlockWorldPos = ChunkBlockWorldPos;
	Event.DetailLevel = DetailLevel;
	Event.EventType = EventType;
	Event.bServerAuthority = HasAuthority();
	const int32 FinestDetailLevel = GetChunkLayerCount() - 1;
	Event.bFinestDetail = FinestDetailLevel >= 0 && DetailLevel >= FinestDetailLevel;
	Event.bGeneratedForFirstTime = Event.bServerAuthority
		&& Event.bFinestDetail
		&& EventType == EChunkWorldChunkLifecycleEventType::Created;

	if (LayoutRuntimeComponent != nullptr)
	{
		LayoutRuntimeComponent->QueueObservedChunkLifecycle(Event);
	}

	if (Event.bServerAuthority)
	{
		OnServerObservedChunkLifecycle.Broadcast(this, Event);
	}
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
					// Preserve both damage and healing deltas from the settled client-only
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

	if (BlockSwapComponent != nullptr)
	{
		// Layout/runtime template stamps can change represented block presentation
		// without emitting any mesh writes, so keep active swap visibility aligned
		// after pure block-material updates as well.
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
	// Destruction presentation delivery is authored per block definition, so force the spawn
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
