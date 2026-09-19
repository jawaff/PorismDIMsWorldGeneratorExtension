// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Contracts/LayoutContractModeAdapter.h"
#include "Layout/Streaming/LayoutStreamingWindow.h"

namespace LayoutContractStandardAdapter
{
	/** Mirrors inherited request-owned region data without sampling terrain or reopening live state. */
	bool TryBuildStandardOutput(
		const FLayoutContractModeAdapterInput& Input,
		FLayoutAdapterOutput& OutOutput,
		FString& OutFailureReason)
	{
		OutOutput = FLayoutAdapterOutput();
		OutFailureReason.Reset();
		if (Input.SolveRequest == nullptr)
		{
			OutFailureReason = TEXT("Standard layout adapter requires a finalized solve request.");
			return false;
		}

		OutOutput.bSucceeded = true;
		OutOutput.ModePlan = Input.ModePlan;

		// Generate footprint cells from FootprintSize when the solve request carries
		// no pre-derived planned cells.  This lets the prewarm defer cell derivation
		// to the adapter instead of hand-rolling a flat grid.
		if (Input.SolveRequest->PlannedCells.IsEmpty()
			&& Input.SolveRequest->FootprintSize.X > 0
			&& Input.SolveRequest->FootprintSize.Y > 0)
		{
			const FIntPoint& FootprintInCells = Input.SolveRequest->FootprintSize;
			const int32 MaxX = FootprintInCells.X - 1;
			const int32 MaxY = FootprintInCells.Y - 1;
			TArray<FLayoutPlannedCell> GeneratedCells;
			GeneratedCells.Reserve(FootprintInCells.X * FootprintInCells.Y);
			for (int32 CellY = 0; CellY < FootprintInCells.Y; ++CellY)
			{
				for (int32 CellX = 0; CellX < FootprintInCells.X; ++CellX)
				{
					FLayoutPlannedCell& Cell = GeneratedCells.AddDefaulted_GetRef();
					Cell.Cell = FIntVector(CellX, CellY, 0);
					Cell.Intent = (CellX == 0 || CellY == 0 || CellX == MaxX || CellY == MaxY)
						? ELayoutCellIntent::Boundary : ELayoutCellIntent::Interior;
				}
			}
			OutOutput.PlannedCells = MoveTemp(GeneratedCells);
		}
		else
		{
			OutOutput.PlannedCells = Input.SolveRequest->PlannedCells;
		}

		OutOutput.ActiveCells.Reserve(OutOutput.PlannedCells.Num());
		for (const FLayoutPlannedCell& PlannedCell : OutOutput.PlannedCells)
		{
			FLayoutContractActiveCellRecord& ActiveCell = OutOutput.ActiveCells.AddDefaulted_GetRef();
			ActiveCell.Cell = PlannedCell.Cell;
		}
		OutOutput.SteppedTerrainSupportMap = Input.SolveRequest->SteppedTerrainSupportMap;
		OutOutput.FrozenTerrainContract.ContractId = FLayoutId(*FString::Printf(
			TEXT("StandardRegion.%s"),
			*Input.ModePlan.ModePlanId.ToString()));
		OutOutput.FrozenTerrainContract.SiteCenterBlockWorldPos = Input.ModePlan.SiteCenterBlockWorldPos;
		OutOutput.FrozenTerrainContract.FootprintSizeInCells = Input.SolveRequest->FootprintSize;
		OutOutput.FrozenTerrainContract.SharedCellSizeInBlocks = Input.Manifest != nullptr
			? Input.Manifest->SharedCellSizeInBlocks
			: FIntVector::ZeroValue;
		if (OutOutput.FrozenTerrainContract.SharedCellSizeInBlocks.X > 0
			&& OutOutput.FrozenTerrainContract.SharedCellSizeInBlocks.Y > 0)
		{
			OutOutput.FrozenTerrainContract.FootprintMinBlockWorldPos =
				FLayoutStreamingWindow::ComputeFootprintMinBlockWorldPos(
					Input.ModePlan.SiteCenterBlockWorldPos,
					Input.SolveRequest->FootprintSize,
					OutOutput.FrozenTerrainContract.SharedCellSizeInBlocks);
		}
		return true;
	}
}
