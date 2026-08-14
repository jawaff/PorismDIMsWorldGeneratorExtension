// Copyright 2026 Spotted Loaf Studio

#include "Actor/Components/ChunkWorldReadinessFreezeComponent.h"
#include "Components/ActorComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/CoreRedirects.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldReadinessFreezeClassRedirectTest,
	"PorismExtension.ChunkWorld.ReadinessFreeze.ClassRedirect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies serialized references using the former startup-freeze class path resolve to the renamed plugin component. */
bool FChunkWorldReadinessFreezeClassRedirectTest::RunTest(const FString& Parameters)
{
	UClass* const CanonicalClass = UChunkWorldReadinessFreezeComponent::StaticClass();
	UClass* const LoadedCanonicalClass = StaticLoadClass(
		UActorComponent::StaticClass(),
		nullptr,
		TEXT("/Script/PorismDIMsWorldGeneratorExtension.ChunkWorldReadinessFreezeComponent"));
	const FCoreRedirectObjectName LegacyName(TEXT("/Script/PorismDIMsWorldGeneratorExtension.PorismStartupFreezeComponent"));
	const FCoreRedirectObjectName RedirectedName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, LegacyName);

	TestEqual(TEXT("Canonical script class loads as readiness-freeze component"), LoadedCanonicalClass, CanonicalClass);
	TestEqual(TEXT("Legacy script class path redirects to readiness-freeze component"), RedirectedName.ToString(), FString(TEXT("/Script/PorismDIMsWorldGeneratorExtension.ChunkWorldReadinessFreezeComponent")));
	return true;
}
