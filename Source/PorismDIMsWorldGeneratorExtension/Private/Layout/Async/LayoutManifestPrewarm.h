// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Async/LayoutBackgroundAdmissibilityPreflight.h"
#include "Layout/Async/LayoutPreSubmitFrozenSnapshot.h"
#include "Layout/Async/LayoutWorkerSolvePacket.h"

/** Background-only manifest prewarm kind; root solves use this before cheap preflight and solve enqueue. */
enum class ELayoutManifestPrewarmKind : uint8
{
	Root,
	Continuation
};

/** Pointer-free manifest prewarm request. Producers must fill source ids before any worker stage consumes it. */
struct FLayoutManifestPrewarmInput
{
	/** Stable prewarm id used for diagnostics and deterministic lifecycle sequencing. */
	FLayoutId PrewarmId;

	/** Root or continuation manifest prewarm; child manifests are emitted by certified parent workers instead. */
	ELayoutManifestPrewarmKind Kind = ELayoutManifestPrewarmKind::Root;

	/** True only when an upstream producer already supplied the full frozen request manifest. */
	bool bHasFrozenRequestManifest = false;

	/** Pointer-free manifest produced before prewarm dispatch; empty until an authoritative producer exists. */
	FLayoutWorkerSolveRequestManifest FrozenRequestManifest;

	/** True when the pre-submit snapshot payload is packed for descriptor-store consumption. */
	bool bHasPreSubmitSnapshot = false;

	/** Full pointer-free pre-submit snapshot carrying the worker packet and mode plan. */
	FLayoutPreSubmitFrozenSnapshot PreSubmitSnapshot;

	/** True when game-thread terrain evidence was sampled and frozen for background consumption. */
	bool bHasFrozenTerrainBiomeAdapterInput = false;

	/** Pre-sampled pointer-free terrain/biome evidence for structural-contract derivation. */
	FLayoutFrozenTerrainBiomeAdapterInput FrozenTerrainBiomeAdapterInput;
};

/** Result of one background manifest prewarm stage. */
struct FLayoutManifestPrewarmResult
{
	/** True when a pointer-free frozen request manifest is available for cheap preflight. */
	bool bHasFrozenRequestManifest = false;

	/** Frozen request manifest that may feed cheap admissibility preflight. */
	FLayoutWorkerSolveRequestManifest FrozenRequestManifest;

	/** True when prewarm rejected permanently instead of producing a manifest. */
	bool bTerminalRejection = false;

	/** Deterministic rejection or validation diagnostic. */
	FString FailureReason;

	/** True when the structural-contract planner derived planned cells and entries. */
	bool bHasDerivedStructuralContract = false;

	/** Finalized manifest with derived planned cells, terrain evidence, and precomputed adapter output. */
	FLayoutWorkerSolveRequestManifest FinalizedManifest;

	/** Entry cells derived by the structural-contract planner for inter-layout negotiation. */
	TArray<FIntVector> DerivedEntryCells;

	/** Deterministic failure reason when structural-contract derivation failed. */
	FString StructuralContractFailureReason;

	/** True when descriptor production ran the precompute adapter and produced output. */
	bool bHasPrecomputedAdapterOutput = false;

	/** Precomputed adapter output ready for solver fast-path consumption. */
	FLayoutAdapterOutput PrecomputedAdapterOutput;

	/** True when rejected prewarm still has adapter-owned topology for editor diagnostics. */
	bool bHasRejectedAdapterPreview = false;

	/** Non-finalized adapter topology shown only by rejected editor previews; never forwarded to solve. */
	FLayoutAdapterOutput RejectedAdapterPreview;
};

/** Background-only sequencing helpers between manifest prewarm and cheap admissibility preflight. */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutManifestPrewarmSequence
{
public:
	/**
	 * Prepares one pointer-free manifest under an invocation-local execution budget. Nested adapter,
	 * authority and terrain-fallback work shares the deadline/work ledger; stopped output is preview-only.
	 * The ledger is never retained in the returned manifest or transported to another worker invocation.
	 */
	static FLayoutManifestPrewarmResult RunPrewarm(const FLayoutManifestPrewarmInput& Input);

	/** Builds the cheap preflight input from one successful prewarm result without touching live state. */
	static bool TryBuildPreflightInput(
		const FLayoutManifestPrewarmResult& PrewarmResult,
		ELayoutManifestPrewarmKind PrewarmKind,
		FLayoutBackgroundAdmissibilityPreflightInput& OutPreflightInput,
		FString& OutFailureReason);
};
