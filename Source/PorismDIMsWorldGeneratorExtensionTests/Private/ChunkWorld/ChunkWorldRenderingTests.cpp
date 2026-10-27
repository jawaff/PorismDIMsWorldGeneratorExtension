#include "ChunkInstanced/ChunkInstancedMeshComponent.h"
#include "Components/ActorComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldProxyRecreationThreadTest,
    "PorismExtension.ChunkWorld.Rendering.GameThreadProxyRecreation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldProxyRecreationThreadTest::RunTest(const FString& Parameters)
{
    // Deferred rendering dispatch consults this virtual contract before creating the custom proxy.
    const UActorComponent* Component = GetDefault<UChunkInstancedMeshComponent>();
    TestTrue(TEXT("Chunk proxy recreation stays on the game thread"),
        Component->RequiresGameThreadEndOfFrameRecreate());
    return true;
}
