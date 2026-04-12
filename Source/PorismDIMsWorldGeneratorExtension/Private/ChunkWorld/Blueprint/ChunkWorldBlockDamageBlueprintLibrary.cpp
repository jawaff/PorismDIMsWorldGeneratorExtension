// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Blueprint/ChunkWorldBlockDamageBlueprintLibrary.h"

#include "Block/BlockTypeSchemaBlueprintLibrary.h"
#include "ChunkWorld/ChunkWorld.h"
#include "ChunkWorldStructs/ChunkWorldEnums.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockHitBlueprintLibrary.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockFeedbackComponent.h"
#include "Engine/AssetManager.h"

namespace
{
	bool TryResolveDamageDefinitionForResolvedBlockHit(
		const FChunkWorldResolvedBlockHit& ResolvedHit,
		FGameplayTag& OutBlockTypeName,
		FBlockDamageDefinition& OutDefinition,
		FInstancedStruct& OutDefaultCustomDataPayload,
		FBlockDamageCustomData& OutDefaultCustomData)
	{
		OutBlockTypeName = FGameplayTag();
		OutDefinition = FBlockDamageDefinition();
		OutDefaultCustomDataPayload.Reset();
		OutDefaultCustomData = FBlockDamageCustomData();

		if (!ResolvedHit.bHasBlock || !IsValid(ResolvedHit.ChunkWorld) || !IsValid(ResolvedHit.BlockTypeSchemaComponent))
		{
			return false;
		}

		FInstancedStruct DefinitionStruct;
		if (!UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForResolvedBlockHit(ResolvedHit, OutBlockTypeName, DefinitionStruct))
		{
			return false;
		}

		if (!UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageDefinition(DefinitionStruct, OutDefinition))
		{
			return false;
		}

		FInstancedStruct DefaultCustomData;
		if (!UChunkWorldBlockHitBlueprintLibrary::TryGetBlockCustomDataForBlockTypeName(ResolvedHit.BlockTypeSchemaComponent, OutBlockTypeName, DefaultCustomData))
		{
			return false;
		}

		if (!UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageCustomData(DefaultCustomData, OutDefaultCustomData))
		{
			return false;
		}

		OutDefaultCustomDataPayload = DefaultCustomData;
		return true;
	}

	FBlockDamageCustomData* GetMutableBlockDamageCustomData(FInstancedStruct& Payload)
	{
		const UScriptStruct* PayloadStruct = Payload.GetScriptStruct();
		if (PayloadStruct == nullptr || !PayloadStruct->IsChildOf(FBlockDamageCustomData::StaticStruct()))
		{
			return nullptr;
		}

		void* PayloadMemory = Payload.GetMutableMemory();
		return PayloadMemory != nullptr ? static_cast<FBlockDamageCustomData*>(PayloadMemory) : nullptr;
	}

	void RequestDestructionActorPreload(const FBlockDamageDefinition& Definition)
	{
		if (Definition.DestructionActorClass.IsNull() || Definition.DestructionActorClass.IsValid())
		{
			return;
		}

		const FSoftObjectPath DestructionClassPath = Definition.DestructionActorClass.ToSoftObjectPath();
		if (!DestructionClassPath.IsValid())
		{
			return;
		}

		// Best-effort preload: destruction actors are one-shot presenters, so the shared damage path only needs to
		// start the load early. The lethal destroy path still falls back to synchronous load if this has not finished.
		UAssetManager::GetStreamableManager().RequestAsyncLoad(DestructionClassPath, FStreamableDelegate());
	}
}

bool UChunkWorldBlockDamageBlueprintLibrary::TryResolveDamageSchemaForResolvedBlockHit(
	const FChunkWorldResolvedBlockHit& ResolvedHit,
	bool bAllowInitialization,
	FGameplayTag& OutBlockTypeName,
	FBlockDamageDefinition& OutDefinition,
	FInstancedStruct& OutCustomDataPayload,
	FBlockDamageCustomData& OutCustomData)
{
	OutBlockTypeName = FGameplayTag();
	OutDefinition = FBlockDamageDefinition();
	OutCustomDataPayload.Reset();
	OutCustomData = FBlockDamageCustomData();

	if (!ResolvedHit.bHasBlock || !IsValid(ResolvedHit.BlockTypeSchemaComponent))
	{
		return false;
	}

	if (bAllowInitialization)
	{
		(void)ResolvedHit.BlockTypeSchemaComponent->InitializeBlockCustomDataForRepresentationIndexes(
			ResolvedHit.BlockWorldPos,
			ResolvedHit.MaterialIndex,
			ResolvedHit.MeshIndex);
	}

	if (!ResolvedHit.BlockTypeSchemaComponent->GetBlockCustomDataForRepresentationIndexes(
		ResolvedHit.BlockWorldPos,
		ResolvedHit.MaterialIndex,
		ResolvedHit.MeshIndex,
		OutBlockTypeName,
		OutCustomDataPayload))
	{
		// These are all fallbacks
		FInstancedStruct DefinitionStruct;
		if (!UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForResolvedBlockHit(ResolvedHit, OutBlockTypeName, DefinitionStruct))
		{
			return false;
		}

		if (!UChunkWorldBlockHitBlueprintLibrary::TryGetBlockCustomDataForBlockTypeName(ResolvedHit.BlockTypeSchemaComponent, OutBlockTypeName, OutCustomDataPayload))
		{
			return false;
		}
	}

	if (!UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageCustomData(OutCustomDataPayload, OutCustomData))
	{
		return false;
	}

	if (!OutBlockTypeName.IsValid())
	{
		return false;
	}

	FInstancedStruct DefinitionStruct;
	if (!UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForResolvedBlockHit(ResolvedHit, OutBlockTypeName, DefinitionStruct))
	{
		return false;
	}

	return UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageDefinition(DefinitionStruct, OutDefinition);
}

bool UChunkWorldBlockDamageBlueprintLibrary::TryBuildAuthorityPayloadFromResolvedBlockHit(
	const FChunkWorldResolvedBlockHit& ResolvedHit,
	FChunkWorldBlockHitAuthorityPayload& OutPayload)
{
	OutPayload = FChunkWorldBlockHitAuthorityPayload();
	if (!ResolvedHit.bHasBlock || !IsValid(ResolvedHit.ChunkWorld))
	{
		return false;
	}

	OutPayload.ChunkWorld = ResolvedHit.ChunkWorld;
	OutPayload.BlockWorldPos = ResolvedHit.BlockWorldPos;
	OutPayload.bHasBlock = true;
	return true;
}

bool UChunkWorldBlockDamageBlueprintLibrary::TryResolveBlockHitContextFromAuthorityPayload(
	const FChunkWorldBlockHitAuthorityPayload& Payload,
	FChunkWorldResolvedBlockHit& OutResolvedHit)
{
	OutResolvedHit = FChunkWorldResolvedBlockHit();
	if (!Payload.bHasBlock || !IsValid(Payload.ChunkWorld))
	{
		return false;
	}

	// Authoritative callers should rebuild a fresh resolved hit from minimal block identity instead of
	// trusting client-side schema/component/material context directly.
	return UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromBlockWorldPos(
		Payload.ChunkWorld,
		Payload.BlockWorldPos,
		OutResolvedHit);
}

bool UChunkWorldBlockDamageBlueprintLibrary::TryBroadcastHitFeedbackForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit)
{
	if (!ResolvedHit.bHasBlock || !IsValid(ResolvedHit.ChunkWorld))
	{
		return false;
	}

	UChunkWorldBlockFeedbackComponent* FeedbackComponent = nullptr;
	if (!UChunkWorldBlockHitBlueprintLibrary::GetBlockFeedbackComponentFromChunkWorld(ResolvedHit.ChunkWorld, FeedbackComponent))
	{
		return false;
	}

	return FeedbackComponent->BroadcastAuthoritativeHitFeedback(ResolvedHit);
}

bool UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit, int32 DamageAmount, FChunkWorldBlockDamageResult& OutResult)
{
	OutResult = FChunkWorldBlockDamageResult();
	if (DamageAmount <= 0 || !ResolvedHit.bHasBlock || !IsValid(ResolvedHit.ChunkWorld))
	{
		return false;
	}

	OutResult.bHitWasRepresentedBlock = true;
	OutResult.ChunkWorld = ResolvedHit.ChunkWorld;
	OutResult.BlockWorldPos = ResolvedHit.BlockWorldPos;
	OutResult.MaterialIndex = ResolvedHit.MaterialIndex;
	OutResult.MeshIndex = ResolvedHit.MeshIndex;

	FGameplayTag BlockTypeName;
	FBlockDamageDefinition Definition;
	FInstancedStruct CustomDataPayload;
	FBlockDamageCustomData CustomData;
	if (!TryResolveDamageSchemaForResolvedBlockHit(ResolvedHit, false, BlockTypeName, Definition, CustomDataPayload, CustomData))
	{
		return false;
	}

	// Project-side fix: treat uninitialized damage custom data as authored full health and fold initialization plus
	// damage into one schema write so one hit does not generate a separate initialization commit first.
	const bool bHasInitializedCustomData = ResolvedHit.BlockTypeSchemaComponent != nullptr
		&& ResolvedHit.BlockTypeSchemaComponent->IsBlockCustomDataInitialized(ResolvedHit.BlockWorldPos);
	if (!bHasInitializedCustomData)
	{
		CustomData.Health = Definition.MaxHealth;
	}

	OutResult.bHasDamageSchema = true;
	OutResult.BlockTypeName = BlockTypeName;
	OutResult.bWasInvincible = Definition.bInvincible;
	OutResult.PreviousHealth = CustomData.Health;
	OutResult.DamageApplied = DamageAmount;
	RequestDestructionActorPreload(Definition);

	if (Definition.bInvincible)
	{
		OutResult.NewHealth = CustomData.Health;
		return true;
	}

	const int32 ClampedHealth = FMath::Max(0, CustomData.Health);
	const int32 NewHealth = FMath::Max(0, ClampedHealth - DamageAmount);
	OutResult.NewHealth = NewHealth;
	OutResult.bAppliedDamage = NewHealth != ClampedHealth;
	OutResult.bDestroyed = NewHealth <= 0 && ClampedHealth > 0;

	if (!OutResult.bAppliedDamage)
	{
		// Reject repeated authoritative damage once the current runtime state is already settled at zero.
		// That avoids replaying destruction presentation when debug or stale interaction requests target a
		// block that has already been destroyed through the real authoritative path.
		OutResult.bDestroyed = false;
		return true;
	}

	FBlockDamageCustomData* MutableDamageCustomData = GetMutableBlockDamageCustomData(CustomDataPayload);
	if (MutableDamageCustomData == nullptr)
	{
		return false;
	}

	MutableDamageCustomData->Health = NewHealth;
	if (!ResolvedHit.BlockTypeSchemaComponent->SetBlockCustomDataForBlockWorldPos(ResolvedHit.BlockWorldPos, CustomDataPayload))
	{
		return false;
	}

	return true;
}

bool UChunkWorldBlockDamageBlueprintLibrary::TryGetCurrentBlockHealthStateForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit, int32& OutHealth, bool& bOutInvincible, FGameplayTag& OutBlockTypeName)
{
	OutHealth = 0;
	bOutInvincible = false;
	OutBlockTypeName = FGameplayTag();

	int32 MaxHealth = 0;
	bool bHasRuntimeHealth = false;
	if (!TryGetRuntimeBlockHealthStateForResolvedBlockHit(ResolvedHit, OutHealth, MaxHealth, bOutInvincible, bHasRuntimeHealth, OutBlockTypeName))
	{
		return false;
	}

	return true;
}

bool UChunkWorldBlockDamageBlueprintLibrary::TryGetBlockHealthStateForBlockWorldPos(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, int32& OutHealth, int32& OutMaxHealth, bool& bOutInvincible, bool& bOutHasStoredHealth, FGameplayTag& OutBlockTypeName)
{
	OutHealth = 0;
	OutMaxHealth = 0;
	bOutInvincible = false;
	bOutHasStoredHealth = false;
	OutBlockTypeName = FGameplayTag();

	FChunkWorldResolvedBlockHit ResolvedHit;
	if (!UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromBlockWorldPos(ChunkWorld, BlockWorldPos, ResolvedHit) || !ResolvedHit.bHasBlock)
	{
		return false;
	}

	if (!TryGetRuntimeBlockHealthStateForResolvedBlockHit(ResolvedHit, OutHealth, OutMaxHealth, bOutInvincible, bOutHasStoredHealth, OutBlockTypeName))
	{
		return false;
	}

	FGameplayTag RuntimeBlockTypeName;
	FInstancedStruct RuntimeCustomDataPayload;
	FBlockDamageCustomData RuntimeCustomData;
	bOutHasStoredHealth = ResolvedHit.BlockTypeSchemaComponent != nullptr
		&& ResolvedHit.BlockTypeSchemaComponent->GetBlockCustomDataForBlockWorldPos(BlockWorldPos, RuntimeBlockTypeName, RuntimeCustomDataPayload)
		&& UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageCustomData(RuntimeCustomDataPayload, RuntimeCustomData);
	return true;
}

bool UChunkWorldBlockDamageBlueprintLibrary::TryGetRuntimeBlockHealthStateForResolvedBlockHit(
	const FChunkWorldResolvedBlockHit& ResolvedHit,
	int32& OutHealth,
	int32& OutMaxHealth,
	bool& bOutInvincible,
	bool& bOutHasRuntimeHealth,
	FGameplayTag& OutBlockTypeName)
{
	OutHealth = 0;
	OutMaxHealth = 0;
	bOutInvincible = false;
	bOutHasRuntimeHealth = false;
	OutBlockTypeName = FGameplayTag();

	FBlockDamageDefinition Definition;
	FInstancedStruct DefaultCustomDataPayload;
	FBlockDamageCustomData DefaultCustomData;
	if (!TryResolveDamageDefinitionForResolvedBlockHit(ResolvedHit, OutBlockTypeName, Definition, DefaultCustomDataPayload, DefaultCustomData))
	{
		return false;
	}

	OutMaxHealth = Definition.MaxHealth;
	bOutInvincible = Definition.bInvincible;

	const UBlockTypeSchemaComponent* BlockTypeSchemaComponent = ResolvedHit.BlockTypeSchemaComponent;
	if (!IsValid(BlockTypeSchemaComponent))
	{
		return false;
	}

	FGameplayTag RuntimeBlockTypeName;
	FInstancedStruct RuntimeCustomDataPayload;
	const bool bHasInitializedCustomData = BlockTypeSchemaComponent->GetBlockCustomDataForBlockWorldPos(
		ResolvedHit.BlockWorldPos,
		RuntimeBlockTypeName,
		RuntimeCustomDataPayload);

	FBlockDamageCustomData RuntimeCustomData;
	const bool bResolvedRuntimeDamageCustomData = bHasInitializedCustomData
		&& UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageCustomData(RuntimeCustomDataPayload, RuntimeCustomData);

	bOutHasRuntimeHealth = bResolvedRuntimeDamageCustomData;
	OutHealth = bResolvedRuntimeDamageCustomData ? RuntimeCustomData.Health : Definition.MaxHealth;
	if (bResolvedRuntimeDamageCustomData && RuntimeBlockTypeName.IsValid())
	{
		OutBlockTypeName = RuntimeBlockTypeName;
	}
	else if (!OutBlockTypeName.IsValid())
	{
		OutBlockTypeName = RuntimeBlockTypeName;
	}
	return true;
}

bool UChunkWorldBlockDamageBlueprintLibrary::TryGetRuntimeBlockHealthStateForBlockWorldPos(
	AChunkWorld* ChunkWorld,
	const FIntVector& BlockWorldPos,
	int32& OutHealth,
	int32& OutMaxHealth,
	bool& bOutInvincible,
	bool& bOutHasRuntimeHealth,
	FGameplayTag& OutBlockTypeName)
{
	OutHealth = 0;
	OutMaxHealth = 0;
	bOutInvincible = false;
	bOutHasRuntimeHealth = false;
	OutBlockTypeName = FGameplayTag();

	FChunkWorldResolvedBlockHit ResolvedHit;
	if (!UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromBlockWorldPos(ChunkWorld, BlockWorldPos, ResolvedHit) || !ResolvedHit.bHasBlock)
	{
		return false;
	}

	return TryGetRuntimeBlockHealthStateForResolvedBlockHit(
		ResolvedHit,
		OutHealth,
		OutMaxHealth,
		bOutInvincible,
		bOutHasRuntimeHealth,
		OutBlockTypeName);
}
