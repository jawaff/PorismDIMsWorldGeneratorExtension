// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Blueprint/ChunkWorldBlockHitBlueprintLibrary.h"

#include "Block/BlockTypeSchemaBlueprintLibrary.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ChunkWorld/ChunkWorld.h"
#include "ChunkWorldStructs/ChunkWorldEnums.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "PorismDIMsWorldGeneratorExtension.h"
#include "ChunkWorld/Components/ChunkWorldBlockFeedbackComponent.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogChunkWorldBlockHitBlueprintLibrary, Log, All);

namespace
{
	/**
	 * Returns true when the block uses a mesh-backed representation.
	 */
	bool IsMeshBackedBlockHitVoxel(int32 MeshIndex)
	{
		return MeshIndex != EmptyMesh && MeshIndex != DefaultMesh;
	}

	/**
	 * Returns true when a runtime block should be treated as represented gameplay data.
	 */
	bool IsRepresentedBlockHitVoxel(int32 MaterialIndex, int32 MeshIndex)
	{
		return MaterialIndex != EmptyMaterial || IsMeshBackedBlockHitVoxel(MeshIndex);
	}

	/**
	 * Resolves one represented block candidate from a specific world position.
	 */
	bool TryResolveRepresentedBlock(AChunkWorld* ChunkWorld, const FIntVector& CandidateBlockWorldPos, const FVector& RepresentativeWorldPos, EChunkWorldBlockHitResolveSource ResolveSource, FChunkWorldResolvedBlockHit& OutHit)
	{
		if (!IsValid(ChunkWorld))
		{
			return false;
		}

		const int32 CandidateMaterialIndex = ChunkWorld->GetBlockValueByBlockWorldPos(CandidateBlockWorldPos, ERessourceType::MaterialIndex, 0);
		const int32 CandidateMeshIndex = ChunkWorld->GetMeshDataByBlockWorldPos(CandidateBlockWorldPos).MeshId;
		if (!IsRepresentedBlockHitVoxel(CandidateMaterialIndex, CandidateMeshIndex))
		{
			return false;
		}

		OutHit.bHasBlock = true;
		OutHit.BlockWorldPos = CandidateBlockWorldPos;
		OutHit.RepresentativeWorldPos = RepresentativeWorldPos;
		OutHit.MaterialIndex = CandidateMaterialIndex;
		OutHit.MeshIndex = CandidateMeshIndex;
		OutHit.ResolveSource = ResolveSource;
		return true;
	}

	/**
	 * Promotes a mesh-backed block that sits directly above a terrain surface candidate.
	 */
	bool TryPromoteOverlayVoxel(AChunkWorld* ChunkWorld, const FIntVector& BaseBlockWorldPos, const FVector& BaseRepresentativeWorldPos, const FVector& SafeImpactNormal, FChunkWorldResolvedBlockHit& OutHit)
	{
		if (!IsValid(ChunkWorld) || SafeImpactNormal.Z < 0.6f)
		{
			return false;
		}

		const FIntVector AboveBlockWorldPos = BaseBlockWorldPos + FIntVector(0, 0, 1);
		FChunkWorldResolvedBlockHit OverlayHit;
		if (!TryResolveRepresentedBlock(ChunkWorld, AboveBlockWorldPos, ChunkWorld->BlockWorldPosToUEWorldPos(AboveBlockWorldPos), EChunkWorldBlockHitResolveSource::LocalProbe, OverlayHit))
		{
			return false;
		}

		if (!IsMeshBackedBlockHitVoxel(OverlayHit.MeshIndex))
		{
			return false;
		}

		OutHit = OverlayHit;
		OutHit.RepresentativeWorldPos = BaseRepresentativeWorldPos;
		return true;
	}
}

bool UChunkWorldBlockHitBlueprintLibrary::GetChunkWorldFromHitResult(const FHitResult& Hit, AChunkWorld*& OutChunkWorld)
{
	OutChunkWorld = nullptr;

	if (AActor* HitActor = Hit.GetActor())
	{
		if (AChunkWorld* ChunkWorld = Cast<AChunkWorld>(HitActor))
		{
			OutChunkWorld = ChunkWorld;
			return true;
		}
	}

	if (UPrimitiveComponent* HitComponent = Hit.GetComponent())
	{
		if (AChunkWorld* ChunkWorld = Cast<AChunkWorld>(HitComponent->GetOwner()))
		{
			OutChunkWorld = ChunkWorld;
			return true;
		}
	}

	return false;
}

bool UChunkWorldBlockHitBlueprintLibrary::GetBlockTypeSchemaComponentFromChunkWorld(AChunkWorld* ChunkWorld, UBlockTypeSchemaComponent*& OutSchemaComponent)
{
	OutSchemaComponent = nullptr;
	if (!IsValid(ChunkWorld))
	{
		return false;
	}

	OutSchemaComponent = ChunkWorld->FindComponentByClass<UBlockTypeSchemaComponent>();
	return IsValid(OutSchemaComponent);
}

bool UChunkWorldBlockHitBlueprintLibrary::GetBlockFeedbackComponentFromChunkWorld(AChunkWorld* ChunkWorld, UChunkWorldBlockFeedbackComponent*& OutFeedbackComponent)
{
	OutFeedbackComponent = nullptr;
	if (!IsValid(ChunkWorld))
	{
		return false;
	}

	OutFeedbackComponent = ChunkWorld->FindComponentByClass<UChunkWorldBlockFeedbackComponent>();
	return IsValid(OutFeedbackComponent);
}

bool UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromHitResult(const FHitResult& Hit, const FVector& TraceDirection, FChunkWorldResolvedBlockHit& OutResolvedHit)
{
	OutResolvedHit = FChunkWorldResolvedBlockHit();

	AChunkWorld* ChunkWorld = nullptr;
	if (!GetChunkWorldFromHitResult(Hit, ChunkWorld)
		|| !IsValid(ChunkWorld)
		|| ChunkWorld->IsActorBeingDestroyed()
		|| !ChunkWorld->PrimLayer
		|| !ChunkWorld->IsRuning())
	{
		return false;
	}

	UBlockTypeSchemaComponent* SchemaComponent = nullptr;
	(void)GetBlockTypeSchemaComponentFromChunkWorld(ChunkWorld, SchemaComponent);

	const UInstancedStaticMeshComponent* InstancedMeshComponent = Cast<UInstancedStaticMeshComponent>(Hit.GetComponent());
	const bool bHitInstancedMesh = InstancedMeshComponent != nullptr;
	const float HalfBlockExtent = ChunkWorld->PrimLayer ? static_cast<float>(ChunkWorld->PrimLayer->BlockSize) * 0.5f : 0.0f;
	constexpr float ProbeEpsilon = 2.0f;

	const FChunkWorldHit ChunkWorldHit = ChunkWorld->GetChunkWorldHitByHitResult(Hit, true);
	const auto TryResolveAndStore = [ChunkWorld, SchemaComponent](const FIntVector& CandidateBlockWorldPos, const FVector& RepresentativeWorldPos, EChunkWorldBlockHitResolveSource ResolveSource, FChunkWorldResolvedBlockHit& OutHit)
	{
		if (!TryResolveRepresentedBlock(ChunkWorld, CandidateBlockWorldPos, RepresentativeWorldPos, ResolveSource, OutHit))
		{
			return false;
		}

		OutHit.ChunkWorld = ChunkWorld;
		OutHit.BlockTypeSchemaComponent = SchemaComponent;
		return true;
	};

	const bool bHasChunkWorldHit = ChunkWorldHit.CheckSuccess && IsRepresentedBlockHitVoxel(ChunkWorldHit.MaterialIndex, ChunkWorldHit.MeshIndex);
	if (bHitInstancedMesh && bHasChunkWorldHit)
	{
		FChunkWorldResolvedBlockHit RevalidatedHit;
		if (TryResolveAndStore(
			ChunkWorldHit.BlockWorldPos,
			ChunkWorld->BlockWorldPosToUEWorldPos(ChunkWorldHit.BlockWorldPos),
			EChunkWorldBlockHitResolveSource::ChunkWorldHit,
			RevalidatedHit))
		{
			OutResolvedHit = RevalidatedHit;
			return true;
		}
	}

	if (InstancedMeshComponent)
	{
		if (Hit.Item >= 0)
		{
			FTransform InstanceTransform;
			if (InstancedMeshComponent->GetInstanceTransform(Hit.Item, InstanceTransform, true))
			{
				const FVector RepresentativeWorldPos = InstanceTransform.GetLocation() + FVector(0.0f, 0.0f, HalfBlockExtent);
				const FIntVector BlockWorldPos = ChunkWorld->UEWorldPosToBlockWorldPos(RepresentativeWorldPos);
				FChunkWorldResolvedBlockHit AnchorHit;
				if (TryResolveAndStore(BlockWorldPos, RepresentativeWorldPos, EChunkWorldBlockHitResolveSource::InstancedMeshAnchor, AnchorHit))
				{
					OutResolvedHit = AnchorHit;
					return true;
				}
			}
		}
	}

	const FVector SafeTraceDirection = TraceDirection.GetSafeNormal();
	const FVector SafeImpactNormal = Hit.ImpactNormal.GetSafeNormal();

	TArray<FVector, TInlineAllocator<5>> ProbeLocations;
	ProbeLocations.Reserve(5);

	if (!SafeImpactNormal.IsNearlyZero())
	{
		ProbeLocations.Add(Hit.ImpactPoint - SafeImpactNormal * (HalfBlockExtent + ProbeEpsilon));
		ProbeLocations.Add(Hit.ImpactPoint + SafeImpactNormal * ProbeEpsilon);
	}

	ProbeLocations.Add(Hit.ImpactPoint - SafeTraceDirection * ProbeEpsilon);
	ProbeLocations.Add(Hit.ImpactPoint + SafeTraceDirection * ProbeEpsilon);
	if (bHasChunkWorldHit)
	{
		ProbeLocations.Add(ChunkWorld->BlockWorldPosToUEWorldPos(ChunkWorldHit.BlockWorldPos));
	}

	for (const FVector& CandidateProbeLocation : ProbeLocations)
	{
		const FIntVector CandidateBlockWorldPos = ChunkWorld->UEWorldPosToBlockWorldPos(CandidateProbeLocation);
		FChunkWorldResolvedBlockHit CandidateHit;
		if (!TryResolveAndStore(CandidateBlockWorldPos, CandidateProbeLocation, EChunkWorldBlockHitResolveSource::LocalProbe, CandidateHit))
		{
			continue;
		}

		const bool bTerrainTopFaceCandidate = !bHitInstancedMesh
			&& !IsMeshBackedBlockHitVoxel(CandidateHit.MeshIndex)
			&& SafeImpactNormal.Z >= 0.6f;
		if (bTerrainTopFaceCandidate && TryPromoteOverlayVoxel(ChunkWorld, CandidateHit.BlockWorldPos, CandidateProbeLocation, SafeImpactNormal, CandidateHit))
		{
			OutResolvedHit = CandidateHit;
			return true;
		}

		OutResolvedHit = CandidateHit;
		return true;
	}

	if (bHasChunkWorldHit)
	{
		if (!TryResolveAndStore(
			ChunkWorldHit.BlockWorldPos,
			ChunkWorld->BlockWorldPosToUEWorldPos(ChunkWorldHit.BlockWorldPos),
			EChunkWorldBlockHitResolveSource::ChunkWorldHit,
			OutResolvedHit))
		{
			return false;
		}
		if (!bHitInstancedMesh
			&& !IsMeshBackedBlockHitVoxel(OutResolvedHit.MeshIndex)
			&& TryPromoteOverlayVoxel(ChunkWorld, OutResolvedHit.BlockWorldPos, OutResolvedHit.RepresentativeWorldPos, SafeImpactNormal, OutResolvedHit))
		{
			return true;
		}
		return true;
	}

	return false;
}

bool UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromBlockWorldPos(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, FChunkWorldResolvedBlockHit& OutResolvedHit)
{
	OutResolvedHit = FChunkWorldResolvedBlockHit();
	if (!IsValid(ChunkWorld)
		|| ChunkWorld->IsActorBeingDestroyed()
		|| !ChunkWorld->PrimLayer
		|| !ChunkWorld->IsRuning())
	{
		return false;
	}

	UBlockTypeSchemaComponent* SchemaComponent = nullptr;
	(void)GetBlockTypeSchemaComponentFromChunkWorld(ChunkWorld, SchemaComponent);

	FChunkWorldResolvedBlockHit CandidateHit;
	if (!TryResolveRepresentedBlock(
		ChunkWorld,
		BlockWorldPos,
		ChunkWorld->BlockWorldPosToUEWorldPos(BlockWorldPos),
		EChunkWorldBlockHitResolveSource::LocalProbe,
		CandidateHit))
	{
		return false;
	}

	CandidateHit.ChunkWorld = ChunkWorld;
	CandidateHit.BlockTypeSchemaComponent = SchemaComponent;
	OutResolvedHit = CandidateHit;
	return true;
}

bool UChunkWorldBlockHitBlueprintLibrary::TryGetBlockCustomDataForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit, FGameplayTag& OutBlockTypeName, FInstancedStruct& OutCustomData)
{
	OutBlockTypeName = FGameplayTag();
	OutCustomData.Reset();

	if (!ResolvedHit.bHasBlock || !IsValid(ResolvedHit.BlockTypeSchemaComponent))
	{
		return false;
	}

	return ResolvedHit.BlockTypeSchemaComponent->GetBlockCustomDataForBlockWorldPos(ResolvedHit.BlockWorldPos, OutBlockTypeName, OutCustomData);
}

bool UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit, FGameplayTag& OutBlockTypeName, FInstancedStruct& OutDefinition)
{
	OutBlockTypeName = FGameplayTag();
	OutDefinition.Reset();

	FInstancedStruct CustomData;
	if (TryGetBlockCustomDataForResolvedBlockHit(ResolvedHit, OutBlockTypeName, CustomData))
	{
		return TryGetBlockDefinitionForBlockTypeName(ResolvedHit.BlockTypeSchemaComponent, OutBlockTypeName, OutDefinition);
	}

	if (!ResolvedHit.bHasBlock || !IsValid(ResolvedHit.BlockTypeSchemaComponent))
	{
		return false;
	}

	if (!ResolvedHit.BlockTypeSchemaComponent->GetBlockDefinitionForBlockWorldPos(ResolvedHit.BlockWorldPos, OutBlockTypeName, OutDefinition))
	{
		UE_LOG(
			LogPorismDIMsWorldGeneratorExtension,
			Warning,
			TEXT("Failed to resolve block definition for chunk world '%s' at %s. LookupReady=%d Registry=%s MaterialIndex=%d MeshIndex=%d ResolveSource=%d"),
			*GetNameSafe(ResolvedHit.ChunkWorld),
			*ResolvedHit.BlockWorldPos.ToString(),
			ResolvedHit.BlockTypeSchemaComponent->IsBlockDefinitionLookupReady(),
			*GetNameSafe(ResolvedHit.BlockTypeSchemaComponent->GetBlockTypeSchemaRegistry()),
			ResolvedHit.MaterialIndex,
			ResolvedHit.MeshIndex,
			static_cast<int32>(ResolvedHit.ResolveSource));
		return false;
	}

	return true;
}

bool UChunkWorldBlockHitBlueprintLibrary::TryGetBlockCustomDataForBlockTypeName(UBlockTypeSchemaComponent* SchemaComponent, FGameplayTag BlockTypeName, FInstancedStruct& OutCustomData)
{
	OutCustomData.Reset();
	if (!IsValid(SchemaComponent) || !BlockTypeName.IsValid())
	{
		return false;
	}

	UBlockTypeSchemaRegistry* Registry = SchemaComponent->GetBlockTypeSchemaRegistry();
	if (!IsValid(Registry))
	{
		return false;
	}

	return Registry->TryGetBlockCustomData(BlockTypeName, OutCustomData);
}

bool UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForBlockTypeName(UBlockTypeSchemaComponent* SchemaComponent, FGameplayTag BlockTypeName, FInstancedStruct& OutDefinition)
{
	OutDefinition.Reset();
	if (!IsValid(SchemaComponent) || !BlockTypeName.IsValid())
	{
		return false;
	}

	UBlockTypeSchemaRegistry* Registry = SchemaComponent->GetBlockTypeSchemaRegistry();
	if (!IsValid(Registry))
	{
		return false;
	}

	return Registry->TryGetBlockDefinition(BlockTypeName, OutDefinition);
}

bool UChunkWorldBlockHitBlueprintLibrary::TryBroadcastDestroyedFeedbackForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit)
{
	if (!ResolvedHit.bHasBlock || !IsValid(ResolvedHit.ChunkWorld))
	{
		return false;
	}

	UChunkWorldBlockFeedbackComponent* FeedbackComponent = nullptr;
	if (!GetBlockFeedbackComponentFromChunkWorld(ResolvedHit.ChunkWorld, FeedbackComponent))
	{
		return false;
	}

	return FeedbackComponent->BroadcastAuthoritativeDestroyFeedback(ResolvedHit);
}
