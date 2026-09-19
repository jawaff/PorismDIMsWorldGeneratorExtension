// Copyright 2026 Spotted Loaf Studio

#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutRuntimeRootIdentityTest,
	"PorismExtension.Layout.Runtime.RootIdentity.CacheAdmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeRootIdentityTest::RunTest(const FString& Parameters)
{
	auto* Runtime = NewObject<UChunkWorldLayoutRuntimeComponent>();
	FResolvedLayoutSiteRecord Record;
	Record.SiteCenterBlockWorldPos = FIntVector(8, 8, 0);
	Record.bLayoutSolved = true;
	Record.CachedApplyability = ELayoutCachedApplyability::Solved;
	Record.RootSolveId = TEXT("RootIdentity.First");
	FIntPoint Key;
	TestTrue(TEXT("Producer-owned identity can enter cache"), Runtime->TryCacheExplicitRootLayoutSite(Record, Key));
	TestEqual(TEXT("Cache returns exact site XY"), Key, FIntPoint(8, 8));
	Record.RootSolveId = TEXT("RootIdentity.Second");
	TestTrue(TEXT("Distinct roots retain separate ownership at the same XY"), Runtime->TryCacheExplicitRootLayoutSite(Record, Key));
	TestEqual(TEXT("Both identities remain cached"), Runtime->GetResolvedLayoutSiteRecords().Num(), 2);

	Record.RootSolveId = NAME_None;
	TestFalse(TEXT("Missing identity cannot create a coordinate fallback entry"), Runtime->TryCacheExplicitRootLayoutSite(Record, Key));
	TestEqual(TEXT("Rejected identity cannot mutate existing cache"), Runtime->GetResolvedLayoutSiteRecords().Num(), 2);
	FString Failure;
	TestFalse(TEXT("Apply rejects missing identity before other publication work"),
		Runtime->TryApplySolvedExplicitRootLayoutSite(Record, &Failure));
	TestTrue(TEXT("Apply names missing root identity"), Failure.Contains(TEXT("root solve identity")));
	TestEqual(TEXT("Rejected Apply preserves both cached roots"), Runtime->GetResolvedLayoutSiteRecords().Num(), 2);
	return true;
}
