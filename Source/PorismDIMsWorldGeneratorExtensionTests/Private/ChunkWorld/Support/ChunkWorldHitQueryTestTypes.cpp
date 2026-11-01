// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Support/ChunkWorldHitQueryTestTypes.h"

TArray<int> AChunkWorldHitQueryTestWorld::GetBlockValuesByBlockWorldPosLevel(const TArray<FIntVector>& Positions,
    const CChunkData* Data, ERessourceType Type, int CustomDataIndex)
{
    if (Type != ERessourceType::MaterialIndex)
        return Super::GetBlockValuesByBlockWorldPosLevel(Positions, Data, Type, CustomDataIndex);
    ++MaterialReadBatches;
    MaterialReads.Append(Positions);
    TArray<int> Result;
    for (const FIntVector& Position : Positions)
    {
        const int32* Value = MaterialValues.Find(Position);
        Result.Add(Value ? *Value : EmptyMaterial);
    }
    return Result;
}
