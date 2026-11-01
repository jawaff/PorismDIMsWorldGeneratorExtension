// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Components/ChunkWorldStreamingMeshComponent.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldColorStreamingMaterialsTest,
    "PorismExtension.Rendering.ColorOnlyStreamingMaterials",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldColorStreamingMaterialsTest::RunTest(const FString&)
{
    auto* World = NewObject<AChunkWorldExtended>();
    World->WorldGenDef = NewObject<UWorldGenDef>(World);
    auto* Parent = UMaterial::GetDefaultMaterial(MD_Surface);
    World->WorldGenDef->SimplifiedBaseMaterial = Parent;
    auto* Component = NewObject<UChunkWorldStreamingMeshComponent>(World);
    Component->World = World;
    Component->MaterialNum = 9;
    Component->SetMaterial(0, Parent);
    TArray<UMaterialInstanceDynamic*> Instances;
    for (int32 Index = 0; Index < 8; ++Index)
    {
        auto* Instance = UMaterialInstanceDynamic::Create(Parent, World);
        auto& Color = Instance->VectorParameterValues.AddDefaulted_GetRef();
        Color.ParameterInfo = FMaterialParameterInfo(TEXT("BaseColor"));
        Color.ParameterValue = FLinearColor(Index / 8.f, 0, 1, 1);
        Component->SetMaterial(Index + 1, Instance);
        Instances.Add(Instance);
    }
    TArray<UMaterialInterface*> Descriptions;
    Component->GatherStreamingMaterials(Descriptions);
    TestEqual(TEXT("Color-only instances share one inherited texture description"), Descriptions.Num(), 1);
    TestTrue(TEXT("Parent supplies the description"), Descriptions.Contains(Parent));
    TArray<UMaterialInterface*> DrawMaterials;
    Component->GetUsedMaterials(DrawMaterials);
    TestEqual(TEXT("Renderer still sees all original material identities"), DrawMaterials.Num(), 9);
    for (auto* Instance : Instances) TestTrue(TEXT("Draw material is not replaced"), DrawMaterials.Contains(Instance));

    auto* Changed = Instances[0];
    const auto CheckSeparate = [&](const TCHAR* Reason)
    {
        Descriptions.Reset();
        Component->GatherStreamingMaterials(Descriptions);
        TestTrue(Reason, Descriptions.Contains(Changed));
        TestTrue(TEXT("Unchanged siblings still share the parent"), Descriptions.Contains(Parent));
    };
    Changed->TextureParameterValues.AddDefaulted();
    CheckSeparate(TEXT("Texture overrides retain their own binding"));
    Changed->TextureParameterValues.Reset();
    Changed->ScalarParameterValues.AddDefaulted();
    CheckSeparate(TEXT("Non-color scalar overrides use native metadata"));
    Changed->ScalarParameterValues.Reset();
    Changed->VectorParameterValues[0].ParameterInfo.Name = TEXT("OtherVector");
    CheckSeparate(TEXT("Unrecognized vector parameters are not collapsed"));
    Changed->VectorParameterValues[0].ParameterInfo.Name = TEXT("BaseColor");
    Changed->VectorParameterValues[0].ParameterInfo.Association = EMaterialParameterAssociation::LayerParameter;
    CheckSeparate(TEXT("Layer parameters are not generated global colors"));
    Changed->VectorParameterValues[0].ParameterInfo.Association = EMaterialParameterAssociation::GlobalParameter;
    Changed->RenamedTextures.Add(TEXT("OldTexture"), {TEXT("NewTexture")});
    CheckSeparate(TEXT("Texture-density remapping remains instance-owned"));
    Changed->RenamedTextures.Reset();
    Changed->GetTextureStreamingData().AddDefaulted();
    CheckSeparate(TEXT("Instance-authored texture density is preserved"));
    Changed->GetTextureStreamingData().Reset();
    Descriptions.Reset();
    Component->GatherStreamingMaterials(Descriptions);
    TestEqual(TEXT("Cleared overrides are reflected without a cache reset"), Descriptions.Num(), 1);

    auto* Foreign = UMaterialInstanceDynamic::Create(Parent, GetTransientPackage());
    Component->SetMaterial(1, Foreign);
    Descriptions.Reset();
    Component->GatherStreamingMaterials(Descriptions);
    TestTrue(TEXT("A material outside generator ownership keeps its identity"), Descriptions.Contains(Foreign));
    TestFalse(TEXT("Replaced material does not remain in a stale description"), Descriptions.Contains(Changed));
    Component->SetMaterial(1, Changed);
    Component->World = nullptr;
    Descriptions.Reset();
    Component->GatherStreamingMaterials(Descriptions);
    TestEqual(TEXT("Missing generator context cannot collapse arbitrary instances"), Descriptions.Num(), 9);
    Component->World = World;
    World->WorldGenDef->SimplifiedBaseMaterial = NewObject<UMaterial>(World);
    Descriptions.Reset();
    Component->GatherStreamingMaterials(Descriptions);
    TestEqual(TEXT("A changed parent definition invalidates eligibility immediately"), Descriptions.Num(), 9);
    TestEqual(TEXT("Extended worlds select the streaming component through the native factory"),
        GetDefault<AChunkWorldExtended>()->MeshComponent.Get(), UChunkWorldStreamingMeshComponent::StaticClass());
    return true;
}
#endif
