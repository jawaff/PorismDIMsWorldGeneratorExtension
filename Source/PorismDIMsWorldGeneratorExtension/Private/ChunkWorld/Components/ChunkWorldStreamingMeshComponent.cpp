// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Components/ChunkWorldStreamingMeshComponent.h"

#include "ChunkWorld/ChunkWorldCore.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureStreamingTypes.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

FBoxSphereBounds UChunkWorldStreamingMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ChunkExtension_CalcBounds);
    return Super::CalcBounds(LocalToWorld);
}

void UChunkWorldStreamingMeshComponent::SetMaterial(int32 ElementIndex, UMaterialInterface* Material)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ChunkExtension_SetMaterial);
    Super::SetMaterial(ElementIndex, Material);
}

void UChunkWorldStreamingMeshComponent::OnRegister()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ChunkExtension_Register);
    Super::OnRegister();
}

void UChunkWorldStreamingMeshComponent::OnUnregister()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ChunkExtension_Unregister);
    Super::OnUnregister();
}

void UChunkWorldStreamingMeshComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ChunkExtension_CreateRenderState);
    Super::CreateRenderState_Concurrent(Context);
}

void UChunkWorldStreamingMeshComponent::DestroyRenderState_Concurrent()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ChunkExtension_DestroyRenderState);
    Super::DestroyRenderState_Concurrent();
}

void UChunkWorldStreamingMeshComponent::OnUpdateTransform(EUpdateTransformFlags Flags, ETeleportType Teleport)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ChunkExtension_UpdateTransform);
    Super::OnUpdateTransform(Flags, Teleport);
}

void UChunkWorldStreamingMeshComponent::GatherStreamingMaterials(TArray<UMaterialInterface*>& OutMaterials) const
{
    TArray<UMaterialInterface*> Materials;
    GetUsedMaterials(Materials);
    const UMaterialInterface* SimpleParent = World && World->WorldGenDef ? World->WorldGenDef->SimplifiedBaseMaterial : nullptr;
    const FName ColorParameter(TEXT("BaseColor"));
    for (UMaterialInterface* Material : Materials)
    {
        auto* Instance = Cast<UMaterialInstanceDynamic>(Material);
        if (SimpleParent && Instance && Instance->GetClass() == UMaterialInstanceDynamic::StaticClass()
            && Instance->GetOuter() == World && Instance->Parent == SimpleParent
            && !Instance->bHasStaticPermutationResource && !Instance->HasTextureStreamingData()
            && Instance->RenamedTextures.IsEmpty() && Instance->ScalarParameterValues.IsEmpty()
            && Instance->DoubleVectorParameterValues.IsEmpty() && Instance->TextureParameterValues.IsEmpty()
            && Instance->TextureCollectionParameterValues.IsEmpty() && Instance->ParameterCollectionParameterValues.IsEmpty()
            && Instance->RuntimeVirtualTextureParameterValues.IsEmpty() && Instance->SparseVolumeTextureParameterValues.IsEmpty()
            && Instance->FontParameterValues.IsEmpty() && Instance->UserSceneTextureOverrides.IsEmpty()
            && !Instance->VectorParameterValues.ContainsByPredicate([&](const FVectorParameterValue& Value)
            {
                return Value.ParameterInfo.Name != ColorParameter
                    || Value.ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter;
            }))
        {
            // StartGen changes only BaseColor for these instances. Texture bindings and density still come from Parent.
            Material = Instance->Parent;
        }
        if (Material) OutMaterials.AddUnique(Material);
    }
}

void UChunkWorldStreamingMeshComponent::GetStreamingRenderAssetInfo(FStreamingTextureLevelContext& LevelContext,
    TArray<FStreamingRenderAssetPrimitiveInfo>& OutStreamingRenderAssets) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ChunkExtension_StreamingMetadata);
    static const IConsoleVariable* NewMetrics = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streaming.UseNewMetrics"));
    if (CanSkipGetTextureStreamingRenderAssetInfo() || !NewMetrics || NewMetrics->GetInt() == 0)
    {
        Super::GetStreamingRenderAssetInfo(LevelContext, OutStreamingRenderAssets);
        return;
    }

    LevelContext.BindBuildData(nullptr);
    TArray<UMaterialInterface*> Materials;
    GatherStreamingMaterials(Materials);
    static const FMeshUVChannelInfo UVChannelData(1.f);
    FPrimitiveMaterialInfo MaterialData;
    MaterialData.PackedRelativeBox = PackedRelativeBox_Identity;
    MaterialData.UVChannelData = &UVChannelData;
    for (UMaterialInterface* Material : Materials)
    {
        MaterialData.Material = Material;
        LevelContext.ProcessMaterial(Bounds, MaterialData, Bounds.SphereRadius, OutStreamingRenderAssets,
            bIsValidTextureStreamingBuiltData, this);
    }
}
