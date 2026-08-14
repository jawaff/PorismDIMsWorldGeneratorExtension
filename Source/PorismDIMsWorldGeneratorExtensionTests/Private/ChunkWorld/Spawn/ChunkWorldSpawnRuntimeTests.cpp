// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Spawn/Support/ChunkWorldSpawnTestPropertyAccess.h"
#include "ChunkWorld/Spawn/Support/ChunkWorldSpawnTestTypes.h"
#include "ChunkWorld/Spawn/Support/ChunkWorldSpawnTestWorld.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

namespace
{
	/** Owns one transient authority world and test-local location coordinator. */
	struct FChunkWorldSpawnRuntimeTestContext
	{
		UWorld* World = nullptr;
		FWorldContext* WorldContext = nullptr;
		AChunkWorldExtended* ChunkWorld = nullptr;
		UChunkWorldSpawnTestComponent* SpawnComponent = nullptr;

		~FChunkWorldSpawnRuntimeTestContext()
		{
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
			SpawnComponent = ChunkWorld != nullptr ? NewObject<UChunkWorldSpawnTestComponent>(ChunkWorld) : nullptr;
			if (SpawnComponent != nullptr)
			{
				SpawnComponent->RegisterComponent();
			}
			return ChunkWorld != nullptr && SpawnComponent != nullptr;
		}
	};

	/** Builds a location-only direct player style request. */
	FChunkWorldSpawnRequest MakeLocationRequest()
	{
		FChunkWorldSpawnRequest Request;
		Request.RequestId = FGuid(101, 202, 303, 404);
		Request.SpawnReason = EChunkWorldSpawnReason::Respawn;
		Request.SubjectBinding.StableSubjectId = FGuid::NewGuid();
		Request.AllowedSourceFamiliesInPriorityOrder.Add(EChunkWorldSpawnSourceFamily::ExplicitFixture);
		return Request;
	}

	/** Builds one finite deterministic test candidate. */
	FChunkWorldSpawnCandidate MakeCandidate(const FName SourceId)
	{
		FChunkWorldSpawnCandidate Candidate;
		Candidate.SourceFamily = EChunkWorldSpawnSourceFamily::ExplicitFixture;
		Candidate.SourceKind = TEXT("AutomationFixture");
		Candidate.SourceId = SourceId;
		Candidate.SearchBounds = FBox(FVector(-500.0f, -500.0f, -50.0f), FVector(500.0f, 500.0f, 300.0f));
		Candidate.TheoreticalLocation = FVector(125.0f, -75.0f, 125.0f);
		Candidate.TheoreticalSurfaceZ = Candidate.TheoreticalLocation.Z;
		Candidate.DebugLabel = SourceId.ToString();
		return Candidate;
	}

	/** Wraps the controlled provider through the public registration boundary. */
	TScriptInterface<IChunkWorldSpawnSourceProvider> AsSourceProvider(UChunkWorldSpawnTestSourceProvider* Provider)
	{
		TScriptInterface<IChunkWorldSpawnSourceProvider> Interface;
		Interface.SetObject(Provider);
		Interface.SetInterface(Provider);
		return Interface;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldSpawnLocationResultTest,
	"PorismExtension.ChunkWorld.Spawn.Runtime.LocationResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies a direct request without definitions publishes one location-only result exactly once. */
bool FChunkWorldSpawnLocationResultTest::RunTest(const FString& Parameters)
{
	FChunkWorldSpawnRuntimeTestContext Context;
	if (!TestTrue(TEXT("Transient authority world initializes"), Context.Initialize()))
	{
		return false;
	}

	UChunkWorldSpawnTestSourceProvider* const Provider = NewObject<UChunkWorldSpawnTestSourceProvider>(Context.ChunkWorld);
	Provider->Candidates.Add(MakeCandidate(TEXT("LocationOnly")));
	if (!TestTrue(TEXT("Registers controlled provider"), Context.SpawnComponent->RegisterSpawnSourceProvider(AsSourceProvider(Provider))))
	{
		return false;
	}

	int32 EventCount = 0;
	FChunkWorldSpawnResult PublishedResult;
	Context.SpawnComponent->OnSpawnResolvedNative.AddLambda([&EventCount, &PublishedResult](const FChunkWorldSpawnResult& Result)
	{
		++EventCount;
		PublishedResult = Result;
	});

	const FChunkWorldSpawnTicketHandle Ticket = Context.SpawnComponent->SubmitSpawnRequest(MakeLocationRequest());
	Context.SpawnComponent->Advance();
	Context.SpawnComponent->Advance();

	FChunkWorldSpawnResult PolledResult;
	TestTrue(TEXT("Direct request receives a ticket"), Ticket.IsValid());
	TestEqual(TEXT("Approved location remains a ready ticket"), Context.SpawnComponent->GetSpawnTicketState(Ticket), EChunkWorldSpawnTicketState::Ready);
	TestTrue(TEXT("Ready ticket remains queryable"), Context.SpawnComponent->GetSpawnTicketResult(Ticket, PolledResult));
	TestTrue(TEXT("Result reports successful location approval"), PolledResult.bSuccess);
	TestTrue(TEXT("Direct player request selects no NPC definition"), !PolledResult.SelectedSubjectDefinition.IsValid());
	TestEqual(TEXT("Location event emits exactly once"), EventCount, 1);
	TestEqual(TEXT("Event preserves request identity"), PublishedResult.RequestId, PolledResult.RequestId);
	TestTrue(TEXT("Event preserves finite handoff region"), PublishedResult.ApprovedCandidateRegion.Minimum.Equals(Provider->Candidates[0].SearchBounds.Min) && PublishedResult.ApprovedCandidateRegion.Maximum.Equals(Provider->Candidates[0].SearchBounds.Max));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldSpawnCacheInvalidationTest,
	"PorismExtension.ChunkWorld.Spawn.Runtime.CacheInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies cache-safe identical location queries reuse candidates until provider registration changes. */
bool FChunkWorldSpawnCacheInvalidationTest::RunTest(const FString& Parameters)
{
	FChunkWorldSpawnRuntimeTestContext Context;
	if (!TestTrue(TEXT("Transient authority world initializes"), Context.Initialize())
		|| !TestTrue(TEXT("Test configures candidate cache lifetime"), ChunkWorldSpawnTest::SetSpawnComponentFloatProperty(*Context.SpawnComponent, TEXT("CandidateQueryCacheSeconds"), 60.0f)))
	{
		return false;
	}

	UChunkWorldSpawnTestSourceProvider* const Provider = NewObject<UChunkWorldSpawnTestSourceProvider>(Context.ChunkWorld);
	Provider->bCacheable = true;
	Provider->Candidates.Add(MakeCandidate(TEXT("Cached")));
	if (!TestTrue(TEXT("Registers cache-safe provider"), Context.SpawnComponent->RegisterSpawnSourceProvider(AsSourceProvider(Provider))))
	{
		return false;
	}

	const FChunkWorldSpawnTicketHandle FirstTicket = Context.SpawnComponent->SubmitSpawnRequest(MakeLocationRequest());
	Context.SpawnComponent->Advance();
	const FChunkWorldSpawnTicketHandle SecondTicket = Context.SpawnComponent->SubmitSpawnRequest(MakeLocationRequest());
	Context.SpawnComponent->Advance();
	TestEqual(TEXT("First identical request resolves"), Context.SpawnComponent->GetSpawnTicketState(FirstTicket), EChunkWorldSpawnTicketState::Ready);
	TestEqual(TEXT("Second identical request resolves"), Context.SpawnComponent->GetSpawnTicketState(SecondTicket), EChunkWorldSpawnTicketState::Ready);
	TestEqual(TEXT("Cache-safe identical query gathers once"), Provider->GatherCallCount, 1);

	TestTrue(TEXT("Provider unregisters"), Context.SpawnComponent->UnregisterSpawnSourceProvider(AsSourceProvider(Provider)));
	TestTrue(TEXT("Provider re-registers"), Context.SpawnComponent->RegisterSpawnSourceProvider(AsSourceProvider(Provider)));
	const FChunkWorldSpawnTicketHandle InvalidatedTicket = Context.SpawnComponent->SubmitSpawnRequest(MakeLocationRequest());
	Context.SpawnComponent->Advance();
	TestEqual(TEXT("Provider registration invalidates cache"), Provider->GatherCallCount, 2);
	TestEqual(TEXT("Registration-invalidated request resolves"), Context.SpawnComponent->GetSpawnTicketState(InvalidatedTicket), EChunkWorldSpawnTicketState::Ready);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldSpawnProviderReentrancyTest,
	"PorismExtension.ChunkWorld.Spawn.Runtime.ProviderReentrancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies a provider callback may cancel its pending ticket without a location event. */
bool FChunkWorldSpawnProviderReentrancyTest::RunTest(const FString& Parameters)
{
	FChunkWorldSpawnRuntimeTestContext Context;
	if (!TestTrue(TEXT("Transient authority world initializes"), Context.Initialize()))
	{
		return false;
	}

	UChunkWorldSpawnTestSourceProvider* const Provider = NewObject<UChunkWorldSpawnTestSourceProvider>(Context.ChunkWorld);
	Provider->Candidates.Add(MakeCandidate(TEXT("Reentrant")));
	if (!TestTrue(TEXT("Registers controlled provider"), Context.SpawnComponent->RegisterSpawnSourceProvider(AsSourceProvider(Provider))))
	{
		return false;
	}

	const FChunkWorldSpawnTicketHandle Ticket = Context.SpawnComponent->SubmitSpawnRequest(MakeLocationRequest());
	Provider->ReentrantComponent = Context.SpawnComponent;
	Provider->ReentrantTicketId = Ticket.TicketId;
	Provider->bCancelTicketDuringGather = true;
	int32 EventCount = 0;
	Context.SpawnComponent->OnSpawnResolvedNative.AddLambda([&EventCount](const FChunkWorldSpawnResult&) { ++EventCount; });
	Context.SpawnComponent->Advance();

	TestEqual(TEXT("Provider cancellation settles ticket"), Context.SpawnComponent->GetSpawnTicketState(Ticket), EChunkWorldSpawnTicketState::Canceled);
	TestEqual(TEXT("Canceled ticket does not publish a location"), EventCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldSpawnPriorityFallbackTest,
	"PorismExtension.ChunkWorld.Spawn.Runtime.PriorityFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies lower-priority source families remain unavailable unless the direct request explicitly permits fallback. */
bool FChunkWorldSpawnPriorityFallbackTest::RunTest(const FString& Parameters)
{
	FChunkWorldSpawnRuntimeTestContext Context;
	if (!TestTrue(TEXT("Transient authority world initializes"), Context.Initialize()))
	{
		return false;
	}

	UChunkWorldSpawnTestSourceProvider* const PreferredProvider = NewObject<UChunkWorldSpawnTestSourceProvider>(Context.ChunkWorld);
	UChunkWorldSpawnTestSourceProvider* const FallbackProvider = NewObject<UChunkWorldSpawnTestSourceProvider>(Context.ChunkWorld);
	PreferredProvider->SourceFamily = EChunkWorldSpawnSourceFamily::AnchorRegion;
	FallbackProvider->SourceFamily = EChunkWorldSpawnSourceFamily::ExplicitFixture;
	FallbackProvider->Candidates.Add(MakeCandidate(TEXT("Fallback")));
	if (!TestTrue(TEXT("Registers empty higher-priority provider"), Context.SpawnComponent->RegisterSpawnSourceProvider(AsSourceProvider(PreferredProvider)))
		|| !TestTrue(TEXT("Registers lower-priority provider"), Context.SpawnComponent->RegisterSpawnSourceProvider(AsSourceProvider(FallbackProvider))))
	{
		return false;
	}

	FChunkWorldSpawnRequest Request = MakeLocationRequest();
	Request.AllowedSourceFamiliesInPriorityOrder[0] = EChunkWorldSpawnSourceFamily::AnchorRegion;
	Request.AllowedSourceFamiliesInPriorityOrder.Add(EChunkWorldSpawnSourceFamily::ExplicitFixture);
	AddExpectedError(TEXT("Failure=NoEligibleSource"));
	const FChunkWorldSpawnTicketHandle StrictTicket = Context.SpawnComponent->SubmitSpawnRequest(Request);
	Context.SpawnComponent->Advance();
	TestEqual(TEXT("Empty higher-priority family fails without fallback"), Context.SpawnComponent->GetSpawnTicketState(StrictTicket), EChunkWorldSpawnTicketState::Failed);
	TestEqual(TEXT("Lower-priority provider is not queried without fallback"), FallbackProvider->GatherCallCount, 0);

	Request.bAllowLowerPrioritySourceFallback = true;
	const FChunkWorldSpawnTicketHandle FallbackTicket = Context.SpawnComponent->SubmitSpawnRequest(Request);
	Context.SpawnComponent->Advance();
	FChunkWorldSpawnResult Result;
	TestEqual(TEXT("Explicit fallback resolves a location"), Context.SpawnComponent->GetSpawnTicketState(FallbackTicket), EChunkWorldSpawnTicketState::Ready);
	TestTrue(TEXT("Fallback result remains queryable"), Context.SpawnComponent->GetSpawnTicketResult(FallbackTicket, Result));
	TestEqual(TEXT("Fallback result identifies lower-priority source family"), Result.SelectedSourceFamily, EChunkWorldSpawnSourceFamily::ExplicitFixture);
	TestEqual(TEXT("Lower-priority provider is queried only after fallback is enabled"), FallbackProvider->GatherCallCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldSpawnResolvedEventReentrancyTest,
	"PorismExtension.ChunkWorld.Spawn.Runtime.ResolvedEventReentrancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies a location-result listener may submit one follow-up request without duplicate delivery or stale ticket access. */
bool FChunkWorldSpawnResolvedEventReentrancyTest::RunTest(const FString& Parameters)
{
	FChunkWorldSpawnRuntimeTestContext Context;
	if (!TestTrue(TEXT("Transient authority world initializes"), Context.Initialize()))
	{
		return false;
	}

	UChunkWorldSpawnTestSourceProvider* const Provider = NewObject<UChunkWorldSpawnTestSourceProvider>(Context.ChunkWorld);
	Provider->Candidates.Add(MakeCandidate(TEXT("ReentrantResult")));
	if (!TestTrue(TEXT("Registers controlled provider"), Context.SpawnComponent->RegisterSpawnSourceProvider(AsSourceProvider(Provider))))
	{
		return false;
	}

	int32 EventCount = 0;
	FChunkWorldSpawnTicketHandle FollowUpTicket;
	Context.SpawnComponent->OnSpawnResolvedNative.AddLambda([&Context, &EventCount, &FollowUpTicket](const FChunkWorldSpawnResult& Result)
	{
		++EventCount;
		if (EventCount == 1)
		{
			FollowUpTicket = Context.SpawnComponent->SubmitSpawnRequest(MakeLocationRequest());
		}
	});

	const FChunkWorldSpawnTicketHandle InitialTicket = Context.SpawnComponent->SubmitSpawnRequest(MakeLocationRequest());
	Context.SpawnComponent->Advance();
	Context.SpawnComponent->Advance();
	TestEqual(TEXT("Initial ticket resolves once"), Context.SpawnComponent->GetSpawnTicketState(InitialTicket), EChunkWorldSpawnTicketState::Ready);
	TestTrue(TEXT("Result listener may submit follow-up ticket"), FollowUpTicket.IsValid());
	TestEqual(TEXT("Follow-up ticket resolves on its own subsequent update"), Context.SpawnComponent->GetSpawnTicketState(FollowUpTicket), EChunkWorldSpawnTicketState::Ready);
	TestEqual(TEXT("Each resolved ticket broadcasts exactly once"), EventCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldSpawnExplicitActorTrackingTest,
	"PorismExtension.ChunkWorld.Spawn.Runtime.ExplicitActorTracking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies game-created actors opt into idempotent generic KillZ observation explicitly. */
bool FChunkWorldSpawnExplicitActorTrackingTest::RunTest(const FString& Parameters)
{
	FChunkWorldSpawnRuntimeTestContext Context;
	if (!TestTrue(TEXT("Transient authority world initializes"), Context.Initialize()))
	{
		return false;
	}

	Context.World->GetWorldSettings()->KillZ = 0.0f;
	AChunkWorldSpawnTestActor* const Actor = Context.World->SpawnActor<AChunkWorldSpawnTestActor>(AChunkWorldSpawnTestActor::StaticClass(), FTransform(FVector(0.0f, 0.0f, 100.0f)));
	if (!TestNotNull(TEXT("Tracked actor initializes"), Actor))
	{
		return false;
	}

	int32 EventCount = 0;
	Context.SpawnComponent->OnSubjectOutOfWorld.AddLambda([&EventCount](const FChunkWorldOutOfWorldEvent&) { ++EventCount; });
	TestTrue(TEXT("Game registers actor"), Context.SpawnComponent->RegisterTrackedSpawnActor(Actor));
	TestTrue(TEXT("Repeated registration is idempotent"), Context.SpawnComponent->RegisterTrackedSpawnActor(Actor));
	Actor->SetActorLocation(FVector(0.0f, 0.0f, -100.0f));
	Context.SpawnComponent->Advance();
	Context.SpawnComponent->Advance();
	TestEqual(TEXT("One crossing emits one event"), EventCount, 1);
	TestTrue(TEXT("Game unregisters actor"), Context.SpawnComponent->UnregisterTrackedSpawnActor(Actor));
	TestFalse(TEXT("Repeated unregister reports no registration"), Context.SpawnComponent->UnregisterTrackedSpawnActor(Actor));
	return true;
}
