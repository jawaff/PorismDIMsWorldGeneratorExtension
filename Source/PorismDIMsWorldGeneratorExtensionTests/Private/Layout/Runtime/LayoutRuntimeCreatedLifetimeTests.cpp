// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorldStructs/ChunkStructureTemplate.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutRuntimeCreatedLifetimeTest,
	"PorismExtension.Layout.Runtime.CreatedLifetime.Smoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeCreatedLifetimeTest::RunTest(const FString& Parameters)
{
	auto Harness = PorismLayoutWorldTestUtilities::CreateChunkWorldHarness(GetTransientPackage());
	auto* Runtime = Harness.RuntimeComponent;
	Runtime->bEnablePlanningWindowRuntimeUpdates = true;
	Runtime->SetLayoutWorldBindings({});
	const FIntVector Origin = FIntVector::ZeroValue;
	const TSet<FIntVector> Required = {Origin};
	UChunkWorldLayoutRuntimeComponent::FObservedChunkLoad Event;
	Event.ChunkBlockWorldPos = Origin;
	Event.DetailLevel = Harness.World->GetChunkLayerCount() - 1;
	Harness.World->ActivateChunkEditFunctions = false;
	// Native callbacks are one-based; direct layout observations below remain zero-based.
	Harness.World->OnChunkUpdate(Origin, Event.DetailLevel + 1, {});
	Runtime->ProcessQueuedLayoutWorkNow();
	TestFalse(TEXT("Updated alone grants no Created authority"), Runtime->HasFreshCreatedChunkEligibility(Required));

	Event.EventType = EChunkWorldChunkLifecycleEventType::Created;
	Harness.World->OnChunkCreate(Origin, Event.DetailLevel + 1, {});
	Harness.World->OnChunkUpdate(Origin, Event.DetailLevel + 1, {});
	TestEqual(TEXT("Repeated observations share one native-chunk queue entry"), Runtime->PendingChunkLoads.Num(), 1);
	Runtime->ProcessQueuedLayoutWorkNow();
	TestTrue(TEXT("Updated in the same batch preserves native Created intent"), Runtime->HasFreshCreatedChunkEligibility(Required));
	// Retained root ownership protects unloaded history; out-of-influence eviction
	// is covered separately by RootSpacing.FootprintGapAndLifetime.
	FResolvedLayoutSiteRecord RetainedRoot;
	RetainedRoot.RootSolveId = TEXT("Root.A");
	Runtime->ResolvedSiteRecords.Add(TEXT("Root.A"), RetainedRoot);
	RetainedRoot.RootSolveId = TEXT("Root.B");
	Runtime->ResolvedSiteRecords.Add(TEXT("Root.B"), RetainedRoot);
	Runtime->MarkRequiredChunkOriginsStamped(Required, TEXT("Artifact.A"), TEXT("Root.A"));
	Event.EventType = EChunkWorldChunkLifecycleEventType::Updated;
	Runtime->HandleObservedLoadedChunk(Event);
	TestTrue(TEXT("Remesh preserves eligibility for a later root or connector"), Runtime->HasFreshCreatedChunkEligibility(Required));
	TestEqual(TEXT("Remesh preserves root stamp history"), Runtime->GetChunkStampedRootCountForTesting(Origin), 1);
	FString Failure;
	TestFalse(TEXT("A different artifact cannot replace the stamped root"),
		Runtime->CanStampRequiredChunkOrigins(Required, TEXT("Artifact.B"), TEXT("Root.A"), Failure));
	Runtime->MarkRequiredChunkOriginsStamped(Required, TEXT("Artifact.B"), TEXT("Root.B"));
	TestTrue(TEXT("A second root does not consume chunk authority"), Runtime->HasFreshCreatedChunkEligibility(Required));
	TestFalse(TEXT("Every target/support chunk needs Created authority"),
		Runtime->HasFreshCreatedChunkEligibility({Origin, FIntVector(16, 0, 0)}));
	Harness.World->OnChunkDelete(Origin, Event.DetailLevel + 1);
	Harness.World->OnChunkCreate(Origin, Event.DetailLevel + 1, {});
	Harness.World->OnChunkUpdate(Origin, Event.DetailLevel + 1, {});
	Runtime->ProcessQueuedLayoutWorkNow();
	TestTrue(TEXT("Delete/Create/Updated establishes a new Created lifetime"), Runtime->HasFreshCreatedChunkEligibility(Required));
	Harness.World->OnChunkDelete(Origin, Event.DetailLevel + 1);
	Harness.World->OnChunkUpdate(Origin, Event.DetailLevel + 1, {});
	Runtime->ProcessQueuedLayoutWorkNow();
	TestFalse(TEXT("Delete/Updated cannot inherit prior Created authority"), Runtime->HasFreshCreatedChunkEligibility(Required));
	TestEqual(TEXT("Unload preserves both root identities"), Runtime->GetChunkStampedRootCountForTesting(Origin), 2);
	Event.EventType = EChunkWorldChunkLifecycleEventType::Created;
	Runtime->HandleObservedLoadedChunk(Event);
	TestTrue(TEXT("New Created restores readiness"), Runtime->HasFreshCreatedChunkEligibility(Required));
	Runtime->AddChunkStampMarkForTesting(Origin, TEXT("LoadedFromSave"), NAME_None);
	Event.EventType = EChunkWorldChunkLifecycleEventType::Created;
	Runtime->HandleObservedLoadedChunk(Event);
	TestFalse(TEXT("Created cannot override explicit restore evidence"), Runtime->HasFreshCreatedChunkEligibility(Required));
	Runtime->ResolvedRootFrozenTerrainContracts.Add(TEXT("Root.A"), FLayoutFrozenTerrainContract());
	Runtime->ResolvedRootRealizationWritePlans.Add(TEXT("Root.A"), nullptr);
	Harness.World->StopGen();
	TestFalse(TEXT("Stop invalidates prior readiness"), Runtime->HasFreshCreatedChunkEligibility(Required));
	TestTrue(TEXT("Stop clears previous-world roots"), Runtime->ResolvedSiteRecords.IsEmpty());
	TestTrue(TEXT("Stop clears previous-world stamp and restore history"), Runtime->StampedChunkOrigins.IsEmpty());
	TestTrue(TEXT("Stop releases frozen root contracts"), Runtime->ResolvedRootFrozenTerrainContracts.IsEmpty());
	TestTrue(TEXT("Stop releases root write plans"), Runtime->ResolvedRootRealizationWritePlans.IsEmpty());
	Harness.World->StartGen();
	Runtime->HandleObservedLoadedChunk(Event);
	TestTrue(TEXT("New-world Created at the same position is eligible"), Runtime->HasFreshCreatedChunkEligibility(Required));
	TestTrue(TEXT("Previous-world artifact cannot block the same root in a new world"),
		Runtime->CanStampRequiredChunkOrigins(Required, TEXT("Artifact.NewWorld"), TEXT("Root.A"), Failure));
	// A leaf can carve beyond its cell and anchor chunk, including empty authored volume.
	UChunkStructureTemplate* Template = NewObject<UChunkStructureTemplate>(GetTransientPackage());
	Template->SizeInBlocks = FIntVector(33, 2, 2);
	FResolvedLayoutSiteRecord TemplateSite;
	TemplateSite.RootSolveId = TEXT("TemplateCoverage");
	TemplateSite.SolveResult.SharedCellSizeInBlocks = FIntVector(1);
	TemplateSite.SolveResult.FootprintSize = FIntPoint(1);
	FLayoutPlacedModule& Placement = TemplateSite.SolveResult.Placements.AddDefaulted_GetRef();
	Placement.TemplatePath = FSoftObjectPath(Template);
	Placement.YawRotationSteps = 1;
	const TSet<FIntVector> TemplateOrigins = Runtime->CollectRequiredChunkOrigins(TemplateSite);
	TestEqual(TEXT("Yaw-rotated empty template spans three chunks"), TemplateOrigins.Num(), 3);
	TestTrue(TEXT("Full rotated volume includes its far chunk"), TemplateOrigins.Contains(FIntVector(0, 32, 0)));
	TestFalse(TEXT("Observed anchor alone cannot authorize a full template"), Runtime->HasFreshCreatedChunkEligibility(TemplateOrigins));
	for (const int32 Y : {16, 32})
	{
		Event.ChunkBlockWorldPos = FIntVector(0, Y, 0);
		Runtime->HandleObservedLoadedChunk(Event);
	}
	TestTrue(TEXT("All template chunks grant full placement readiness"), Runtime->HasFreshCreatedChunkEligibility(TemplateOrigins));
	Runtime->AddChunkStampMarkForTesting(FIntVector(0, 32, 0), TEXT("LoadedFromSave"), NAME_None);
	TestFalse(TEXT("Restore away from the anchor excludes the entire template"), Runtime->HasFreshCreatedChunkEligibility(TemplateOrigins));
	Runtime->AddChunkStampMarkForTesting(Origin, TEXT("LoadedFromSave"), NAME_None);
	TestFalse(TEXT("New-world explicit restore still excludes placement"), Runtime->HasFreshCreatedChunkEligibility(Required));
	return true;
}
