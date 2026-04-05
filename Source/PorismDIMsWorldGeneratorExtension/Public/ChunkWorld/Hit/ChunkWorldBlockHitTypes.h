// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "ChunkWorldBlockHitTypes.generated.h"

class AChunkWorld;
class UBlockTypeSchemaComponent;

/**
 * Identifies which fallback path produced the resolved block hit.
 */
UENUM(BlueprintType)
enum class EChunkWorldBlockHitResolveSource : uint8
{
	None,
	ChunkWorldHit,
	InstancedMeshAnchor,
	LocalProbe
};

/**
 * Reusable resolved block-hit context shared by trace, damage, and schema lookup helpers.
 */
USTRUCT(BlueprintType)
struct FChunkWorldResolvedBlockHit
{
	GENERATED_BODY()

	/**
	 * Chunk world that owns the resolved block.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld")
	TObjectPtr<AChunkWorld> ChunkWorld = nullptr;

	/**
	 * Schema component attached to the resolved chunk world.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld")
	TObjectPtr<UBlockTypeSchemaComponent> BlockTypeSchemaComponent = nullptr;

	/**
	 * True when the hit resolved to a represented block.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	bool bHasBlock = false;

	/**
	 * Resolved block world position.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	FIntVector BlockWorldPos = FIntVector::ZeroValue;

	/**
	 * World-space point used as the representative anchor for the resolved block.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	FVector RepresentativeWorldPos = FVector::ZeroVector;

	/**
	 * Material index for the resolved block.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	int32 MaterialIndex = 0;

	/**
	 * Mesh index for the resolved block.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	int32 MeshIndex = 0;

	/**
	 * Which fallback path selected this hit.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld")
	EChunkWorldBlockHitResolveSource ResolveSource = EChunkWorldBlockHitResolveSource::None;
};
