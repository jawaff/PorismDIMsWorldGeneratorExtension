// Copyright 2026 Spotted Loaf Studio

#include "Actor/Components/PorismHealthInteractionComponent.h"
#include "Actor/Components/PorismPredictedBlockStateComponent.h"
#include "Actor/Components/PorismTraceInteractionComponent.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionChunkWorldExtendedComponentWiringTest,
	"PorismExtension.ChunkWorldExtended.Actor.CreatesSharedComponents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionChunkWorldExtendedComponentWiringTest::RunTest(const FString& Parameters)
{
	AChunkWorldExtended* ChunkWorld = NewObject<AChunkWorldExtended>();
	TestNotNull(TEXT("Chunk world extended test actor is created"), ChunkWorld);
	TestNotNull(TEXT("Chunk world extended creates a block schema component"), ChunkWorld->GetBlockTypeSchemaComponent());
	TestNotNull(TEXT("Chunk world extended creates a block feedback component"), ChunkWorld->GetBlockFeedbackComponent());
	TestNotNull(TEXT("Chunk world extended creates a block swap scanner component"), ChunkWorld->GetBlockSwapScannerComponent());
	TestNotNull(TEXT("Chunk world extended creates a block swap component"), ChunkWorld->GetBlockSwapComponent());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionPredictedBlockStateInvalidInputTest,
	"PorismExtension.Prediction.Component.RejectsInvalidDamageRequests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionPredictedBlockStateInvalidInputTest::RunTest(const FString& Parameters)
{
	AActor* Owner = NewObject<AActor>();
	UPorismPredictedBlockStateComponent* PredictionComponent = NewObject<UPorismPredictedBlockStateComponent>(Owner);
	TestNotNull(TEXT("Prediction component is created"), PredictionComponent);

	FChunkWorldBlockDamageRequest InvalidRequest;
	FChunkWorldBlockDamageRequestResult RequestResult;
	AddExpectedError(TEXT("damage amount was 0"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Invalid predicted block damage requests are rejected"), PredictionComponent->ApplyPredictedDamageRequest(InvalidRequest, RequestResult));

	FChunkWorldResolvedBlockHit ResolvedHit;
	FChunkWorldBlockDamageRequest InvalidAuthoritativeRequest;
	InvalidAuthoritativeRequest.DamageAmount = 4;
	AddExpectedError(TEXT("resolved hit was invalid"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Invalid authoritative block damage requests are rejected"), PredictionComponent->ApplyAuthoritativeDamageRequest(InvalidAuthoritativeRequest, RequestResult));

	int32 Health = 0;
	bool bIsInvincible = false;
	FGameplayTag BlockTypeName;
	TestFalse(TEXT("No predicted state exists for an invalid resolved hit"), PredictionComponent->TryGetPredictedHealthState(ResolvedHit, Health, bIsInvincible, BlockTypeName));

	FPorismBlockHealthState HealthState;
	TestFalse(TEXT("Health-state lookup fails cleanly for an invalid resolved hit"), PredictionComponent->TryGetHealthState(ResolvedHit, HealthState));

	PredictionComponent->ClearPredictionForResolvedBlockHit(ResolvedHit);
	TestTrue(TEXT("Clearing a missing prediction is a no-op"), true);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionPredictedBlockStateInvalidInputNoBroadcastTest,
	"PorismExtension.Prediction.Component.InvalidRequestsDoNotBroadcastTrackedStateChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionPredictedBlockStateInvalidInputNoBroadcastTest::RunTest(const FString& Parameters)
{
	AActor* Owner = NewObject<AActor>();
	UPorismPredictedBlockStateComponent* PredictionComponent = NewObject<UPorismPredictedBlockStateComponent>(Owner);
	TestNotNull(TEXT("Prediction component is created"), PredictionComponent);

	int32 BroadcastCount = 0;
	PredictionComponent->OnTrackedBlockStateChanged().AddLambda([&BroadcastCount](AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos)
	{
		++BroadcastCount;
	});

	FChunkWorldBlockDamageRequest InvalidRequest;
	FChunkWorldBlockDamageRequestResult RequestResult;
	AddExpectedError(TEXT("damage amount was 0"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Invalid predicted requests are rejected"), PredictionComponent->ApplyPredictedDamageRequest(InvalidRequest, RequestResult));
	TestEqual(TEXT("Rejected requests do not broadcast tracked block state changes"), BroadcastCount, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionTraceInteractionConfigurationTest,
	"PorismExtension.TraceInteraction.Component.ConfigurationDefaultsAndToggles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionTraceInteractionConfigurationTest::RunTest(const FString& Parameters)
{
	AActor* Owner = NewObject<AActor>();
	UPorismTraceInteractionComponent* TraceComponent = NewObject<UPorismTraceInteractionComponent>(Owner);
	TestNotNull(TEXT("Trace interaction component is created"), TraceComponent);

	TestEqual(TEXT("Initial trace distance matches the base distance"), TraceComponent->GetTraceDistance(), TraceComponent->GetBaseTraceDistance());
	TestFalse(TEXT("Fresh trace components start without an interaction target"), TraceComponent->HasValidInteractionTarget());
	TestFalse(TEXT("Fresh trace components start without an active block interaction"), TraceComponent->HasActiveBlockInteraction());

	TraceComponent->SetTraceDistance(480.0f, false);
	TestEqual(TEXT("Base trace distance updates without forcing an immediate trace"), TraceComponent->GetBaseTraceDistance(), 480.0f);
	TestEqual(TEXT("Effective trace distance reflects the updated base value"), TraceComponent->GetTraceDistance(), 480.0f);

	TraceComponent->SetUseThirdPersonTraceDistance(true, false);
	TestTrue(TEXT("Third-person trace distance mode can be enabled without tracing"), TraceComponent->IsUsingThirdPersonTraceDistance());
	TestEqual(TEXT("Effective trace distance includes the third-person bonus when enabled"), TraceComponent->GetTraceDistance(), 630.0f);

	TraceComponent->SetUseThirdPersonTraceDistance(false, false);
	TestFalse(TEXT("Third-person trace distance mode can be disabled without tracing"), TraceComponent->IsUsingThirdPersonTraceDistance());
	TestEqual(TEXT("Effective trace distance returns to the base value when disabled"), TraceComponent->GetTraceDistance(), 480.0f);

	TraceComponent->SetTraceDistance(-20.0f, false);
	TestEqual(TEXT("Negative trace distances clamp back to zero"), TraceComponent->GetBaseTraceDistance(), 0.0f);
	TestEqual(TEXT("Effective trace distance also clamps to zero"), TraceComponent->GetTraceDistance(), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionDamageTraceInteractionDefaultStateTest,
	"PorismExtension.DamageTraceInteraction.Component.DefaultStateRejectsDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionDamageTraceInteractionDefaultStateTest::RunTest(const FString& Parameters)
{
	AActor* Owner = NewObject<AActor>();
	UPorismHealthInteractionComponent* DamageTraceComponent = NewObject<UPorismHealthInteractionComponent>(Owner);
	TestNotNull(TEXT("Damage trace interaction component is created"), DamageTraceComponent);

	TestFalse(TEXT("Fresh damage trace components start without an active damage block interaction"), DamageTraceComponent->HasActiveDamageBlockInteraction());
	TestEqual(TEXT("Fresh damage trace components expose an empty damage payload"), DamageTraceComponent->GetLastDamageBlockInteractionResult().CurrentHealth, 0);

	return true;
}
