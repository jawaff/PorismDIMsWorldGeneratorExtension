// Copyright 2026 Spotted Loaf Studio

#include "Biome/Types/BiomeGameplayTags.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Spawn/Support/ChunkWorldSpawnTestTypes.h"
#include "ChunkWorld/Spawn/Support/ChunkWorldSpawnTestWorld.h"
#include "Misc/AutomationTest.h"

#include <limits>

namespace
{
	/** Owns one transient authority world for selection coverage. */
	struct FSelectionContext
	{
		UWorld* World = nullptr;
		FWorldContext* WorldContext = nullptr;
		AChunkWorldExtended* ChunkWorld = nullptr;
		UChunkWorldSpawnTestComponent* SpawnComponent = nullptr;
		~FSelectionContext() { ChunkWorldSpawnTest::DestroyWorld(World, WorldContext); }
		bool Initialize()
		{
			World = ChunkWorldSpawnTest::CreateWorld(WorldContext);
			if (World == nullptr) { return false; }
			ChunkWorld = World->SpawnActor<AChunkWorldExtended>();
			SpawnComponent = ChunkWorld != nullptr ? NewObject<UChunkWorldSpawnTestComponent>(ChunkWorld) : nullptr;
			if (SpawnComponent != nullptr) { SpawnComponent->RegisterComponent(); }
			return ChunkWorld != nullptr && SpawnComponent != nullptr;
		}
	};

	FChunkWorldSpawnRequest MakeRequest()
	{
		FChunkWorldSpawnRequest Request;
		Request.SubjectBinding.StableSubjectId = FGuid::NewGuid();
		Request.AllowedSourceFamiliesInPriorityOrder.Add(EChunkWorldSpawnSourceFamily::ExplicitFixture);
		return Request;
	}

	FChunkWorldSpawnCandidate MakeCandidate(const FName SourceId)
	{
		FChunkWorldSpawnCandidate Candidate;
		Candidate.SourceFamily = EChunkWorldSpawnSourceFamily::ExplicitFixture;
		Candidate.SourceKind = TEXT("AutomationFixture");
		Candidate.SourceId = SourceId;
		Candidate.SearchBounds = FBox(FVector(-500.0f, -500.0f, -50.0f), FVector(500.0f, 500.0f, 300.0f));
		Candidate.TheoreticalLocation = FVector(125.0f, -75.0f, 125.0f);
		Candidate.TheoreticalSurfaceZ = 125.0;
		Candidate.DebugLabel = SourceId.ToString();
		return Candidate;
	}

	TScriptInterface<IChunkWorldSpawnSourceProvider> AsSourceProvider(UChunkWorldSpawnTestSourceProvider* Provider)
	{
		TScriptInterface<IChunkWorldSpawnSourceProvider> Interface;
		Interface.SetObject(Provider);
		Interface.SetInterface(Provider);
		return Interface;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldSpawnOptionalDefinitionSelectionTest,
	"PorismExtension.ChunkWorld.Spawn.Selection.OptionalDefinition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies weighted definitions are optional but selected when supplied by NPC policy. */
bool FChunkWorldSpawnOptionalDefinitionSelectionTest::RunTest(const FString& Parameters)
{
	FSelectionContext Context;
	if (!TestTrue(TEXT("Transient authority world initializes"), Context.Initialize())) { return false; }
	UChunkWorldSpawnTestSourceProvider* const Provider = NewObject<UChunkWorldSpawnTestSourceProvider>(Context.ChunkWorld);
	FChunkWorldSpawnCandidate Candidate = MakeCandidate(TEXT("Foundation"));
	Candidate.SpawnTags.AddTag(BiomeGameplayTags::Foundation.GetTag());
	Provider->Candidates.Add(Candidate);
	if (!TestTrue(TEXT("Provider registers"), Context.SpawnComponent->RegisterSpawnSourceProvider(AsSourceProvider(Provider)))) { return false; }

	FChunkWorldSpawnRequest Request = MakeRequest();
	FInstancedStruct Definition;
	Definition.InitializeAs(FChunkWorldSpawnSubjectDefinitionBase::StaticStruct());
	Definition.GetMutablePtr<FChunkWorldSpawnSubjectDefinitionBase>()->RequiredSpawnTags.AddTag(BiomeGameplayTags::Foundation.GetTag());
	Request.SubjectDefinitions.Add(MoveTemp(Definition));
	const FChunkWorldSpawnTicketHandle Ticket = Context.SpawnComponent->SubmitSpawnRequest(Request);
	Context.SpawnComponent->Advance();

	FChunkWorldSpawnResult Result;
	TestEqual(TEXT("Weighted request resolves location"), Context.SpawnComponent->GetSpawnTicketState(Ticket), EChunkWorldSpawnTicketState::Ready);
	TestTrue(TEXT("Result is queryable"), Context.SpawnComponent->GetSpawnTicketResult(Ticket, Result));
	TestTrue(TEXT("Selected definition survives opaque result handoff"), Result.SelectedSubjectDefinition.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldSpawnWeightedCompatibilityTest,
	"PorismExtension.ChunkWorld.Spawn.Selection.WeightedCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies weighted NPC definitions select only candidate sources satisfying their opaque tag requirements. */
bool FChunkWorldSpawnWeightedCompatibilityTest::RunTest(const FString& Parameters)
{
	FSelectionContext Context;
	if (!TestTrue(TEXT("Transient authority world initializes"), Context.Initialize())) { return false; }
	UChunkWorldSpawnTestSourceProvider* const Provider = NewObject<UChunkWorldSpawnTestSourceProvider>(Context.ChunkWorld);
	FChunkWorldSpawnCandidate Foundation = MakeCandidate(TEXT("Foundation"));
	Foundation.SpawnTags.AddTag(BiomeGameplayTags::Foundation.GetTag());
	FChunkWorldSpawnCandidate Reservation = MakeCandidate(TEXT("Reservation"));
	Reservation.SpawnTags.AddTag(BiomeGameplayTags::Reservation.GetTag());
	Provider->Candidates = { Foundation, Reservation };
	if (!TestTrue(TEXT("Provider registers"), Context.SpawnComponent->RegisterSpawnSourceProvider(AsSourceProvider(Provider)))) { return false; }

	FChunkWorldSpawnRequest Request = MakeRequest();
	FInstancedStruct FoundationDefinition;
	FoundationDefinition.InitializeAs(FChunkWorldSpawnSubjectDefinitionBase::StaticStruct());
	FoundationDefinition.GetMutablePtr<FChunkWorldSpawnSubjectDefinitionBase>()->SelectionWeight = 0.01f;
	FoundationDefinition.GetMutablePtr<FChunkWorldSpawnSubjectDefinitionBase>()->RequiredSpawnTags.AddTag(BiomeGameplayTags::Foundation.GetTag());
	FInstancedStruct ReservationDefinition;
	ReservationDefinition.InitializeAs(FChunkWorldSpawnSubjectDefinitionBase::StaticStruct());
	ReservationDefinition.GetMutablePtr<FChunkWorldSpawnSubjectDefinitionBase>()->SelectionWeight = 100000.0f;
	ReservationDefinition.GetMutablePtr<FChunkWorldSpawnSubjectDefinitionBase>()->RequiredSpawnTags.AddTag(BiomeGameplayTags::Reservation.GetTag());
	Request.SubjectDefinitions = { MoveTemp(FoundationDefinition), MoveTemp(ReservationDefinition) };

	const FChunkWorldSpawnTicketHandle Ticket = Context.SpawnComponent->SubmitSpawnRequest(Request);
	Context.SpawnComponent->Advance();
	FChunkWorldSpawnResult Result;
	TestEqual(TEXT("Compatible weighted request resolves"), Context.SpawnComponent->GetSpawnTicketState(Ticket), EChunkWorldSpawnTicketState::Ready);
	TestTrue(TEXT("Weighted result is queryable"), Context.SpawnComponent->GetSpawnTicketResult(Ticket, Result));
	TestEqual(TEXT("Higher-weight compatible pair chooses reservation candidate"), Result.SelectedSourceId, FName(TEXT("Reservation")));
	const FChunkWorldSpawnSubjectDefinitionBase* const SelectedDefinition = Result.SelectedSubjectDefinition.GetPtr<FChunkWorldSpawnSubjectDefinitionBase>();
	TestTrue(TEXT("Selected opaque definition matches selected source tags"), SelectedDefinition != nullptr && SelectedDefinition->RequiredSpawnTags.HasTagExact(BiomeGameplayTags::Reservation.GetTag()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldSpawnRejectsMalformedInputTest,
	"PorismExtension.ChunkWorld.Spawn.Selection.RejectsMalformedInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies malformed definitions and candidates cannot publish a location. */
bool FChunkWorldSpawnRejectsMalformedInputTest::RunTest(const FString& Parameters)
{
	FSelectionContext Context;
	if (!TestTrue(TEXT("Transient authority world initializes"), Context.Initialize())) { return false; }
	UChunkWorldSpawnTestSourceProvider* const Provider = NewObject<UChunkWorldSpawnTestSourceProvider>(Context.ChunkWorld);
	FChunkWorldSpawnCandidate BadCandidate = MakeCandidate(TEXT("Bad"));
	BadCandidate.TheoreticalLocation.Z = std::numeric_limits<double>::quiet_NaN();
	Provider->Candidates.Add(BadCandidate);
	if (!TestTrue(TEXT("Provider registers"), Context.SpawnComponent->RegisterSpawnSourceProvider(AsSourceProvider(Provider)))) { return false; }

	FChunkWorldSpawnRequest Request = MakeRequest();
	FInstancedStruct BadDefinition;
	BadDefinition.InitializeAs(FChunkWorldSpawnSubjectDefinitionBase::StaticStruct());
	BadDefinition.GetMutablePtr<FChunkWorldSpawnSubjectDefinitionBase>()->SelectionWeight = std::numeric_limits<float>::quiet_NaN();
	Request.SubjectDefinitions.Add(MoveTemp(BadDefinition));
	AddExpectedError(TEXT("Failure=NoEligibleSubjectDefinition"));
	const FChunkWorldSpawnTicketHandle BadDefinitionTicket = Context.SpawnComponent->SubmitSpawnRequest(Request);
	TestEqual(TEXT("Malformed definition fails admission"), Context.SpawnComponent->GetSpawnTicketState(BadDefinitionTicket), EChunkWorldSpawnTicketState::Failed);

	AddExpectedError(TEXT("Failure=NoEligibleSource"));
	const FChunkWorldSpawnTicketHandle BadCandidateTicket = Context.SpawnComponent->SubmitSpawnRequest(MakeRequest());
	Context.SpawnComponent->Advance();
	TestEqual(TEXT("Malformed candidate cannot resolve"), Context.SpawnComponent->GetSpawnTicketState(BadCandidateTicket), EChunkWorldSpawnTicketState::Failed);
	return true;
}
