// Copyright 2026 Spotted Loaf Studio

#include "Actor/Components/ChunkWorldReadinessFreezeComponent.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Spawn/Support/ChunkWorldSpawnTestWorld.h"
#include "ChunkWorld/Support/ChunkWorldReadinessTestTypes.h"
#include "GameFramework/Controller.h"
#include "Layout/Testing/LayoutTestWorldSupport.h"
#include "Misc/AutomationTest.h"

namespace
{
	/** Owns transient authority actors for focused runtime-readiness contract coverage. */
	struct FRuntimeReadinessTestContext
	{
		UWorld* World = nullptr;
		FWorldContext* WorldContext = nullptr;
		AChunkWorldExtended* ChunkWorld = nullptr;
		AChunkWorldReadinessTestPawn* Pawn = nullptr;
		AChunkWorldReadinessTestController* Controller = nullptr;

		~FRuntimeReadinessTestContext()
		{
			FLayoutTestWorldSupport::ShutdownTransientChunkWorld(ChunkWorld);
			ChunkWorldSpawnTest::DestroyWorld(World, WorldContext);
		}

		bool Initialize()
		{
			World = ChunkWorldSpawnTest::CreateWorld(WorldContext);
			if (World == nullptr)
			{
				return false;
			}

			FActorSpawnParameters SpawnParameters;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ChunkWorld = World->SpawnActor<AChunkWorldExtended>(AChunkWorldExtended::StaticClass(), FTransform::Identity, SpawnParameters);
			Pawn = World->SpawnActor<AChunkWorldReadinessTestPawn>(AChunkWorldReadinessTestPawn::StaticClass(), FTransform(FVector(0.0, 0.0, 100.0)), SpawnParameters);
			Controller = World->SpawnActor<AChunkWorldReadinessTestController>(AChunkWorldReadinessTestController::StaticClass(), FTransform::Identity, SpawnParameters);
			if (ChunkWorld == nullptr || Pawn == nullptr || Controller == nullptr)
			{
				return false;
			}

			FLayoutTestWorldSupport::InitializeTransientChunkWorld(ChunkWorld);
			if (!ChunkWorld->IsRunning())
			{
				return false;
			}

			Controller->Possess(Pawn);
			ChunkWorld->AddChunkWorldWalker(Pawn->GetTestWalker());
			return Pawn->GetTestFreeze() != nullptr && ChunkWorld->HasRegisteredChunkWorldWalker(Pawn->GetTestWalker());
		}
	};

	FChunkWorldSpawnRegion MakeRegion()
	{
		FChunkWorldSpawnRegion Region;
		Region.Minimum = FVector(-100.0, -100.0, -100.0);
		Region.Maximum = FVector(100.0, 100.0, 1000.0);
		return Region;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldRuntimeReadinessContractDefaultsTest,
	"PorismExtension.ChunkWorld.ReadinessFreeze.Runtime.Contracts.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies runtime readiness starts inert and rejects unavailable authority/world context without releasing anything. */
bool FChunkWorldRuntimeReadinessContractDefaultsTest::RunTest(const FString& Parameters)
{
	FChunkWorldRuntimeReadinessSession Session;
	TestFalse(TEXT("Default runtime session has no id"), Session.SessionId.IsValid());
	TestEqual(TEXT("Default runtime session is inactive"), Session.State, EChunkWorldRuntimeReadinessState::Inactive);
	TestEqual(TEXT("Default runtime session has no failure"), Session.Failure, EChunkWorldRuntimeReadinessFailure::None);

	UChunkWorldReadinessFreezeComponent* const Freeze = NewObject<UChunkWorldReadinessFreezeComponent>(GetTransientPackage());
	TestNotNull(TEXT("Runtime freeze component constructs"), Freeze);
	if (Freeze == nullptr)
	{
		return false;
	}

	TestFalse(TEXT("No owner/authority context rejects runtime session start"), Freeze->StartRuntimeReadinessSession(nullptr, MakeRegion(), true).IsValid());
	TestEqual(TEXT("Rejected start leaves session inactive"), Freeze->GetRuntimeReadinessSession().State, EChunkWorldRuntimeReadinessState::Inactive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldRuntimeReadinessInvalidAcknowledgementTest,
	"PorismExtension.ChunkWorld.ReadinessFreeze.Runtime.InvalidAcknowledgement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies an invalid player acknowledgement fails exactly one active server session without releasing its freeze. */
bool FChunkWorldRuntimeReadinessInvalidAcknowledgementTest::RunTest(const FString& Parameters)
{
	FRuntimeReadinessTestContext Context;
	if (!TestTrue(TEXT("Transient runtime-readiness authority world initializes"), Context.Initialize()))
	{
		return false;
	}

	UChunkWorldReadinessTestFreezeComponent* const Freeze = Context.Pawn->GetTestFreeze();
	const FGuid SessionId = Freeze->StartRuntimeReadinessSession(Context.ChunkWorld, MakeRegion(), true);
	TestTrue(TEXT("Authority starts a bounded player session"), SessionId.IsValid());
	TestEqual(TEXT("Session binds exact registered owner walker"), Freeze->GetRuntimeReadinessSession().TrackedWalker.Get(), static_cast<UObject*>(Context.Pawn->GetTestWalker()));
	TestFalse(TEXT("Wrong session acknowledgement is rejected without poisoning active session"), Freeze->AcknowledgeOwningClientRuntimeReady(Context.Controller, FGuid::NewGuid(), Freeze->GetRuntimeReadinessSession().ReadinessId));
	TestEqual(TEXT("Wrong session leaves active session waiting"), Freeze->GetRuntimeReadinessSession().State, EChunkWorldRuntimeReadinessState::WaitingForServerReady);
	TestFalse(TEXT("Operation id cannot stand in for the readiness receipt"),
		Freeze->AcknowledgeOwningClientRuntimeReady(Context.Controller, SessionId, SessionId));
	TestEqual(TEXT("Wrong readiness receipt leaves active session waiting"), Freeze->GetRuntimeReadinessSession().State, EChunkWorldRuntimeReadinessState::WaitingForServerReady);
	TestFalse(TEXT("Invalid matching acknowledgement is rejected"), Freeze->AcknowledgeOwningClientRuntimeReady(nullptr, SessionId, Freeze->GetRuntimeReadinessSession().ReadinessId));
	const FChunkWorldRuntimeReadinessSession FailedSession = Freeze->GetRuntimeReadinessSession();
	TestEqual(TEXT("Invalid acknowledgement records terminal failure"), FailedSession.State, EChunkWorldRuntimeReadinessState::Failed);
	TestEqual(TEXT("Invalid acknowledgement preserves structured cause"), FailedSession.Failure, EChunkWorldRuntimeReadinessFailure::InvalidClientAcknowledgement);
	TestTrue(TEXT("Repeated acknowledgement for terminal session is idempotent"), Freeze->AcknowledgeOwningClientRuntimeReady(Context.Controller, SessionId, Freeze->GetRuntimeReadinessSession().ReadinessId));
	TestEqual(TEXT("Terminal failure remains frozen rather than releasing"), Freeze->GetRuntimeReadinessSession().State, EChunkWorldRuntimeReadinessState::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldRuntimeReadinessServerSettlementTest,
	"PorismExtension.ChunkWorld.ReadinessFreeze.Runtime.ServerSettlement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies one server-only ready event performs one bounded re-anchor and releases exactly once. */
bool FChunkWorldRuntimeReadinessServerSettlementTest::RunTest(const FString& Parameters)
{
	FRuntimeReadinessTestContext Context;
	if (!TestTrue(TEXT("Transient runtime-readiness authority world initializes"), Context.Initialize()))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AChunkWorldReadinessTestSurface* Surface = Context.World->SpawnActor<AChunkWorldReadinessTestSurface>(
		AChunkWorldReadinessTestSurface::StaticClass(),
		FTransform(FVector(0.0f, 0.0f, 0.0f)),
		SpawnParameters);
	TestNotNull(TEXT("Bounded settlement surface spawns"), Surface);
	if (Surface == nullptr)
	{
		return false;
	}

	UChunkWorldReadinessTestFreezeComponent* const Freeze = Context.Pawn->GetTestFreeze();
	Freeze->ApplyStartupFreeze();
	TestTrue(TEXT("Startup freeze is active before runtime settlement"), Freeze->IsStartupFreezeActive());
	const FGuid SessionId = Freeze->StartRuntimeReadinessSession(Context.ChunkWorld, MakeRegion(), false);
	TestTrue(TEXT("Authority starts server-only session"), SessionId.IsValid());
	Freeze->EmitRuntimeReady(Context.ChunkWorld, Context.Pawn->GetTestWalker(), Freeze->GetRuntimeReadinessSession().ReadinessId);
	const FChunkWorldRuntimeReadinessSession SettledSession = Freeze->GetRuntimeReadinessSession();
	TestEqual(TEXT("One ready event settles server-only session"), SettledSession.State, EChunkWorldRuntimeReadinessState::Settled);
	TestTrue(TEXT("Server stores re-anchored transform"), SettledSession.SettledTransform.GetLocation().Z > 0.0f);
	TestTrue(TEXT("Owner applies server settled transform"), Context.Pawn->GetActorLocation().Equals(SettledSession.SettledTransform.GetLocation()));
	TestFalse(TEXT("Runtime settlement releases lingering startup freeze"), Freeze->IsStartupFreezeActive());
	Freeze->EmitRuntimeReady(Context.ChunkWorld, Context.Pawn->GetTestWalker(), Freeze->GetRuntimeReadinessSession().ReadinessId);
	TestEqual(TEXT("Duplicate ready event cannot reopen or release twice"), Freeze->GetRuntimeReadinessSession().State, EChunkWorldRuntimeReadinessState::Settled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldRuntimeReadinessClientAcknowledgementTest,
	"PorismExtension.ChunkWorld.ReadinessFreeze.Runtime.ClientAcknowledgement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies server waits for matching controller acknowledgement before player settlement. */
bool FChunkWorldRuntimeReadinessClientAcknowledgementTest::RunTest(const FString& Parameters)
{
	FRuntimeReadinessTestContext Context;
	if (!TestTrue(TEXT("Transient runtime-readiness authority world initializes"), Context.Initialize()))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Context.World->SpawnActor<AChunkWorldReadinessTestSurface>(AChunkWorldReadinessTestSurface::StaticClass(), FTransform::Identity, SpawnParameters);
	UChunkWorldReadinessTestFreezeComponent* const Freeze = Context.Pawn->GetTestFreeze();
	const FGuid SessionId = Freeze->StartRuntimeReadinessSession(Context.ChunkWorld, MakeRegion(), true);
	TestTrue(TEXT("Authority starts client-acknowledged player session"), SessionId.IsValid());
	Freeze->EmitRuntimeReady(Context.ChunkWorld, Context.Pawn->GetTestWalker(), Freeze->GetRuntimeReadinessSession().ReadinessId);
	TestEqual(TEXT("Server remains frozen while waiting for client acknowledgement"), Freeze->GetRuntimeReadinessSession().State, EChunkWorldRuntimeReadinessState::WaitingForClientReady);
	TestTrue(TEXT("Matching possessed controller acknowledgement succeeds"), Freeze->AcknowledgeOwningClientRuntimeReady(Context.Controller, SessionId, Freeze->GetRuntimeReadinessSession().ReadinessId));
	TestEqual(TEXT("Server settles only after matching acknowledgement"), Freeze->GetRuntimeReadinessSession().State, EChunkWorldRuntimeReadinessState::Settled);
	TestTrue(TEXT("Duplicate valid acknowledgement is idempotent after settlement"), Freeze->AcknowledgeOwningClientRuntimeReady(Context.Controller, SessionId, Freeze->GetRuntimeReadinessSession().ReadinessId));
	TestEqual(TEXT("Duplicate valid acknowledgement cannot reopen settled session"), Freeze->GetRuntimeReadinessSession().State, EChunkWorldRuntimeReadinessState::Settled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldRuntimeReadinessInvalidTraceProfileTest,
	"PorismExtension.ChunkWorld.ReadinessFreeze.Runtime.InvalidTraceProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies an invalid configured trace profile reports structured failure without release. */
bool FChunkWorldRuntimeReadinessInvalidTraceProfileTest::RunTest(const FString& Parameters)
{
	FRuntimeReadinessTestContext Context;
	if (!TestTrue(TEXT("Transient runtime-readiness authority world initializes"), Context.Initialize()))
	{
		return false;
	}

	UChunkWorldReadinessTestFreezeComponent* const Freeze = Context.Pawn->GetTestFreeze();
	Freeze->SetSettlementCollisionProfile(TEXT("MissingRuntimeSettlementProfile"));
	const FGuid SessionId = Freeze->StartRuntimeReadinessSession(Context.ChunkWorld, MakeRegion(), false);
	TestTrue(TEXT("Authority starts invalid-profile session"), SessionId.IsValid());
	Freeze->EmitRuntimeReady(Context.ChunkWorld, Context.Pawn->GetTestWalker(), Freeze->GetRuntimeReadinessSession().ReadinessId);
	TestEqual(TEXT("Invalid profile retains structured trace failure"), Freeze->GetRuntimeReadinessSession().Failure, EChunkWorldRuntimeReadinessFailure::InvalidTraceProfile);
	TestEqual(TEXT("Invalid profile retains terminal frozen state"), Freeze->GetRuntimeReadinessSession().State, EChunkWorldRuntimeReadinessState::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldRuntimeReadinessNoSurfaceTest,
	"PorismExtension.ChunkWorld.ReadinessFreeze.Runtime.NoSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies no bounded blocking surface produces structured failure while retaining freeze. */
bool FChunkWorldRuntimeReadinessNoSurfaceTest::RunTest(const FString& Parameters)
{
	FRuntimeReadinessTestContext Context;
	if (!TestTrue(TEXT("Transient runtime-readiness authority world initializes"), Context.Initialize()))
	{
		return false;
	}

	UChunkWorldReadinessTestFreezeComponent* const Freeze = Context.Pawn->GetTestFreeze();
	const FGuid SessionId = Freeze->StartRuntimeReadinessSession(Context.ChunkWorld, MakeRegion(), false);
	TestTrue(TEXT("Authority starts no-surface session"), SessionId.IsValid());
	Freeze->EmitRuntimeReady(Context.ChunkWorld, Context.Pawn->GetTestWalker(), Freeze->GetRuntimeReadinessSession().ReadinessId);
	TestEqual(TEXT("No surface retains structured failure"), Freeze->GetRuntimeReadinessSession().Failure, EChunkWorldRuntimeReadinessFailure::NoSettledSurface);
	TestEqual(TEXT("No surface retains freeze terminal state"), Freeze->GetRuntimeReadinessSession().State, EChunkWorldRuntimeReadinessState::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldRuntimeReadinessTimeoutTest,
	"PorismExtension.ChunkWorld.ReadinessFreeze.Runtime.ClientAcknowledgementTimeout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies required client acknowledgement timeout fails once without releasing freeze. */
bool FChunkWorldRuntimeReadinessTimeoutTest::RunTest(const FString& Parameters)
{
	FRuntimeReadinessTestContext Context;
	if (!TestTrue(TEXT("Transient runtime-readiness authority world initializes"), Context.Initialize()))
	{
		return false;
	}

	UChunkWorldReadinessTestFreezeComponent* const Freeze = Context.Pawn->GetTestFreeze();
	Freeze->SetClientReadyTimeout(0.0f);
	const FGuid SessionId = Freeze->StartRuntimeReadinessSession(Context.ChunkWorld, MakeRegion(), true);
	TestTrue(TEXT("Authority starts timeout session"), SessionId.IsValid());
	Freeze->EmitRuntimeReady(Context.ChunkWorld, Context.Pawn->GetTestWalker(), Freeze->GetRuntimeReadinessSession().ReadinessId);
	Freeze->Advance();
	TestEqual(TEXT("Timeout records structured failure"), Freeze->GetRuntimeReadinessSession().Failure, EChunkWorldRuntimeReadinessFailure::ClientAcknowledgementTimeout);
	TestEqual(TEXT("Timeout retains terminal frozen state"), Freeze->GetRuntimeReadinessSession().State, EChunkWorldRuntimeReadinessState::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldRuntimeReadinessWorldTeardownTest,
	"PorismExtension.ChunkWorld.ReadinessFreeze.Runtime.WorldTeardown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies chunk-world teardown fails active runtime readiness while retaining freeze. */
bool FChunkWorldRuntimeReadinessWorldTeardownTest::RunTest(const FString& Parameters)
{
	FRuntimeReadinessTestContext Context;
	if (!TestTrue(TEXT("Transient runtime-readiness authority world initializes"), Context.Initialize()))
	{
		return false;
	}

	UChunkWorldReadinessTestFreezeComponent* const Freeze = Context.Pawn->GetTestFreeze();
	const FGuid SessionId = Freeze->StartRuntimeReadinessSession(Context.ChunkWorld, MakeRegion(), false);
	TestTrue(TEXT("Authority starts teardown session"), SessionId.IsValid());
	Context.ChunkWorld->Destroy();
	Freeze->Advance();
	TestEqual(TEXT("Destroyed chunk world records terminal runtime failure"), Freeze->GetRuntimeReadinessSession().State, EChunkWorldRuntimeReadinessState::Failed);
	TestEqual(TEXT("Destroyed chunk world preserves structured failure"), Freeze->GetRuntimeReadinessSession().Failure, EChunkWorldRuntimeReadinessFailure::WorldTornDown);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldRuntimeReadinessCancelTest,
	"PorismExtension.ChunkWorld.ReadinessFreeze.Runtime.Cancel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies cancellation tears down tracked-walker state and retains freeze as terminal gameplay-visible state. */
bool FChunkWorldRuntimeReadinessCancelTest::RunTest(const FString& Parameters)
{
	FRuntimeReadinessTestContext Context;
	if (!TestTrue(TEXT("Transient runtime-readiness authority world initializes"), Context.Initialize()))
	{
		return false;
	}

	UChunkWorldReadinessTestFreezeComponent* const Freeze = Context.Pawn->GetTestFreeze();
	const FGuid SessionId = Freeze->StartRuntimeReadinessSession(Context.ChunkWorld, MakeRegion(), false);
	TestTrue(TEXT("Authority starts server-only session"), SessionId.IsValid());
	TestTrue(TEXT("Matching session cancels"), Freeze->CancelRuntimeReadinessSession(SessionId));
	TestEqual(TEXT("Canceled session stays terminal"), Freeze->GetRuntimeReadinessSession().State, EChunkWorldRuntimeReadinessState::Canceled);
	TestFalse(TEXT("Duplicate cancellation cannot create another terminal event"), Freeze->CancelRuntimeReadinessSession(SessionId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldRuntimeReadinessWalkerRebindTest,
	"PorismExtension.ChunkWorld.ReadinessFreeze.Runtime.LateWalkerRebind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies an authority session waits through late walker registration without silently rebinding to another walker. */
bool FChunkWorldRuntimeReadinessWalkerRebindTest::RunTest(const FString& Parameters)
{
	FRuntimeReadinessTestContext Context;
	if (!TestTrue(TEXT("Transient runtime-readiness authority world initializes"), Context.Initialize()))
	{
		return false;
	}

	UChunkWorldReadinessTestFreezeComponent* const Freeze = Context.Pawn->GetTestFreeze();
	Context.ChunkWorld->RemoveChunkWorldWalker(Context.Pawn->GetTestWalker());
	const FGuid SessionId = Freeze->StartRuntimeReadinessSession(Context.ChunkWorld, MakeRegion(), false);
	TestTrue(TEXT("Authority accepts late-walker session"), SessionId.IsValid());
	TestEqual(TEXT("Session waits for missing exact walker"), Freeze->GetRuntimeReadinessSession().State, EChunkWorldRuntimeReadinessState::WaitingForWalker);
	Context.ChunkWorld->AddChunkWorldWalker(Context.Pawn->GetTestWalker());
	Freeze->Advance();
	TestEqual(TEXT("Late registration binds only owner walker"), Freeze->GetRuntimeReadinessSession().TrackedWalker.Get(), static_cast<UObject*>(Context.Pawn->GetTestWalker()));
	TestEqual(TEXT("Late registration resumes readiness tracking"), Freeze->GetRuntimeReadinessSession().State, EChunkWorldRuntimeReadinessState::WaitingForServerReady);
	TestTrue(TEXT("Late-walker session remains cancelable"), Freeze->CancelRuntimeReadinessSession(SessionId));
	return true;
}
