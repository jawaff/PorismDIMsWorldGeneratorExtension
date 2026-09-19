// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Async/LayoutWorkerSolvePacket.h"

/** Background-only cheap preflight kind; used after manifest prewarm and before solve enqueue. */
enum class ELayoutBackgroundAdmissibilityPreflightKind : uint8
{
	Root,
	Continuation
};

/** Pointer-free cheap admissibility input copied from an already-frozen request manifest. */
struct FLayoutBackgroundAdmissibilityPreflightInput
{
	/** Root or continuation preflight; child jobs skip this stage after certified handoff. */
	ELayoutBackgroundAdmissibilityPreflightKind Kind = ELayoutBackgroundAdmissibilityPreflightKind::Root;

	/** Frozen request manifest produced before this preflight runs. */
	FLayoutWorkerSolveRequestManifest RequestManifest;
};

/** Cheap preflight result; success means candidate may be queued for real solve, not that proof passed. */
struct FLayoutBackgroundAdmissibilityPreflightResult
{
	/** True when the candidate passes cheap pointer-free admissibility checks. */
	bool bAdmissible = false;

	/** Deterministic diagnostic emitted when the preflight rejects. */
	FString FailureReason;
};

/** Runs cheap pointer-free root/continuation admissibility checks without proof, terrain-fit, chunk-apply, or live reads. */
// Exported for companion automation coverage while header remains private to layout async implementation.
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutBackgroundAdmissibilityPreflight
{
public:
	/** Evaluates only already-frozen manifest/mode/terrain evidence and returns a fail-closed result. */
	static FLayoutBackgroundAdmissibilityPreflightResult Run(const FLayoutBackgroundAdmissibilityPreflightInput& Input);
};
