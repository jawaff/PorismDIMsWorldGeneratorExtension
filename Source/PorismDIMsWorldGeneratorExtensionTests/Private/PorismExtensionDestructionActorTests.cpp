// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldChaosDestructionPresentationActor.h"
#include "ChunkWorld/Actors/ChunkWorldChaosGentleReleaseActor.h"
#include "ChunkWorld/Actors/ChunkWorldTimedCleanupDestructionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionTimedCleanupFrameworkTriggerTest,
	"PorismExtension.DestructionActors.TimedCleanup.FrameworkTriggerIsIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionTimedCleanupFrameworkTriggerTest::RunTest(const FString& Parameters)
{
	AChunkWorldTimedCleanupDestructionActor* DestructionActor = NewObject<AChunkWorldTimedCleanupDestructionActor>();
	TestNotNull(TEXT("Timed cleanup destruction actor is created"), DestructionActor);

	const FChunkWorldBlockDestructionRequest InitialRequest = []()
	{
		FChunkWorldBlockDestructionRequest Request;
		Request.BlockWorldPos = FIntVector(4, 5, 6);
		Request.SpawnTransform = FTransform(FRotator::ZeroRotator, FVector(100.0f, 200.0f, 300.0f));
		Request.RepresentativeWorldPos = FVector(110.0f, 210.0f, 310.0f);
		return Request;
	}();

	DestructionActor->ExecuteFrameworkDestructionTrigger(InitialRequest);

	TestEqual(
		TEXT("Timed cleanup framework trigger records the first request block position"),
		DestructionActor->GetLastDestructionRequest().BlockWorldPos,
		InitialRequest.BlockWorldPos);
	TestTrue(
		TEXT("Timed cleanup framework trigger moves the actor to the destruction transform"),
		DestructionActor->GetActorLocation().Equals(InitialRequest.SpawnTransform.GetLocation()));

	FChunkWorldBlockDestructionRequest DuplicateRequest = InitialRequest;
	DuplicateRequest.BlockWorldPos = FIntVector(7, 8, 9);
	DuplicateRequest.SpawnTransform.SetLocation(FVector(-50.0f, -60.0f, -70.0f));
	DestructionActor->ExecuteFrameworkDestructionTrigger(DuplicateRequest);

	TestEqual(
		TEXT("Timed cleanup framework trigger ignores duplicate requests after the first accepted trigger"),
		DestructionActor->GetLastDestructionRequest().BlockWorldPos,
		InitialRequest.BlockWorldPos);
	TestTrue(
		TEXT("Timed cleanup actor keeps the original accepted transform after duplicate trigger attempts"),
		DestructionActor->GetActorLocation().Equals(InitialRequest.SpawnTransform.GetLocation()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionChaosFrameworkTriggerTest,
	"PorismExtension.DestructionActors.ChaosPresentation.FrameworkTriggerIsIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionChaosFrameworkTriggerTest::RunTest(const FString& Parameters)
{
	AChunkWorldChaosDestructionPresentationActor* DestructionActor = NewObject<AChunkWorldChaosDestructionPresentationActor>();
	TestNotNull(TEXT("Chaos destruction presentation actor is created"), DestructionActor);
	TestNotNull(TEXT("Chaos destruction presentation actor creates its geometry collection component"), DestructionActor->GetGeometryCollectionComponent());
	TestNotNull(TEXT("Chaos destruction presentation actor creates its field system component"), DestructionActor->GetFieldSystemComponent());

	const FChunkWorldBlockDestructionRequest InitialRequest = []()
	{
		FChunkWorldBlockDestructionRequest Request;
		Request.BlockWorldPos = FIntVector(1, 2, 3);
		Request.SpawnTransform = FTransform(FRotator::ZeroRotator, FVector(25.0f, 35.0f, 45.0f));
		Request.RepresentativeWorldPos = FVector(26.0f, 36.0f, 46.0f);
		return Request;
	}();

	DestructionActor->ExecuteFrameworkDestructionTrigger(InitialRequest);

	TestEqual(
		TEXT("Chaos framework trigger records the first request block position"),
		DestructionActor->GetLastDestructionRequest().BlockWorldPos,
		InitialRequest.BlockWorldPos);
	TestTrue(
		TEXT("Chaos framework trigger moves the actor to the destruction transform"),
		DestructionActor->GetActorLocation().Equals(InitialRequest.SpawnTransform.GetLocation()));

	FChunkWorldBlockDestructionRequest DuplicateRequest = InitialRequest;
	DuplicateRequest.BlockWorldPos = FIntVector(9, 8, 7);
	DuplicateRequest.SpawnTransform.SetLocation(FVector(75.0f, 85.0f, 95.0f));
	DestructionActor->ExecuteFrameworkDestructionTrigger(DuplicateRequest);

	TestEqual(
		TEXT("Chaos framework trigger ignores duplicate requests after the first accepted trigger"),
		DestructionActor->GetLastDestructionRequest().BlockWorldPos,
		InitialRequest.BlockWorldPos);
	TestTrue(
		TEXT("Chaos actor keeps the original accepted transform after duplicate trigger attempts"),
		DestructionActor->GetActorLocation().Equals(InitialRequest.SpawnTransform.GetLocation()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionChaosDelayedCollisionStagingPreservesPhysicsTest,
	"PorismExtension.DestructionActors.ChaosPresentation.DelayedCollisionStagingPreservesPhysicsCollisionMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionChaosDelayedCollisionStagingPreservesPhysicsTest::RunTest(const FString& Parameters)
{
	AChunkWorldChaosDestructionPresentationActor* DestructionActor = NewObject<AChunkWorldChaosDestructionPresentationActor>();
	TestNotNull(TEXT("Chaos destruction presentation actor is created"), DestructionActor);
	TestNotNull(TEXT("Chaos destruction presentation actor creates its geometry collection component"), DestructionActor->GetGeometryCollectionComponent());

	const FChunkWorldBlockDestructionRequest Request = []()
	{
		FChunkWorldBlockDestructionRequest LocalRequest;
		LocalRequest.BlockWorldPos = FIntVector(2, 4, 6);
		LocalRequest.SpawnTransform = FTransform(FRotator::ZeroRotator, FVector(10.0f, 20.0f, 30.0f));
		LocalRequest.RepresentativeWorldPos = FVector(11.0f, 21.0f, 31.0f);
		return LocalRequest;
	}();

	DestructionActor->ExecuteFrameworkDestructionTrigger(Request);

	TestEqual(
		TEXT("Delayed collision staging keeps Chaos collision in a physics-capable mode"),
		DestructionActor->GetGeometryCollectionComponent()->GetCollisionEnabled(),
		ECollisionEnabled::QueryAndPhysics);
	TestEqual(
		TEXT("Delayed collision staging ignores world static contacts until the restore step"),
		DestructionActor->GetGeometryCollectionComponent()->GetCollisionResponseToChannel(ECC_WorldStatic),
		ECR_Ignore);
	TestEqual(
		TEXT("Delayed collision staging ignores world dynamic contacts until the restore step"),
		DestructionActor->GetGeometryCollectionComponent()->GetCollisionResponseToChannel(ECC_WorldDynamic),
		ECR_Ignore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionChaosGentleReleaseDefaultsTest,
	"PorismExtension.DestructionActors.ChaosGentleRelease.DefaultsKeepFractureCapableStartup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionChaosGentleReleaseDefaultsTest::RunTest(const FString& Parameters)
{
	AChunkWorldChaosGentleReleaseActor* DestructionActor = NewObject<AChunkWorldChaosGentleReleaseActor>();
	TestNotNull(TEXT("Chaos gentle release actor is created"), DestructionActor);

	const FChunkWorldChaosDestructionPresentationTuning& Tuning = DestructionActor->GetDestructionTuning();
	TestEqual(
		TEXT("Gentle release uses uniform fracture field mode"),
		Tuning.FractureFieldMode,
		EChunkWorldChaosFractureFieldMode::Uniform);
	TestTrue(
		TEXT("Gentle release keeps delayed collision staging enabled"),
		Tuning.bDelayCollisionUntilAfterFracture);
	TestTrue(
		TEXT("Gentle release keeps temporary bottom anchor enabled"),
		Tuning.bUseTemporaryBottomAnchor);
	TestFalse(
		TEXT("Gentle release disables the optional separation impulse"),
		Tuning.bApplyGentleSeparationImpulse);

	const FChunkWorldBlockDestructionRequest Request = []()
	{
		FChunkWorldBlockDestructionRequest LocalRequest;
		LocalRequest.BlockWorldPos = FIntVector(3, 6, 9);
		LocalRequest.SpawnTransform = FTransform(FRotator::ZeroRotator, FVector(40.0f, 50.0f, 60.0f));
		LocalRequest.RepresentativeWorldPos = FVector(41.0f, 51.0f, 61.0f);
		return LocalRequest;
	}();

	DestructionActor->ExecuteFrameworkDestructionTrigger(Request);

	TestEqual(
		TEXT("Gentle release staging still preserves a physics-capable collision mode"),
		DestructionActor->GetGeometryCollectionComponent()->GetCollisionEnabled(),
		ECollisionEnabled::QueryAndPhysics);
	return true;
}
