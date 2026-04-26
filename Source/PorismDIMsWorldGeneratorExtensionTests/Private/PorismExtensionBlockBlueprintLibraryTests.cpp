// Copyright 2026 Spotted Loaf Studio

#include "Block/BlockTypeSchemaBlueprintLibrary.h"
#include "Block/BlockTypeSchemaRegistry.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockDamageBlueprintLibrary.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockHitBlueprintLibrary.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Layout/LayoutWorldTestUtilities.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "StructUtils/InstancedStruct.h"

namespace
{
	FInstancedStruct MakeExactHealthDefinitionPayload(int32 MaxHealth, bool bInvincible)
	{
		FBlockHealthDefinition Definition;
		Definition.MaxHealth = MaxHealth;
		Definition.bInvincible = bInvincible;

		FInstancedStruct Payload;
		Payload.InitializeAs<FBlockHealthDefinition>(Definition);
		return Payload;
	}

	FInstancedStruct MakeExactHealthCustomDataPayload(int32 Health)
	{
		FBlockHealthCustomData CustomData;
		CustomData.Health = Health;

		FInstancedStruct Payload;
		Payload.InitializeAs<FBlockHealthCustomData>(CustomData);
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
	Row.Definition = MakeExactHealthDefinitionPayload(72, true);
	Row.CustomData = MakeExactHealthCustomDataPayload(18);

	TArray<FBlockTypeSchema>& MutableRows = const_cast<TArray<FBlockTypeSchema>&>(Registry->GetBlockTypeDefinitions());
	MutableRows.Add(Row);

	UBlockTypeSchemaComponent* SchemaComponent = NewObject<UBlockTypeSchemaComponent>();
	TestNotNull(TEXT("Schema component is created"), SchemaComponent);
	SchemaComponent->SetBlockTypeSchemaRegistry(Registry);

	FInstancedStruct DefinitionPayload;
	TestTrue(TEXT("Definition lookup succeeds for the authored block type tag"), UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForBlockTypeName(SchemaComponent, TAG_PorismExtension_TestBlock, DefinitionPayload));

	FBlockHealthDefinition HealthDefinition;
	TestTrue(TEXT("Resolved definition converts to the shared health definition family"), UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthDefinition(DefinitionPayload, HealthDefinition));
	TestEqual(TEXT("Resolved definition keeps the authored max health"), HealthDefinition.MaxHealth, 72);
	TestTrue(TEXT("Resolved definition keeps the authored invincibility flag"), HealthDefinition.bInvincible);

	FInstancedStruct CustomDataPayload;
	TestTrue(TEXT("Custom-data lookup succeeds for the authored block type tag"), UChunkWorldBlockHitBlueprintLibrary::TryGetBlockCustomDataForBlockTypeName(SchemaComponent, TAG_PorismExtension_TestBlock, CustomDataPayload));

	FBlockHealthCustomData HealthCustomData;
	TestTrue(TEXT("Resolved custom data converts to the shared health custom-data family"), UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthCustomData(CustomDataPayload, HealthCustomData));
	TestEqual(TEXT("Resolved custom data keeps the authored health value"), HealthCustomData.Health, 18);

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
	FChunkWorldBlockHealthDeltaResult DamageResult;
	TestFalse(TEXT("Hit feedback rejects invalid resolved hits"), UChunkWorldBlockDamageBlueprintLibrary::TryBroadcastHitFeedbackForResolvedBlockHit(ResolvedHit));
	TestFalse(TEXT("Damage apply rejects invalid resolved hits"), UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(ResolvedHit, 5, DamageResult));
	TestFalse(TEXT("Damage apply rejects zero damage"), UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(ResolvedHit, 0, DamageResult));
	TestFalse(TEXT("Healing apply rejects invalid resolved hits"), UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockHealingForResolvedBlockHit(ResolvedHit, 5, DamageResult));
	TestFalse(TEXT("Healing apply rejects zero healing"), UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockHealingForResolvedBlockHit(ResolvedHit, 0, DamageResult));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionBlockHitBlueprintLibrarySupportResolutionTest,
	"PorismExtension.BlockHit.BlueprintLibrary.BlockWorldPositionResolutionTargetsSupportBlocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionBlockHitBlueprintLibrarySupportResolutionTest::RunTest(const FString& Parameters)
{
	PorismLayoutWorldTestUtilities::FLayoutWorldTestHarness Harness = PorismLayoutWorldTestUtilities::CreateChunkWorldHarness(nullptr);
	TestNotNull(TEXT("Transient chunk world harness spawns a runtime world"), Harness.World);
	if (Harness.World == nullptr)
	{
		return false;
	}

	const FIntVector SupportBlockWorldPos(4, 4, 4);
	const FIntVector DestroyedMeshBlockWorldPos = SupportBlockWorldPos + FIntVector(0, 0, 1);

	Harness.World->SetBlockValueByBlockWorldPos(SupportBlockWorldPos, SinfullMaterial, false);
	Harness.World->SetBlockValueByBlockWorldPos(DestroyedMeshBlockWorldPos, SinfullMaterial, false);

	FChunkWorldResolvedBlockHit ResolvedFromBlockWorldPos;
	TestTrue(
		TEXT("Block-world-position resolution still targets the support block when the upper cell only retains background material"),
		UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromBlockWorldPos(Harness.World, SupportBlockWorldPos, ResolvedFromBlockWorldPos));
	TestEqual(TEXT("Support-block resolution keeps the lower block position"), ResolvedFromBlockWorldPos.BlockWorldPos, SupportBlockWorldPos);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionBlockHitBlueprintLibraryDirectResolutionTest,
	"PorismExtension.BlockHit.BlueprintLibrary.DirectBlockWorldPositionResolutionDoesNotPromoteOverlayMeshes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionBlockHitBlueprintLibraryDirectResolutionTest::RunTest(const FString& Parameters)
{
	PorismLayoutWorldTestUtilities::FLayoutWorldTestHarness Harness = PorismLayoutWorldTestUtilities::CreateChunkWorldHarness(nullptr);
	TestNotNull(TEXT("Transient chunk world harness spawns a runtime world"), Harness.World);
	if (Harness.World == nullptr)
	{
		return false;
	}

	const FIntVector SupportBlockWorldPos(5, 5, 5);
	const FIntVector OverlayBlockWorldPos = SupportBlockWorldPos + FIntVector(0, 0, 1);

	Harness.World->SetBlockValueByBlockWorldPos(SupportBlockWorldPos, SinfullMaterial, false);
	FMeshData OverlayMeshData;
	OverlayMeshData.MeshId = 19;
	Harness.World->SetMeshDataByBlockWorldPos(OverlayBlockWorldPos, OverlayMeshData, false);

	FChunkWorldResolvedBlockHit DirectResolvedHit;
	TestTrue(
		TEXT("Direct block-world-position resolution succeeds for the support block"),
		UChunkWorldBlockHitBlueprintLibrary::TryResolveDirectBlockHitContextFromBlockWorldPos(Harness.World, SupportBlockWorldPos, DirectResolvedHit));
	TestEqual(TEXT("Direct resolution stays on the requested support block instead of promoting to the overlay above"), DirectResolvedHit.BlockWorldPos, SupportBlockWorldPos);
	TestEqual(TEXT("Direct resolution keeps the support block material index"), DirectResolvedHit.MaterialIndex, SinfullMaterial);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionBlockHitBlueprintLibraryOverlayPromotionHeuristicTest,
	"PorismExtension.BlockHit.BlueprintLibrary.OnlyPromotesOverlayMeshesFromUpperSupportHits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionBlockHitBlueprintLibraryOverlayPromotionHeuristicTest::RunTest(const FString& Parameters)
{
	PorismLayoutWorldTestUtilities::FLayoutWorldTestHarness Harness = PorismLayoutWorldTestUtilities::CreateChunkWorldHarness(nullptr);
	TestNotNull(TEXT("Transient chunk world harness spawns a runtime world"), Harness.World);
	if (Harness.World == nullptr || Harness.World->PrimLayer == nullptr)
	{
		return false;
	}

	const FIntVector SupportBlockWorldPos(7, 7, 7);
	const FVector SupportBlockCenter = Harness.World->BlockWorldPosToUEWorldPos(SupportBlockWorldPos);
	const float HalfBlockExtent = static_cast<float>(Harness.World->PrimLayer->BlockSize) * 0.5f;

	const FVector UpperSlopeImpactPoint = SupportBlockCenter + FVector(HalfBlockExtent * 0.2f, 0.0f, HalfBlockExtent * 0.7f);
	TestTrue(
		TEXT("Upper-region hits that approach from above still promote to the overlay even if the struck surface is not flat"),
		UChunkWorldBlockHitBlueprintLibrary::ShouldPromoteOverlayBlockFromHit(
			Harness.World,
			SupportBlockWorldPos,
			UpperSlopeImpactPoint,
			FVector(1.0f, 0.0f, 0.0f),
			FVector(0.0f, 0.0f, -1.0f)));

	const FVector MidHeightSideImpactPoint = SupportBlockCenter + FVector(HalfBlockExtent * 0.6f, 0.0f, 0.0f);
	TestTrue(
		TEXT("Hits whose surface normal is in the top hemisphere of the support block still promote to the overlay even when the impact point is not near the very top"),
		UChunkWorldBlockHitBlueprintLibrary::ShouldPromoteOverlayBlockFromHit(
			Harness.World,
			SupportBlockWorldPos,
			MidHeightSideImpactPoint,
			FVector(0.0f, 0.0f, 0.15f),
			FVector(1.0f, 0.0f, 0.0f)));

	const FVector LowerUndersideImpactPoint = SupportBlockCenter + FVector(0.0f, 0.0f, -HalfBlockExtent * 0.7f);
	TestFalse(
		TEXT("Lower or underside hits do not redirect to the overlay mesh above"),
		UChunkWorldBlockHitBlueprintLibrary::ShouldPromoteOverlayBlockFromHit(
			Harness.World,
			SupportBlockWorldPos,
			LowerUndersideImpactPoint,
			-FVector::UpVector,
			FVector(0.0f, 0.0f, 1.0f)));

	return true;
}
