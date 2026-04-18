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
	bool TryResolveHealthDefinitionForResolvedBlockHit(
		const FChunkWorldResolvedBlockHit& ResolvedHit,
		FGameplayTag& OutBlockTypeName,
		FBlockHealthDefinition& OutDefinition,
		FInstancedStruct& OutDefaultCustomDataPayload,
		FBlockHealthCustomData& OutDefaultCustomData)
	{
		OutBlockTypeName = FGameplayTag();
		OutDefinition = FBlockHealthDefinition();
		OutDefaultCustomDataPayload.Reset();
		OutDefaultCustomData = FBlockHealthCustomData();

		if (!ResolvedHit.bHasBlock || !IsValid(ResolvedHit.ChunkWorld) || !IsValid(ResolvedHit.BlockTypeSchemaComponent))
		{
			return false;
		}

		FInstancedStruct DefinitionStruct;
		if (!UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForResolvedBlockHit(ResolvedHit, OutBlockTypeName, DefinitionStruct))
		{
			return false;
		}

		if (!UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthDefinition(DefinitionStruct, OutDefinition))
		{
			return false;
		}

		FInstancedStruct DefaultCustomData;
		if (!UChunkWorldBlockHitBlueprintLibrary::TryGetBlockCustomDataForBlockTypeName(ResolvedHit.BlockTypeSchemaComponent, OutBlockTypeName, DefaultCustomData))
		{
			return false;
		}

		if (!UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthCustomData(DefaultCustomData, OutDefaultCustomData))
		{
			return false;
		}

		OutDefaultCustomDataPayload = DefaultCustomData;
		return true;
	}

	FBlockHealthCustomData* GetMutableBlockHealthCustomData(FInstancedStruct& Payload)
	{
		const UScriptStruct* PayloadStruct = Payload.GetScriptStruct();
		if (PayloadStruct == nullptr || !PayloadStruct->IsChildOf(FBlockHealthCustomData::StaticStruct()))
		{
			return nullptr;
		}

		void* PayloadMemory = Payload.GetMutableMemory();
		return PayloadMemory != nullptr ? static_cast<FBlockHealthCustomData*>(PayloadMemory) : nullptr;
	}

	void RequestDestructionActorPreload(const FBlockHealthDefinition& Definition)
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

		// Best-effort preload: destruction actors are one-shot presenters, so the shared health path only needs to
		// start the load early. The lethal destroy path still falls back to synchronous load if this has not finished.
		UAssetManager::GetStreamableManager().RequestAsyncLoad(DestructionClassPath, FStreamableDelegate());
	}
}

bool UChunkWorldBlockDamageBlueprintLibrary::TryResolveHealthSchemaForResolvedBlockHit(
	const FChunkWorldResolvedBlockHit& ResolvedHit,
	bool bAllowInitialization,
	FGameplayTag& OutBlockTypeName,
	FBlockHealthDefinition& OutDefinition,
	FInstancedStruct& OutCustomDataPayload,
	FBlockHealthCustomData& OutCustomData)
{
	OutBlockTypeName = FGameplayTag();
	OutDefinition = FBlockHealthDefinition();
	OutCustomDataPayload.Reset();
	OutCustomData = FBlockHealthCustomData();

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

	if (!UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthCustomData(OutCustomDataPayload, OutCustomData))
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

	return UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthDefinition(DefinitionStruct, OutDefinition);
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

bool UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit, int32 DamageAmount, FChunkWorldBlockHealthDeltaResult& OutResult)
{
	return TryApplyBlockHealthDeltaForResolvedBlockHit(ResolvedHit, DamageAmount, false, OutResult);
}

bool UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockHealingForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit, int32 HealingAmount, FChunkWorldBlockHealthDeltaResult& OutResult)
{
	return TryApplyBlockHealthDeltaForResolvedBlockHit(ResolvedHit, HealingAmount, true, OutResult);
}

bool UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockHealthDeltaForResolvedBlockHit(
	const FChunkWorldResolvedBlockHit& ResolvedHit,
	const int32 Amount,
	const bool bIsHealing,
	FChunkWorldBlockHealthDeltaResult& OutResult)
{
	OutResult = FChunkWorldBlockHealthDeltaResult();
	if (Amount <= 0 || !ResolvedHit.bHasBlock || !IsValid(ResolvedHit.ChunkWorld))
	{
		return false;
	}

	OutResult.bHitWasRepresentedBlock = true;
	OutResult.ChunkWorld = ResolvedHit.ChunkWorld;
	OutResult.BlockWorldPos = ResolvedHit.BlockWorldPos;
	OutResult.MaterialIndex = ResolvedHit.MaterialIndex;
	OutResult.MeshIndex = ResolvedHit.MeshIndex;

	FGameplayTag BlockTypeName;
	FBlockHealthDefinition Definition;
	FInstancedStruct CustomDataPayload;
	FBlockHealthCustomData CustomData;
	if (!TryResolveHealthSchemaForResolvedBlockHit(ResolvedHit, false, BlockTypeName, Definition, CustomDataPayload, CustomData))
	{
		return false;
	}

	// Project-specific change: treat uninitialized health custom data as authored full health and fold the first
	// health mutation into one schema write so initialization does not commit separately from the real change.
	const bool bHasInitializedCustomData = ResolvedHit.BlockTypeSchemaComponent != nullptr
		&& ResolvedHit.BlockTypeSchemaComponent->IsBlockCustomDataInitialized(ResolvedHit.BlockWorldPos);
	if (!bHasInitializedCustomData)
	{
		CustomData.Health = Definition.MaxHealth;
	}

	OutResult.bHasHealthSchema = true;
	OutResult.BlockTypeName = BlockTypeName;
	OutResult.bWasInvincible = Definition.bInvincible;
	OutResult.PreviousHealth = CustomData.Health;
	int32 NewHealth = CustomData.Health;
	if (bIsHealing)
	{
		const int32 ClampedHealth = FMath::Clamp(CustomData.Health, 0, Definition.MaxHealth);
		NewHealth = FMath::Min(Definition.MaxHealth, ClampedHealth + Amount);
		OutResult.NewHealth = NewHealth;
		OutResult.HealingApplied = NewHealth - ClampedHealth;
		OutResult.bAppliedHealing = OutResult.HealingApplied > 0;
		OutResult.bAppliedHealthChange = OutResult.bAppliedHealing;
	}
	else
	{
		OutResult.DamageApplied = Amount;
		RequestDestructionActorPreload(Definition);

		if (Definition.bInvincible)
		{
			OutResult.NewHealth = CustomData.Health;
			return true;
		}

		const int32 ClampedHealth = FMath::Max(0, CustomData.Health);
		NewHealth = FMath::Max(0, ClampedHealth - Amount);
		OutResult.NewHealth = NewHealth;
		OutResult.bAppliedDamage = NewHealth != ClampedHealth;
		OutResult.bAppliedHealthChange = OutResult.bAppliedDamage;
		OutResult.bDestroyed = NewHealth <= 0 && ClampedHealth > 0;

		if (!OutResult.bAppliedDamage)
		{
			// Reject repeated authoritative damage once the current runtime state is already settled at zero.
			// That avoids replaying destruction presentation when debug or stale interaction requests target a
			// block that has already been destroyed through the real authoritative path.
			OutResult.bDestroyed = false;
			return true;
		}
	}

	if (!OutResult.bAppliedHealthChange)
	{
		return true;
	}

	FBlockHealthCustomData* MutableHealthCustomData = GetMutableBlockHealthCustomData(CustomDataPayload);
	if (MutableHealthCustomData == nullptr)
	{
		return false;
	}

	MutableHealthCustomData->Health = NewHealth;
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
	FBlockHealthCustomData RuntimeCustomData;
	bOutHasStoredHealth = ResolvedHit.BlockTypeSchemaComponent != nullptr
		&& ResolvedHit.BlockTypeSchemaComponent->GetBlockCustomDataForBlockWorldPos(BlockWorldPos, RuntimeBlockTypeName, RuntimeCustomDataPayload)
		&& UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthCustomData(RuntimeCustomDataPayload, RuntimeCustomData);
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

	FBlockHealthDefinition Definition;
	FInstancedStruct DefaultCustomDataPayload;
	FBlockHealthCustomData DefaultCustomData;
	if (!TryResolveHealthDefinitionForResolvedBlockHit(ResolvedHit, OutBlockTypeName, Definition, DefaultCustomDataPayload, DefaultCustomData))
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

	FBlockHealthCustomData RuntimeCustomData;
	const bool bResolvedRuntimeHealthCustomData = bHasInitializedCustomData
		&& UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthCustomData(RuntimeCustomDataPayload, RuntimeCustomData);

	bOutHasRuntimeHealth = bResolvedRuntimeHealthCustomData;
	OutHealth = bResolvedRuntimeHealthCustomData ? RuntimeCustomData.Health : Definition.MaxHealth;
	if (bResolvedRuntimeHealthCustomData && RuntimeBlockTypeName.IsValid())
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
