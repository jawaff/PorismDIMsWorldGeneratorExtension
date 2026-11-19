// Copyright 2026 Spotted Loaf Studio
#pragma once
#include "CoreMinimal.h"
class AChunkWorldExtended;
namespace ChunkWorldCollision
{
	/** Current finest voxel collision coverage under native topology/update fences. Empty ready chunks count.
	 * Material-only checks exclude instance-only LODs: a ready foliage layer cannot replace a pending solid voxel collider.
	 * Read-only, no generation or residency request; fails closed for unavailable data or queries over 4096 chunks. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool HasFinestCollisionCoverageInBounds(AChunkWorldExtended& World, const FBox& WorldBounds, bool bMaterialOnly = false);
}
