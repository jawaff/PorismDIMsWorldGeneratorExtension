// Copyright 2026 Spotted Loaf Studio

#include "Block/BlockTypeSchemaBlueprintLibrary.h"
#include "Block/BlockTypeSchemaRegistry.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockDamageBlueprintLibrary.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockHitBlueprintLibrary.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockPresentationSubsystem.h"
#include "Materials/Material.h"
#include "Misc/ScopeExit.h"
#include "NiagaraSystem.h"
#include "Sound/SoundWave.h"
#include "Components/StaticMeshComponent.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "ChunkWorld/Support/ChunkWorldHitQueryTestTypes.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPorismExtensionBlockHitUniqueProbesTest,
	"PorismExtension.BlockHit.BlueprintLibrary.UniqueFallbackProbes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionBlockHitUniqueProbesTest::RunTest(const FString&)
{
	auto* World = GEditor->GetEditorWorldContext().World()->SpawnActor<AChunkWorldHitQueryTestWorld>();
	FLayoutTestWorldSupport::InitializeTransientChunkWorld(World, FIntVector(16), TEXT("HitQueryTest"));
	ON_SCOPE_EXIT { FLayoutTestWorldSupport::ShutdownTransientChunkWorld(World); World->Destroy(); };
	auto* Component = NewObject<UStaticMeshComponent>(World);
	FHitResult Hit;
	Hit.Component = Component;
	// Native collision identity is absent: exercise the real local fallback used by stale hits.
	AddExpectedError(TEXT("HitCheck FAIL"), EAutomationExpectedErrorFlags::Contains, 4);
	for (const FVector Point : {FVector(4.25), FVector(-4.25)})
	{
		Hit.ImpactPoint = Point;
		const FIntVector Cell = World->UEWorldPosToBlockWorldPos(Point);
		World->MaterialValues.Reset();
		World->MaterialReads.Reset();
		FChunkWorldResolvedBlockHit Resolved;
		TestFalse(TEXT("Coincident empty probes remain a miss"),
			UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromHitResult(Hit, FVector::ZeroVector, Resolved));
		const int32 CellReads = World->MaterialReads.FilterByPredicate([&](const FIntVector& Read) { return Read == Cell; }).Num();
		AddInfo(FString::Printf(TEXT("HIT_QUERY empty cell=%s candidate_reads=%d total_material_reads=%d"),
			*Cell.ToString(), CellReads, World->MaterialReads.Num()));
		TestEqual(TEXT("Each fallback candidate is resolved once within the call"), CellReads, 1);
		TestFalse(TEXT("A miss does not publish a stale block"), Resolved.bHasBlock);

		World->MaterialValues.Add(Cell, SinfullMaterial);
		World->MaterialReads.Reset();
		TestTrue(TEXT("An edit is visible on the next independent hit query"),
			UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromHitResult(Hit, FVector::ZeroVector, Resolved));
		TestEqual(TEXT("The edited candidate retains priority"), Resolved.BlockWorldPos, Cell);
		TestEqual(TEXT("The first successful probe retains its representative point"), Resolved.RepresentativeWorldPos, Point);
		TestEqual(TEXT("The successful fallback source is retained"), Resolved.ResolveSource, EChunkWorldBlockHitResolveSource::LocalProbe);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPorismExtensionBlockHitBatchedFallbackTest,
	"PorismExtension.BlockHit.BlueprintLibrary.BatchedFallbackReads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionBlockHitBatchedFallbackTest::RunTest(const FString&)
{
	auto* World = GEditor->GetEditorWorldContext().World()->SpawnActor<AChunkWorldHitQueryTestWorld>();
	FLayoutTestWorldSupport::InitializeTransientChunkWorld(World, FIntVector(16), TEXT("BatchedHitQuery"));
	ON_SCOPE_EXIT { FLayoutTestWorldSupport::ShutdownTransientChunkWorld(World); World->Destroy(); };
	FHitResult Hit;
	Hit.Component = NewObject<UStaticMeshComponent>(World);
	AddExpectedError(TEXT("HitCheck FAIL"), EAutomationExpectedErrorFlags::Contains, 8);
	for (const FVector Point : {FVector(4.25), FVector(-4.25)})
	{
		Hit.ImpactPoint = Point;
		const FIntVector Cell = World->UEWorldPosToBlockWorldPos(Point);
		World->MaterialValues.Reset();
		World->MaterialReadBatches = 0;
		FChunkWorldResolvedBlockHit Resolved;
		TestFalse(TEXT("Empty fallback remains unresolved"),
			UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromHitResult(Hit, FVector::ZeroVector, Resolved));
		TestEqual(TEXT("Fallback candidate and overlay use one native material batch"), World->MaterialReadBatches, 1);
		World->MaterialValues.Add(Cell, SinfullMaterial);
		World->MaterialReadBatches = 0;
		TestTrue(TEXT("The next query sees edited material without retained misses"),
			UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromHitResult(Hit, FVector::ZeroVector, Resolved));
		TestEqual(TEXT("Hit resolution also uses one native material batch"), World->MaterialReadBatches, 1);
		TestEqual(TEXT("Candidate priority is unchanged"), Resolved.BlockWorldPos, Cell);
		TestEqual(TEXT("Representative position is unchanged"), Resolved.RepresentativeWorldPos, Point);

		FMeshData Mesh;
		Mesh.MeshId = 19;
		World->SetMeshDataByBlockWorldPos(Cell + FIntVector(0, 0, 1), Mesh, false);
		TestTrue(TEXT("Batched reads retain live overlay promotion"),
			UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromHitResult(Hit, FVector::ZeroVector, Resolved));
		TestEqual(TEXT("Overlay keeps its position"), Resolved.BlockWorldPos, Cell + FIntVector(0, 0, 1));
		TestEqual(TEXT("Overlay keeps its native mesh ID"), Resolved.MeshIndex, 19);
		Mesh.MeshId = 23;
		World->SetMeshDataByBlockWorldPos(Cell, Mesh, false);
		TestTrue(TEXT("A direct mesh retains priority over the overlay"),
			UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromHitResult(Hit, FVector::ZeroVector, Resolved));
		TestEqual(TEXT("Direct mesh keeps its position"), Resolved.BlockWorldPos, Cell);
		TestEqual(TEXT("Direct mesh keeps its native mesh ID"), Resolved.MeshIndex, 23);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionBlockHitBlueprintLibraryRegistryPayloadTest,
	"PorismExtension.BlockHit.BlueprintLibrary.ResolvesRegistryPayloadsByBlockTypeName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionBlockHitBlueprintLibraryRegistryPayloadTest::RunTest(const FString& Parameters)
{
	const FGameplayTag TestBlock = FGameplayTag::RequestGameplayTag(TEXT("DropType"));
	UBlockTypeSchemaRegistry* Registry = NewObject<UBlockTypeSchemaRegistry>();
	TestNotNull(TEXT("Registry object is created"), Registry);

	FBlockTypeSchema Row;
	Row.BlockTypeName = TestBlock;
	Row.Definition = MakeExactHealthDefinitionPayload(72, true);
	Row.CustomData = MakeExactHealthCustomDataPayload(18);

	TArray<FBlockTypeSchema>& MutableRows = const_cast<TArray<FBlockTypeSchema>&>(Registry->GetBlockTypeDefinitions());
	MutableRows.Add(Row);

	UBlockTypeSchemaComponent* SchemaComponent = NewObject<UBlockTypeSchemaComponent>();
	TestNotNull(TEXT("Schema component is created"), SchemaComponent);
	SchemaComponent->SetBlockTypeSchemaRegistry(Registry);

	FInstancedStruct DefinitionPayload;
	TestTrue(TEXT("Definition lookup succeeds for the authored block type tag"), UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForBlockTypeName(SchemaComponent, TestBlock, DefinitionPayload));

	FBlockHealthDefinition HealthDefinition;
	TestTrue(TEXT("Resolved definition converts to the shared health definition family"), UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthDefinition(DefinitionPayload, HealthDefinition));
	TestEqual(TEXT("Resolved definition keeps the authored max health"), HealthDefinition.MaxHealth, 72);
	TestTrue(TEXT("Resolved definition keeps the authored invincibility flag"), HealthDefinition.bInvincible);

	FInstancedStruct CustomDataPayload;
	TestTrue(TEXT("Custom-data lookup succeeds for the authored block type tag"), UChunkWorldBlockHitBlueprintLibrary::TryGetBlockCustomDataForBlockTypeName(SchemaComponent, TestBlock, CustomDataPayload));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPorismExtensionCommittedBlockPresentationTest,
	"PorismExtension.BlockDamage.BlueprintLibrary.CommittedPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionCommittedBlockPresentationTest::RunTest(const FString& Parameters)
{
	auto Harness = PorismLayoutWorldTestUtilities::CreateChunkWorldHarness(nullptr);
	if (!TestNotNull(TEXT("Running chunk world"), Harness.World)) return false;
	auto* Schema = Harness.World->GetBlockTypeSchemaComponent();
	auto* Registry = Schema->GetBlockTypeSchemaRegistry();
	Harness.World->StopGen();
	FBlockHealthDefinition Definition;
	Definition.MaxHealth = 20;
	Definition.MaterialAsset = UMaterial::GetDefaultMaterial(MD_Surface);
	Definition.HitSound = NewObject<USoundWave>();
	Definition.DestroyedSound = NewObject<USoundWave>();
	Definition.HitEffect = NewObject<UNiagaraSystem>();
	Definition.DestroyedEffect = NewObject<UNiagaraSystem>();
	FBlockTypeSchema Row;
	Row.BlockTypeName = FGameplayTag::RequestGameplayTag(TEXT("DropType"));
	Row.Definition.InitializeAs<FBlockHealthDefinition>(Definition);
	Row.CustomData = MakeExactHealthCustomDataPayload(20);
	auto& Rows = const_cast<TArray<FBlockTypeSchema>&>(Registry->GetBlockTypeDefinitions());
	Rows.Add(Row);
	Harness.World->StartGen();
	if (!TestTrue(TEXT("Schema-backed generator restarted"), Harness.World->IsRunning())) return false;
	auto* Sink = Harness.World->GetWorld()->GetSubsystem<UChunkWorldBlockPresentationSubsystem>();
	if (!TestNotNull(TEXT("Presentation integration"), Sink)) return false;
	TArray<FChunkWorldBlockPresentation> Events;
	const FDelegateHandle Handle = Sink->OnCommitted.AddLambda([&](const FChunkWorldBlockPresentation& Event) { Events.Add(Event); });
	ON_SCOPE_EXIT { Sink->OnCommitted.Remove(Handle); };
	TestTrue(TEXT("Bound integration suppresses legacy playback"), UChunkWorldBlockPresentationSubsystem::HasConsumer(Harness.World->GetWorld()));

	const FIntVector Position(4, 4, 4);
	Harness.World->SetBlockValueByBlockWorldPos(Position, SinfullMaterial, false);
	FChunkWorldResolvedBlockHit Hit;
	if (!TestTrue(TEXT("Real represented block resolves"), UChunkWorldBlockHitBlueprintLibrary::TryResolveDirectBlockHitContextFromBlockWorldPos(Harness.World, Position, Hit))) return false;
	FChunkWorldBlockHealthDeltaResult Result;
	TestTrue(TEXT("Initial damage commits"), UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(Hit, 7, Result));
	if (!TestEqual(TEXT("Initialization and first damage publish once"), Events.Num(), 1)) return false;
	TestEqual(TEXT("Committed damage, not initial full-health refill"), Events.Last().HealthDelta, -7);
	TestEqual(TEXT("Nonlethal sound captured"), Events.Last().Sound, Definition.HitSound.Get());
	TestEqual(TEXT("Nonlethal Niagara captured"), Events.Last().Niagara, Definition.HitEffect.Get());
	TestTrue(TEXT("Overheal succeeds"), UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockHealingForResolvedBlockHit(Hit, 100, Result));
	TestEqual(TEXT("Healing clamps to actual missing health"), Events.Last().HealthDelta, 7);
	TestNull(TEXT("Healing does not replay impact sound"), Events.Last().Sound);
	const int32 BeforeNoop = Events.Num();
	UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockHealingForResolvedBlockHit(Hit, 100, Result);
	TestEqual(TEXT("Full-health heal is silent"), Events.Num(), BeforeNoop);

	Rows[0].Definition.GetMutable<FBlockHealthDefinition>().bInvincible = true;
	Schema->RebuildBlockDefinitionLookupMaps();
	TestTrue(TEXT("Invincible impact accepted"), UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(Hit, 100, Result));
	TestEqual(TEXT("Invincible hit publishes one impact"), Events.Num(), BeforeNoop + 1);
	TestEqual(TEXT("Invincible hit has no damage number"), Events.Last().HealthDelta, 0);
	TestEqual(TEXT("Invincible hit retains impact sound"), Events.Last().Sound, Definition.HitSound.Get());
	Rows[0].Definition.GetMutable<FBlockHealthDefinition>().bInvincible = false;
	Schema->RebuildBlockDefinitionLookupMaps();
	TestTrue(TEXT("Overkill commits"), UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(Hit, 100, Result));
	TestEqual(TEXT("Overkill shows remaining health, not requested damage"), Events.Last().HealthDelta, -20);
	TestTrue(TEXT("Lethal outcome captured"), Events.Last().Result.bDestroyed);
	TestEqual(TEXT("Destruction sound survives representation removal"), Events.Last().Sound, Definition.DestroyedSound.Get());
	TestEqual(TEXT("Destruction Niagara survives representation removal"), Events.Last().Niagara, Definition.DestroyedEffect.Get());
	TestEqual(TEXT("Block identity retained"), Events.Last().Result.BlockWorldPos, Position);
	TestEqual(TEXT("Representative anchor retained"), Events.Last().Point, Hit.RepresentativeWorldPos);
	const int32 BeforeRepeated = Events.Num();
	UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(Hit, 100, Result);
	TestEqual(TEXT("Repeated stale lethal request cannot replay outcome"), Events.Num(), BeforeRepeated);

	const FIntVector FailedPosition(6, 4, 4);
	Harness.World->SetBlockValueByBlockWorldPos(FailedPosition, SinfullMaterial, false);
	UChunkWorldBlockHitBlueprintLibrary::TryResolveDirectBlockHitContextFromBlockWorldPos(Harness.World, FailedPosition, Hit);
	// A schema/storage mismatch must fail at the real write boundary without publishing proposed health.
	const TArray<int32> Defaults = Harness.World->RuntimeConfig->CustomFeatureDefaultData;
	Harness.World->RuntimeConfig->CustomFeatureDefaultData.Reset();
	AddExpectedError(TEXT("custom-data channels"), EAutomationExpectedErrorFlags::Contains, 5);
	TestFalse(TEXT("Failed schema write rejects damage"), UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(Hit, 1, Result));
	Harness.World->RuntimeConfig->CustomFeatureDefaultData = Defaults;
	TestEqual(TEXT("Failed write publishes nothing"), Events.Num(), BeforeRepeated);
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
