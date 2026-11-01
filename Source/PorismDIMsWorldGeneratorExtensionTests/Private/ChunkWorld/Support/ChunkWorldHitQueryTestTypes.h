// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorldHitQueryTestTypes.generated.h"

/** Counts material reads at native dispatch while retaining the initialized world's mesh store. */
UCLASS()
class AChunkWorldHitQueryTestWorld : public AChunkWorldExtended
{
    GENERATED_BODY()
public:
    TMap<FIntVector, int32> MaterialValues;
    TArray<FIntVector> MaterialReads;
    int32 MaterialReadBatches = 0;

    virtual TArray<int> GetBlockValuesByBlockWorldPosLevel(const TArray<FIntVector>& Positions,
        const CChunkData* Data, ERessourceType Type, int CustomDataIndex) override;
};
