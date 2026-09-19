// Copyright 2026 Spotted Loaf Studio
#pragma once

#include "Layout/Contracts/LayoutContractModeAdapter.h"
#include "Layout/Contracts/LayoutContractPipeline.h"

namespace PorismLayoutContractTestUtilities
{
	/** Exercises adapter preparation followed by assembly, including reuse of frozen prewarm evidence. */
	inline bool PrepareAndBuildRegionContract(
		const FLayoutRegionSolveRequest& Request,
		const FIntVector& Center,
		int32 WorldSeed,
		FLayoutRegionContract& Contract,
		FString& Failure)
	{
		Contract = FLayoutRegionContract();
		const auto Manifest = FLayoutContractPipeline::BuildManifestFromSolveRequest(Request);
		FLayoutContractModeAdapterInput Input;
		Input.SolveRequest = &Request;
		Input.Manifest = &Manifest;
		Input.ModePlan = Request.bHasSelectedModePlan ? Request.SelectedModePlan
			: FLayoutContractPipeline::BuildModePlanFromSolveRequest(Request, Center, WorldSeed);
		FLayoutAdapterOutput Prepared;
		return FLayoutContractModeAdapter::TryPrepareRegionOutput(Input, Prepared, Failure)
			&& FLayoutContractPipeline::TryBuildPreparedRegionContract(Manifest, Request.FootprintSize, Prepared, Contract, Failure);
	}
}
