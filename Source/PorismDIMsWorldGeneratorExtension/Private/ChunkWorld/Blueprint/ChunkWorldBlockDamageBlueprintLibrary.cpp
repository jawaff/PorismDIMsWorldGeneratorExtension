// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Blueprint/ChunkWorldBlockDamageBlueprintLibrary.h"

#include "Block/BlockTypeSchemaBlueprintLibrary.h"
#include "ChunkWorld/ChunkWorld.h"
#include "ChunkWorldStructs/ChunkWorldEnums.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "ChunkWorld/Blueprint/ChunkWorldHitBlueprintLibrary.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockFeedbackComponent.h"

bool UChunkWorldBlockDamageBlueprintLibrary::TryResolveDamageSchemaForResolvedBlockHit(
	const FChunkWorldResolvedBlockHit& ResolvedHit,
	bool bAllowMaterialization,
	FGameplayTag& OutBlockTypeName,
	FBlockDamageDefinition& OutDefinition,
	FBlockDamageCustomData& OutCustomData)
{
	OutBlockTypeName = FGameplayTag();
	OutDefinition = FBlockDamageDefinition();
	OutCustomData = FBlockDamageCustomData();

	if (!ResolvedHit.bHasBlock || !IsValid(ResolvedHit.BlockTypeSchemaComponent))
	{
		return false;
	}

	if (bAllowMaterialization)
	{
		(void)ResolvedHit.BlockTypeSchemaComponent->InitializeBlockCustomData(ResolvedHit.BlockWorldPos);
	}

	if (!ResolvedHit.BlockTypeSchemaComponent->GetBlockCustomDataForBlockWorldPos(ResolvedHit.BlockWorldPos, OutBlockTypeName, OutCustomData))
	{
		// These are all fallbacks
		FInstancedStruct DefinitionStruct;
		if (!UChunkWorldHitBlueprintLibrary::TryGetBlockDefinitionForResolvedBlockHit(ResolvedHit, OutBlockTypeName, DefinitionStruct))
		{
			return false;
		}

		FInstancedStruct DefaultCustomData;
		if (!UChunkWorldHitBlueprintLibrary::TryGetBlockCustomDataForBlockTypeName(ResolvedHit.BlockTypeSchemaComponent, OutBlockTypeName, DefaultCustomData))
		{
			return false;
		}

		const FBlockDamageCustomData* DamageCustomData = DefaultCustomData.GetPtr<FBlockDamageCustomData>();
		if (DamageCustomData == nullptr)
		{
			return false;
		}

		OutCustomData = *DamageCustomData;
	}

	if (!OutBlockTypeName.IsValid())
	{
		return false;
	}

	FInstancedStruct DefinitionStruct;
	if (!UChunkWorldHitBlueprintLibrary::TryGetBlockDefinitionForResolvedBlockHit(ResolvedHit, OutBlockTypeName, DefinitionStruct))
	{
		return false;
	}

	return UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageDefinition(DefinitionStruct, OutDefinition);
}

bool UChunkWorldBlockDamageBlueprintLibrary::TryBroadcastHitFeedbackForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit)
{
	if (!ResolvedHit.bHasBlock || !IsValid(ResolvedHit.ChunkWorld))
	{
		return false;
	}

	FGameplayTag BlockTypeName;
	FBlockDamageDefinition Definition;
	FBlockDamageCustomData CustomData;
	if (!TryResolveDamageSchemaForResolvedBlockHit(ResolvedHit, false, BlockTypeName, Definition, CustomData) || Definition.HitSound == nullptr)
	{
		return false;
	}

	UChunkWorldBlockFeedbackComponent* FeedbackComponent = nullptr;
	if (!UChunkWorldHitBlueprintLibrary::GetBlockFeedbackComponentFromChunkWorld(ResolvedHit.ChunkWorld, FeedbackComponent))
	{
		return false;
	}

	const FVector FeedbackLocation = ResolvedHit.RepresentativeWorldPos.IsNearlyZero()
		? ResolvedHit.ChunkWorld->BlockWorldPosToUEWorldPos(ResolvedHit.BlockWorldPos)
		: ResolvedHit.RepresentativeWorldPos;
	return FeedbackComponent->BroadcastFeedbackAtLocation(FeedbackLocation, Definition.HitSound);
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
	FBlockDamageCustomData CustomData;
	if (!TryResolveDamageSchemaForResolvedBlockHit(ResolvedHit, true, BlockTypeName, Definition, CustomData))
	{
		return false;
	}

	OutResult.bHasDamageSchema = true;
	OutResult.BlockTypeName = BlockTypeName;
	OutResult.bWasInvincible = Definition.bInvincible;
	OutResult.PreviousHealth = CustomData.Health;
	OutResult.DamageApplied = DamageAmount;

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
		return true;
	}

	CustomData.Health = NewHealth;
	if (!ResolvedHit.BlockTypeSchemaComponent->SetBlockCustomDataForBlockWorldPos(ResolvedHit.BlockWorldPos, CustomData))
	{
		return false;
	}

	if (OutResult.bDestroyed)
	{
		return true;
	}

	(void)TryBroadcastHitFeedbackForResolvedBlockHit(ResolvedHit);
	return true;
}

bool UChunkWorldBlockDamageBlueprintLibrary::TryGetCurrentBlockHealthStateForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit, int32& OutHealth, bool& bOutInvincible, FGameplayTag& OutBlockTypeName)
{
	OutHealth = 0;
	bOutInvincible = false;
	OutBlockTypeName = FGameplayTag();

	FBlockDamageDefinition Definition;
	FBlockDamageCustomData CustomData;
	if (!TryResolveDamageSchemaForResolvedBlockHit(ResolvedHit, false, OutBlockTypeName, Definition, CustomData))
	{
		return false;
	}

	OutHealth = CustomData.Health;
	bOutInvincible = Definition.bInvincible;
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
	if (!UChunkWorldHitBlueprintLibrary::TryResolveBlockHitContextFromBlockWorldPos(ChunkWorld, BlockWorldPos, ResolvedHit) || !ResolvedHit.bHasBlock)
	{
		return false;
	}

	FBlockDamageDefinition Definition;
	FBlockDamageCustomData CustomData;
	if (!TryResolveDamageSchemaForResolvedBlockHit(ResolvedHit, false, OutBlockTypeName, Definition, CustomData))
	{
		return false;
	}

	OutMaxHealth = Definition.MaxHealth;
	OutHealth = CustomData.Health;
	bOutInvincible = Definition.bInvincible;

	FGameplayTag RuntimeBlockTypeName;
	FBlockDamageCustomData RuntimeCustomData;
	bOutHasStoredHealth = ResolvedHit.BlockTypeSchemaComponent != nullptr
		&& ResolvedHit.BlockTypeSchemaComponent->GetBlockCustomDataForBlockWorldPos(BlockWorldPos, RuntimeBlockTypeName, RuntimeCustomData);
	return true;
}
