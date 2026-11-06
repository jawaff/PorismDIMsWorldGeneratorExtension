// Copyright 2026 Spotted Loaf Studio
#include "ChunkWorldExtended/ChunkWorldCollisionCoverage.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "Chunk/Chunk.h"
#include "ChunkComponent/ChunkComponent.h"
#include "Misc/ScopeExit.h"
namespace ChunkWorldCollision
{
	/** Read native finest-collision readiness under its visibility/topology fence.
	 * Ready includes empty chunks; navigation export and navigation demand are irrelevant. */
	bool HasFinestCollisionCoverageInBounds(AChunkWorldExtended& World, const FBox& WorldBounds)
	{
		if (!World.IsRunning() || !World.WorldGenDef || !World.GetPrimaryComponent() || !WorldBounds.IsValid) return false;
		const CChunkData* Layer = nullptr;
		int32 LayerIndex = INDEX_NONE;
		for (int32 Index = 0; Index < static_cast<int32>(World.WorldChunks.size()); ++Index)
		{
			const CChunkData* Candidate = World.WorldChunks[Index];
			if (Candidate->Collision || Candidate->InstancedMeshCollision) { Layer = Candidate; LayerIndex = Index; }
		}
		// A world configured without voxel collision can still settle on ordinary scene geometry.
		if (!Layer) return true;
		const FBox Local = WorldBounds.TransformBy(World.GetPrimaryComponent()->GetComponentTransform().Inverse());
		const FVector Size(Layer->ChunkSize);
		if (Size.ContainsNaN() || Size.GetMin() <= 0.0 || Local.Min.ContainsNaN() || Local.Max.ContainsNaN()) return false;
		const FVector GridMin = Local.Min / Size;
		const FVector GridMax = Local.Max / Size;
		if (GridMin.GetMin() < double(MIN_int32) + 2.0 || GridMax.GetMax() > double(MAX_int32) - 2.0) return false;
		const FIntVector Min(FMath::FloorToInt(GridMin.X), FMath::FloorToInt(GridMin.Y), FMath::FloorToInt(GridMin.Z));
		const FIntVector Max(FMath::FloorToInt(GridMax.X), FMath::FloorToInt(GridMax.Y), FMath::FloorToInt(GridMax.Z));
		const FVector Count = FVector(Max) - FVector(Min) + FVector::OneVector;
		if (Count.X * Count.Y * Count.Z > 4096.0) return false;
		bool bExpected = false;
		if (!World.CheckChunkPosesIsRunning.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel)) return false;
		ON_SCOPE_EXIT { World.CheckChunkPosesIsRunning.store(false, std::memory_order_release); };
		{
			std::lock_guard<std::mutex> Updates(World.ChunkUpdateCalls.Mutex);
			if (LayerIndex < static_cast<int32>(World.ChunkUpdateCalls.Data.size()))
				for (const FIntVector& Key : World.ChunkUpdateCalls.Data[LayerIndex])
					if (Key.X >= Min.X - 1 && Key.X <= Max.X + 1 && Key.Y >= Min.Y - 1 && Key.Y <= Max.Y + 1 && Key.Z >= Min.Z - 1 && Key.Z <= Max.Z + 1)
						return false;
		}
		std::lock_guard<std::mutex> Roots(World.ChunkStack.Mutex);
		for (int32 X = Min.X; X <= Max.X; ++X)
		for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
		for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
		{
			const FVector Point = (FVector(X, Y, Z) + FVector(0.5)) * Size;
			const auto Contains = [&World, &Point](const CChunk* Chunk)
			{
				if (!Chunk) return false;
				const FVector Start = FVector(Chunk->Start) * World.WorldGenDef->BaseBlockSize;
				const FVector End = Start + FVector(Chunk->ChunkData->ChunkSize);
				return Point.X >= Start.X && Point.X < End.X && Point.Y >= Start.Y && Point.Y < End.Y && Point.Z >= Start.Z && Point.Z < End.Z;
			};
			CChunk* Chunk = nullptr;
			for (const auto& Bucket : World.ChunkStack.Data)
				for (const auto& Entry : Bucket)
					if (Contains(Entry.Chunk)) Chunk = Entry.Chunk;
			while (Chunk && Chunk->ChunkData != Layer)
			{
				CChunk* Child = nullptr;
				for (CChunk* Candidate : Chunk->Children)
					if (Contains(Candidate)) { Child = Candidate; break; }
				Chunk = Child;
			}
			if (!Chunk || !Chunk->IsReady) return false;
		}
		return true;
	}

}
