// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "ChunkComponent/ChunkComponent.h"
#include "ChunkWorldStreamingMeshComponent.generated.h"

/** Shares texture-binding work for generated color-only LOD instances without changing draw materials. */
UCLASS()
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldStreamingMeshComponent : public UChunkComponent
{
    GENERATED_BODY()

public:
    /** Resolves current material bindings on each query; no retained metadata or changed publication cadence. */
    virtual void GetStreamingRenderAssetInfo(FStreamingTextureLevelContext& LevelContext,
        TArray<FStreamingRenderAssetPrimitiveInfo>& OutStreamingRenderAssets) const override;

    /** Trace-only publication hooks; native geometry, bounds and lifecycle ownership remain unchanged. */
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
    virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* Material) override;

protected:
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
    virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
    virtual void DestroyRenderState_Concurrent() override;
    virtual void OnUpdateTransform(EUpdateTransformFlags Flags, ETeleportType Teleport) override;

private:
    friend class FChunkWorldColorStreamingMaterialsTest;

    /** Only generated instances with inherited texture/density metadata may share their parent's description. */
    void GatherStreamingMaterials(TArray<UMaterialInterface*>& OutMaterials) const;
};
