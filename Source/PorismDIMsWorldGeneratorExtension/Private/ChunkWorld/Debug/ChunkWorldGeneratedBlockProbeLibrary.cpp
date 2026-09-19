// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Debug/ChunkWorldGeneratedBlockProbeLibrary.h"

#include "ChunkWorld/ChunkWorld.h"
#include "ChunkWorldStructs/ChunkWorldEnums.h"
#include "PorismDIMsWorldGeneratorExtension.h"

namespace
{
	TArray<FIntPoint> BuildProbeOffsets(const FGeneratedBlockColumnProbeRequest& Request)
	{
		TArray<FIntPoint> Offsets;
		const int32 MaxColumns = FMath::Max(1, Request.MaxColumns);

		if (!Request.RelativeColumnOffsets.IsEmpty())
		{
			Offsets.Reserve(FMath::Min(Request.RelativeColumnOffsets.Num(), MaxColumns));
			for (const FIntPoint& Offset : Request.RelativeColumnOffsets)
			{
				if (Offsets.Num() >= MaxColumns)
				{
					break;
				}

				Offsets.Add(Offset);
			}
			return Offsets;
		}

		const int32 Radius = FMath::Max(0, Request.RadiusXYBlocks);
		const int32 Step = FMath::Max(1, Request.StepXYBlocks);
		for (int32 Y = -Radius; Y <= Radius && Offsets.Num() < MaxColumns; Y += Step)
		{
			for (int32 X = -Radius; X <= Radius && Offsets.Num() < MaxColumns; X += Step)
			{
				Offsets.Add(FIntPoint(X, Y));
			}
		}

		return Offsets;
	}

	int32 ReadMaterialIndex(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const bool bUseNonBlockingRead)
	{
		if (bUseNonBlockingRead)
		{
			return ChunkWorld->TryGetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::MaterialIndex, 0);
		}

		return ChunkWorld->GetBlockValueByBlockWorldPos(BlockWorldPos, ERessourceType::MaterialIndex, 0);
	}

	void FinalizeReportSummary(const FGeneratedBlockColumnProbeRequest& Request, FGeneratedBlockProbeReport& Report)
	{
		if (Report.bFoundAnySolid)
		{
			Report.Summary = FString::Printf(
				TEXT("GeneratedBlockProbe Center=%s Columns=%d SolidColumns=%d HighestSolidZRange=[%d,%d]"),
				*Request.CenterBlockWorldPos.ToString(),
				Report.SampledColumnCount,
				Report.SolidColumnCount,
				Report.MinHighestSolidZ,
				Report.MaxHighestSolidZ);
			return;
		}

		Report.Summary = FString::Printf(
			TEXT("GeneratedBlockProbe Center=%s Columns=%d SolidColumns=0"),
			*Request.CenterBlockWorldPos.ToString(),
			Report.SampledColumnCount);
	}
}

bool UChunkWorldGeneratedBlockProbeLibrary::ProbeGeneratedBlockColumns(
	AChunkWorld* ChunkWorld,
	const FGeneratedBlockColumnProbeRequest& Request,
	FGeneratedBlockProbeReport& OutReport)
{
	OutReport = FGeneratedBlockProbeReport();

	if (ChunkWorld == nullptr)
	{
		OutReport.Summary = TEXT("GeneratedBlockProbe failed: ChunkWorld is null.");
		return false;
	}

	int32 MinZ = Request.MinZBlock;
	int32 MaxZ = Request.MaxZBlock;
	if (MinZ > MaxZ)
	{
		Swap(MinZ, MaxZ);
	}

	const int32 RequestedZSpan = MaxZ - MinZ + 1;
	const int32 MaxZSpan = FMath::Max(1, Request.MaxZSpanBlocks);
	if (RequestedZSpan > MaxZSpan)
	{
		MaxZ = MinZ + MaxZSpan - 1;
	}

	const TArray<FIntPoint> Offsets = BuildProbeOffsets(Request);
	OutReport.Columns.Reserve(Offsets.Num());
	OutReport.SampledColumnCount = Offsets.Num();

	for (const FIntPoint& Offset : Offsets)
	{
		FGeneratedBlockColumnProbeResult ColumnResult;
		ColumnResult.BlockWorldXY = FIntPoint(
			Request.CenterBlockWorldPos.X + Offset.X,
			Request.CenterBlockWorldPos.Y + Offset.Y);

		for (int32 Z = MinZ; Z <= MaxZ; ++Z)
		{
			const FIntVector BlockWorldPos(ColumnResult.BlockWorldXY.X, ColumnResult.BlockWorldXY.Y, Z);
			const int32 MaterialIndex = ReadMaterialIndex(ChunkWorld, BlockWorldPos, Request.bUseNonBlockingRead);
			if (MaterialIndex == Request.EmptyMaterialIndex)
			{
				continue;
			}

			if (!ColumnResult.bFoundSolid)
			{
				ColumnResult.bFoundSolid = true;
				ColumnResult.LowestSolidZ = Z;
			}

			ColumnResult.HighestSolidZ = Z;
			ColumnResult.HighestSolidMaterialIndex = MaterialIndex;
			++ColumnResult.SolidBlockCount;
		}

		if (ColumnResult.bFoundSolid)
		{
			if (!OutReport.bFoundAnySolid)
			{
				OutReport.bFoundAnySolid = true;
				OutReport.MinHighestSolidZ = ColumnResult.HighestSolidZ;
				OutReport.MaxHighestSolidZ = ColumnResult.HighestSolidZ;
			}
			else
			{
				OutReport.MinHighestSolidZ = FMath::Min(OutReport.MinHighestSolidZ, ColumnResult.HighestSolidZ);
				OutReport.MaxHighestSolidZ = FMath::Max(OutReport.MaxHighestSolidZ, ColumnResult.HighestSolidZ);
			}

			++OutReport.SolidColumnCount;
		}

		OutReport.Columns.Add(ColumnResult);
	}

	FinalizeReportSummary(Request, OutReport);
	return true;
}

bool UChunkWorldGeneratedBlockProbeLibrary::LogGeneratedBlockColumns(
	AChunkWorld* ChunkWorld,
	const FGeneratedBlockColumnProbeRequest& Request,
	const bool bLogEachColumn,
	FGeneratedBlockProbeReport& OutReport)
{
	const bool bSucceeded = ProbeGeneratedBlockColumns(ChunkWorld, Request, OutReport);
	if (!bSucceeded)
	{
		UE_LOG(LogPorismDIMsWorldGeneratorExtension, Warning, TEXT("%s"), *OutReport.Summary);
		return false;
	}

	UE_LOG(LogPorismDIMsWorldGeneratorExtension, Display, TEXT("%s"), *OutReport.Summary);

	if (bLogEachColumn)
	{
		for (const FGeneratedBlockColumnProbeResult& Column : OutReport.Columns)
		{
			if (Column.bFoundSolid)
			{
				UE_LOG(
					LogPorismDIMsWorldGeneratorExtension,
					Display,
					TEXT("GeneratedBlockProbe Column=(%d,%d) LowestSolidZ=%d HighestSolidZ=%d HighestSolidMaterial=%d SolidCount=%d"),
					Column.BlockWorldXY.X,
					Column.BlockWorldXY.Y,
					Column.LowestSolidZ,
					Column.HighestSolidZ,
					Column.HighestSolidMaterialIndex,
					Column.SolidBlockCount);
			}
			else
			{
				UE_LOG(
					LogPorismDIMsWorldGeneratorExtension,
					Display,
					TEXT("GeneratedBlockProbe Column=(%d,%d) Empty"),
					Column.BlockWorldXY.X,
					Column.BlockWorldXY.Y);
			}
		}
	}

	return true;
}
