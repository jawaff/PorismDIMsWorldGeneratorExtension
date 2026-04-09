// Copyright 2026 Spotted Loaf Studio

#include "Block/BlockTypeSchemaBlueprintLibrary.h"
#include "Block/BlockTypeSchemaRegistry.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockDamageBlueprintLibrary.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockHitBlueprintLibrary.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "StructUtils/InstancedStruct.h"

namespace
{
	FInstancedStruct MakeExactDamageDefinitionPayload(int32 MaxHealth, bool bInvincible)
	{
		FBlockDamageDefinition Definition;
		Definition.MaxHealth = MaxHealth;
		Definition.bInvincible = bInvincible;

		FInstancedStruct Payload;
		Payload.InitializeAs<FBlockDamageDefinition>(Definition);
		return Payload;
	}

	FInstancedStruct MakeExactDamageCustomDataPayload(int32 Health)
	{
		FBlockDamageCustomData CustomData;
		CustomData.Health = Health;

		FInstancedStruct Payload;
		Payload.InitializeAs<FBlockDamageCustomData>(CustomData);
		return Payload;
	}
}

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_PorismExtension_TestBlock, "DropType");

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionBlockHitBlueprintLibraryInvalidInputTest,
	"PorismExtension.BlockHit.BlueprintLibrary.RejectsInvalidInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionBlockHitBlueprintLibraryInvalidInputTest::RunTest(const FString& Parameters)
{
	AChunkWorld* ResolvedChunkWorld = nullptr;
	TestFalse(TEXT("Empty hit results do not resolve a chunk world"), UChunkWorldBlockHitBlueprintLibrary::GetChunkWorldFromHitResult(FHitResult(), ResolvedChunkWorld));

	UBlockTypeSchemaComponent* SchemaComponent = nullptr;
	TestFalse(TEXT("Null chunk worlds do not resolve a schema component"), UChunkWorldBlockHitBlueprintLibrary::GetBlockTypeSchemaComponentFromChunkWorld(nullptr, SchemaComponent));

	UChunkWorldBlockFeedbackComponent* FeedbackComponent = nullptr;
	TestFalse(TEXT("Null chunk worlds do not resolve a feedback component"), UChunkWorldBlockHitBlueprintLibrary::GetBlockFeedbackComponentFromChunkWorld(nullptr, FeedbackComponent));

	FChunkWorldResolvedBlockHit ResolvedHit;
	TestFalse(TEXT("Block-world-position resolution fails cleanly without a chunk world"), UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromBlockWorldPos(nullptr, FIntVector::ZeroValue, ResolvedHit));

	FGameplayTag BlockTypeName;
	FInstancedStruct Payload;
	TestFalse(TEXT("Custom-data reads fail cleanly for invalid resolved hits"), UChunkWorldBlockHitBlueprintLibrary::TryGetBlockCustomDataForResolvedBlockHit(ResolvedHit, BlockTypeName, Payload));
	TestFalse(TEXT("Definition reads fail cleanly for invalid resolved hits"), UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForResolvedBlockHit(ResolvedHit, BlockTypeName, Payload));
	TestFalse(TEXT("Destroyed feedback broadcast fails cleanly for invalid resolved hits"), UChunkWorldBlockHitBlueprintLibrary::TryBroadcastDestroyedFeedbackForResolvedBlockHit(ResolvedHit));

	AChunkWorldExtended* ChunkWorld = NewObject<AChunkWorldExtended>();
	TestNotNull(TEXT("Chunk world test actor is created"), ChunkWorld);

	UStaticMeshComponent* PrimitiveComponent = NewObject<UStaticMeshComponent>(ChunkWorld);
	TestNotNull(TEXT("Primitive component is created"), PrimitiveComponent);

	FHitResult HitResult;
	HitResult.Component = PrimitiveComponent;

	TestTrue(TEXT("Hit component owner resolves back to the owning chunk world"), UChunkWorldBlockHitBlueprintLibrary::GetChunkWorldFromHitResult(HitResult, ResolvedChunkWorld));
	TestEqual(TEXT("Resolved chunk world matches the hit component owner"), ResolvedChunkWorld, static_cast<AChunkWorld*>(ChunkWorld));
	TestTrue(TEXT("Chunk world exposes its schema component through the block hit helper"), UChunkWorldBlockHitBlueprintLibrary::GetBlockTypeSchemaComponentFromChunkWorld(ChunkWorld, SchemaComponent));
	TestEqual(TEXT("Resolved schema component matches the chunk world component"), SchemaComponent, ChunkWorld->GetBlockTypeSchemaComponent());
	TestTrue(TEXT("Chunk world exposes its block feedback component through the block hit helper"), UChunkWorldBlockHitBlueprintLibrary::GetBlockFeedbackComponentFromChunkWorld(ChunkWorld, FeedbackComponent));
	TestEqual(TEXT("Resolved feedback component matches the chunk world component"), FeedbackComponent, ChunkWorld->GetBlockFeedbackComponent());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionBlockHitBlueprintLibraryRegistryPayloadTest,
	"PorismExtension.BlockHit.BlueprintLibrary.ResolvesRegistryPayloadsByBlockTypeName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionBlockHitBlueprintLibraryRegistryPayloadTest::RunTest(const FString& Parameters)
{
	UBlockTypeSchemaRegistry* Registry = NewObject<UBlockTypeSchemaRegistry>();
	TestNotNull(TEXT("Registry object is created"), Registry);

	FBlockTypeSchema Row;
	Row.BlockTypeName = TAG_PorismExtension_TestBlock;
	Row.Definition = MakeExactDamageDefinitionPayload(72, true);
	Row.CustomData = MakeExactDamageCustomDataPayload(18);

	TArray<FBlockTypeSchema>& MutableRows = const_cast<TArray<FBlockTypeSchema>&>(Registry->GetBlockTypeDefinitions());
	MutableRows.Add(Row);

	UBlockTypeSchemaComponent* SchemaComponent = NewObject<UBlockTypeSchemaComponent>();
	TestNotNull(TEXT("Schema component is created"), SchemaComponent);
	SchemaComponent->SetBlockTypeSchemaRegistry(Registry);

	FInstancedStruct DefinitionPayload;
	TestTrue(TEXT("Definition lookup succeeds for the authored block type tag"), UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForBlockTypeName(SchemaComponent, TAG_PorismExtension_TestBlock, DefinitionPayload));

	FBlockDamageDefinition DamageDefinition;
	TestTrue(TEXT("Resolved definition converts to the shared damage definition family"), UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageDefinition(DefinitionPayload, DamageDefinition));
	TestEqual(TEXT("Resolved definition keeps the authored max health"), DamageDefinition.MaxHealth, 72);
	TestTrue(TEXT("Resolved definition keeps the authored invincibility flag"), DamageDefinition.bInvincible);

	FInstancedStruct CustomDataPayload;
	TestTrue(TEXT("Custom-data lookup succeeds for the authored block type tag"), UChunkWorldBlockHitBlueprintLibrary::TryGetBlockCustomDataForBlockTypeName(SchemaComponent, TAG_PorismExtension_TestBlock, CustomDataPayload));

	FBlockDamageCustomData DamageCustomData;
	TestTrue(TEXT("Resolved custom data converts to the shared damage custom-data family"), UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageCustomData(CustomDataPayload, DamageCustomData));
	TestEqual(TEXT("Resolved custom data keeps the authored health value"), DamageCustomData.Health, 18);

	TestFalse(TEXT("Invalid block type tags do not resolve definitions"), UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForBlockTypeName(SchemaComponent, FGameplayTag(), DefinitionPayload));
	TestFalse(TEXT("Invalid block type tags do not resolve custom data"), UChunkWorldBlockHitBlueprintLibrary::TryGetBlockCustomDataForBlockTypeName(SchemaComponent, FGameplayTag(), CustomDataPayload));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionBlockDamageBlueprintLibraryInvalidInputTest,
	"PorismExtension.BlockDamage.BlueprintLibrary.RejectsInvalidInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionBlockDamageBlueprintLibraryInvalidInputTest::RunTest(const FString& Parameters)
{
	FChunkWorldResolvedBlockHit ResolvedHit;
	FChunkWorldBlockDamageResult DamageResult;
	TestFalse(TEXT("Hit feedback rejects invalid resolved hits"), UChunkWorldBlockDamageBlueprintLibrary::TryBroadcastHitFeedbackForResolvedBlockHit(ResolvedHit));
	TestFalse(TEXT("Damage apply rejects invalid resolved hits"), UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(ResolvedHit, 5, DamageResult));
	TestFalse(TEXT("Damage apply rejects zero damage"), UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(ResolvedHit, 0, DamageResult));

	int32 CurrentHealth = 0;
	int32 MaxHealth = 0;
	bool bIsInvincible = false;
	bool bHasRuntimeHealth = false;
	FGameplayTag BlockTypeName;
	TestFalse(TEXT("Current health reads reject invalid resolved hits"), UChunkWorldBlockDamageBlueprintLibrary::TryGetCurrentBlockHealthStateForResolvedBlockHit(ResolvedHit, CurrentHealth, bIsInvincible, BlockTypeName));
	TestFalse(TEXT("Runtime health reads reject invalid resolved hits"), UChunkWorldBlockDamageBlueprintLibrary::TryGetRuntimeBlockHealthStateForResolvedBlockHit(ResolvedHit, CurrentHealth, MaxHealth, bIsInvincible, bHasRuntimeHealth, BlockTypeName));
	TestFalse(TEXT("Block-position health reads reject null chunk worlds"), UChunkWorldBlockDamageBlueprintLibrary::TryGetBlockHealthStateForBlockWorldPos(nullptr, FIntVector::ZeroValue, CurrentHealth, MaxHealth, bIsInvincible, bHasRuntimeHealth, BlockTypeName));

	return true;
}
