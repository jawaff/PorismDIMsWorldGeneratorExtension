// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"

class AChunkWorldExtended;
class UChunkStructureTemplate;

/**
 * Shared automation-only support for building minimal transient chunk worlds that can execute
 * the real layout realization path against Porism save caches.
 */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutTestWorldSupport
{
public:
	/** Initializes one transient running chunk world with a single finest-detail chunk layer. */
	static void InitializeTransientChunkWorld(
		AChunkWorldExtended* World,
		const FIntVector& ChunkBlockSize = FIntVector(16, 16, 16),
		const FName BiomeRowName = TEXT("LayoutTestBiome"));

	/** Tears down the transient chunk-world state previously created for automation coverage. */
	static void ShutdownTransientChunkWorld(AChunkWorldExtended* World);

	/** Writes one solid surface block into the transient world for terrain-aware realization tests. */
	static void WriteSurfaceBlock(AChunkWorldExtended* World, const FIntVector& BlockWorldPos, int32 MaterialIndex = 1);

	/** Adds one simple solid block entry and captures mapping so template validation succeeds. */
	static void ConfigureSolidTemplate(
		UChunkStructureTemplate* Template,
		AChunkWorldExtended* World,
		const FIntVector& SizeInBlocks,
		const FIntVector& RelativeBlockPos = FIntVector::ZeroValue,
		int32 MaterialIndex = 1);
};
