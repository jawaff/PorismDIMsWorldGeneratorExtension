// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Contracts/LayoutContractTypes.h"
#include "Layout/Solver/LayoutProfileSolver.h"

/** Worker-safe input for selecting a contract mode plan without touching live assets or terrain. */
struct FLayoutContractModeSelectionInput
{
	/** Region solve request carrying frozen scope, placement, continuation, and policy values. */
	const FLayoutRegionSolveRequest* SolveRequest = nullptr;

	/** Final snapped site center chosen before proof. */
	FIntVector SiteCenterBlockWorldPos = FIntVector::ZeroValue;

	/** World seed used for deterministic diagnostics and future seed derivation. */
	int32 WorldSeed = 0;
};

/** Contract-mode selection helpers shared by runtime, generator, and future adapter entry points. */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContractModeSelection
{
public:
	/** Selects one mode plan from frozen request values only; no terrain, asset, or chunk reads. */
	static FLayoutModePlan SelectModePlan(const FLayoutContractModeSelectionInput& Input);
};
