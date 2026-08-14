// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Spawn/ChunkWorldSpawnComponent.h"
#include "ChunkWorld/Spawn/ChunkWorldSpawnTypes.h"
#include "ChunkWorld/Spawn/Support/ChunkWorldSpawnTestPropertyAccess.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldSpawnTypeDefaultsTest,
	"PorismExtension.ChunkWorld.Spawn.Contracts.TypeDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies the public plugin spawn types retain stable defaults and diagnostic labels. */
bool FChunkWorldSpawnTypeDefaultsTest::RunTest(const FString& Parameters)
{
	FChunkWorldSpawnRequest Request;
	FChunkWorldSpawnResult Result;
	FChunkWorldSpawnTicketHandle SpawnTicket;

	TestFalse(TEXT("Default request id is invalid until a caller or component assigns one"), Request.RequestId.IsValid());
	TestFalse(TEXT("Default result is not an approved placement"), Result.bSuccess);
	TestFalse(TEXT("Default spawn ticket is invalid"), SpawnTicket.IsValid());
	TestEqual(TEXT("Ready tickets use a stable label"), FString(ChunkWorldSpawn::ToString(EChunkWorldSpawnTicketState::Ready)), FString(TEXT("Ready")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldSpawnComponentAttachmentTest,
	"PorismExtension.ChunkWorld.Spawn.Contracts.ComponentAttachment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies the chunk-world attachment is optional, inert, and reports an explicit failure before authority is available. */
bool FChunkWorldSpawnComponentAttachmentTest::RunTest(const FString& Parameters)
{
	AChunkWorldExtended* const ChunkWorld = NewObject<AChunkWorldExtended>(GetTransientPackage());
	UChunkWorldSpawnComponent* const SpawnComponent = ChunkWorld->GetSpawnComponent();
	TestNotNull(TEXT("Chunk world attaches its optional spawn component"), SpawnComponent);
	if (SpawnComponent == nullptr)
	{
		return false;
	}

	TestFalse(TEXT("Spawn processing does not tick before a request or returned actor needs it"), SpawnComponent->IsComponentTickEnabled());

	FChunkWorldSpawnRequest Request;
	Request.AllowedSourceFamiliesInPriorityOrder.Add(EChunkWorldSpawnSourceFamily::ReservationField);
	AddExpectedError(TEXT("Failure=WorldInvalid"));
	const FChunkWorldSpawnTicketHandle Ticket = SpawnComponent->SubmitSpawnRequest(Request);
	TestTrue(TEXT("An unavailable authority context still produces a queryable failure ticket"), Ticket.IsValid());

	FChunkWorldSpawnResult Result;
	TestTrue(TEXT("Failed ticket retains an explicit result"), SpawnComponent->GetSpawnTicketResult(Ticket, Result));
	TestEqual(TEXT("Unowned transient actor rejects requests as world-invalid"), Result.FailureCategory, EChunkWorldSpawnFailureCategory::WorldInvalid);
	TestEqual(TEXT("Failed ticket has terminal failed state"), SpawnComponent->GetSpawnTicketState(Ticket), EChunkWorldSpawnTicketState::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldSpawnTicketCapacityTest,
	"PorismExtension.ChunkWorld.Spawn.Contracts.TerminalTicketCapacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies deterministic oldest-terminal eviction frees bounded record capacity before another request is accepted. */
bool FChunkWorldSpawnTicketCapacityTest::RunTest(const FString& Parameters)
{
	AChunkWorldExtended* const ChunkWorld = NewObject<AChunkWorldExtended>(GetTransientPackage());
	UChunkWorldSpawnComponent* const SpawnComponent = ChunkWorld->GetSpawnComponent();
	if (!TestNotNull(TEXT("Chunk world attaches its optional spawn component"), SpawnComponent)
		|| !TestTrue(TEXT("Test can configure the total ticket-record bound"), ChunkWorldSpawnTest::SetSpawnComponentIntProperty(*SpawnComponent, TEXT("MaximumSpawnTicketRecords"), 2))
		|| !TestTrue(TEXT("Test can configure terminal ticket retention"), ChunkWorldSpawnTest::SetSpawnComponentIntProperty(*SpawnComponent, TEXT("MaximumRetainedTerminalTickets"), 2)))
	{
		return false;
	}

	FChunkWorldSpawnRequest Request;
	Request.AllowedSourceFamiliesInPriorityOrder.Add(EChunkWorldSpawnSourceFamily::ReservationField);
	AddExpectedError(TEXT("Failure=WorldInvalid"), EAutomationExpectedErrorFlags::Contains, 3);
	const FChunkWorldSpawnTicketHandle FirstTicket = SpawnComponent->SubmitSpawnRequest(Request);
	const FChunkWorldSpawnTicketHandle SecondTicket = SpawnComponent->SubmitSpawnRequest(Request);
	const FChunkWorldSpawnTicketHandle ThirdTicket = SpawnComponent->SubmitSpawnRequest(Request);

	TestTrue(TEXT("First terminal failure receives a handle"), FirstTicket.IsValid());
	TestTrue(TEXT("Second terminal failure receives a handle"), SecondTicket.IsValid());
	TestTrue(TEXT("Capacity reservation admits a replacement terminal failure"), ThirdTicket.IsValid());

	FChunkWorldSpawnResult Result;
	TestFalse(TEXT("First terminal record is evicted at capacity even when all failures share one world time"), SpawnComponent->GetSpawnTicketResult(FirstTicket, Result));
	TestTrue(TEXT("Second terminal record remains queryable after deterministic first-record eviction"), SpawnComponent->GetSpawnTicketResult(SecondTicket, Result));
	TestTrue(TEXT("Newest terminal record remains queryable"), SpawnComponent->GetSpawnTicketResult(ThirdTicket, Result));
	return true;
}
