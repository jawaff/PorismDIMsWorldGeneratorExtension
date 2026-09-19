// Copyright 2026 Spotted Loaf Studio

#include "Layout/Async/LayoutBackgroundSolveTypes.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"

#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBackgroundSolveSettingsResolveRetryAndDiscoveryBudgetsTest,
	"PorismExtension.Layout.Async.Settings.ResolveRetryAndDiscoveryBudgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBackgroundSolveSettingsResolveRetryAndDiscoveryBudgetsTest::RunTest(const FString& Parameters)
{
	FLayoutBackgroundSolveSettings Settings;
	TestEqual(TEXT("Default candidate-attempt cap stays at five"), Settings.ResolveMaxLayoutSolveCandidateAttempts(), 5);

	Settings.MaxLayoutSolveCandidateAttempts = 0;
	TestEqual(TEXT("Candidate-attempt cap clamps to at least one"), Settings.ResolveMaxLayoutSolveCandidateAttempts(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldLayoutRuntimeComponentAsyncSettingsMetadataTest,
	"PorismExtension.Layout.Async.Settings.RuntimeComponentExposesRetryAndDiscoveryBudgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldLayoutRuntimeComponentAsyncSettingsMetadataTest::RunTest(const FString& Parameters)
{
	const UChunkWorldLayoutRuntimeComponent* const Defaults = GetDefault<UChunkWorldLayoutRuntimeComponent>();
	if (!TestNotNull(TEXT("Runtime component default object exists"), Defaults))
	{
		return false;
	}

	const FIntProperty* const CandidateAttemptsProperty = FindFProperty<FIntProperty>(
		UChunkWorldLayoutRuntimeComponent::StaticClass(),
		TEXT("MaxLayoutSolveCandidateAttempts"));
	if (!TestNotNull(TEXT("Runtime component exposes MaxLayoutSolveCandidateAttempts"), CandidateAttemptsProperty))
	{
		return false;
	}

	TestEqual(TEXT("Candidate-attempt property default stays at five"), CandidateAttemptsProperty->GetPropertyValue_InContainer(Defaults), 5);
	TestEqual(TEXT("Candidate-attempt property clamps to one"), CandidateAttemptsProperty->GetMetaData(TEXT("ClampMin")), FString(TEXT("1")));
	TestEqual(TEXT("Candidate-attempt property stays under async category"), CandidateAttemptsProperty->GetMetaData(TEXT("Category")), FString(TEXT("Layout|Async")));
	TestTrue(TEXT("Candidate-attempt tooltip describes bounded async retries"), CandidateAttemptsProperty->GetMetaData(TEXT("ToolTip")).Contains(TEXT("dropped from active work")));

	return true;
}
