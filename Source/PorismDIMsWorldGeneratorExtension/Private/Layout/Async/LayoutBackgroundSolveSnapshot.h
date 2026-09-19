// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Solver/LayoutProfileSolver.h"

struct FLayoutWorldBindingRuntimeView;

/** Worker-safe mirror of the world-binding runtime view; stores only value types, ids, tags, policy structs, and soft paths. */
struct FLayoutWorldBindingRuntimeSnapshot
{
	/** Resolved world-facing placement kind for the selected binding/candidate context. */
	ELayoutWorldBindingPlacementKind PlacementKind = ELayoutWorldBindingPlacementKind::None;

	/** Stable world-binding identity carried through planning records and solve requests. */
	FName BindingId;

	/** Stable candidate identity carried through planning records and solve requests. */
	FName CandidateId;

	/** Worker-safe matching biome row selected by the source planning/discovery surface. */
	FName MatchingBiomeRowName;

	/** Pointer-free compatible biome allow-list selected upstream; empty until an authoritative producer exists. */
	TArray<FName> CompatibleBiomeRowNames;

	/** Minimal resolved continuation-selection contract carried by future path/bridge/tunnel world-binding requests. */
	FLayoutResolvedWorldBindingContinuationSelection ContinuationSelection;

	/** Soft path for the profile selected on the game thread; workers never dereference it. */
	FSoftObjectPath LayoutProfilePath;

	/** Soft path for the content set selected on the game thread; workers never dereference it. */
	FSoftObjectPath ContentSetPath;

	/** Binding-owned ordinary-root endpoint connector tags resolved from the selected candidate. */
	FGameplayTagContainer ExportedConnectorTypeTags;

	/** Shared cell size carried by the current content/module owner when one exists. */
	FIntVector SharedCellSizeInBlocks = FIntVector::ZeroValue;

	/** Request-owned template placement offset carried by the current binding/runtime contract. */
	int32 TemplatePlacementZOffsetBlocks = 0;

	/** Resolved world-facing terrain/alignment policy for this selected placement. */
	FLayoutWorldBindingPlacementPolicy PlacementPolicy;

	/** Resolved continuation-family path policy for this selected placement, when applicable. */
	FLayoutWorldBindingContinuationPolicy ContinuationPolicy;

	/** Root solve budget carried by the current binding/candidate context. */
	FLayoutRootSolveBudgetSettings SolveBudget;

	/** Builds a pointer-free worker snapshot from a bounded game-thread runtime-view capture. */
	static PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutWorldBindingRuntimeSnapshot CaptureFromRuntimeView(const FLayoutWorldBindingRuntimeView& RuntimeView);

	/** Builds a pointer-free worker snapshot from a cached connector candidate without reopening live assets. */
	static PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutWorldBindingRuntimeSnapshot CaptureFromConnectorRecord(const FResolvedLayoutConnectorRecord& ConnectorRecord);

	/** Returns true when the snapshot contains no live UObject-backed carriers. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateNoLiveObjectCarriers(FString& OutFailureReason) const;
};

/** Worker-safety utilities that remove game-thread-only carriers from copied solve data. */
namespace LayoutBackgroundSolveSnapshot
{
	/** Returns one request copy with live source carriers stripped from profile/content/module snapshots. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveRequest MakeWorkerSafeSolveRequest(const FLayoutRegionSolveRequest& Request);

	/** Returns true when one request no longer carries known live asset pointers in solve snapshots. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateWorkerSafeSolveRequest(const FLayoutRegionSolveRequest& Request, FString& OutFailureReason);

	/** Removes live placement carriers from one worker solve result while preserving ids, bundle cells, and soft paths. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void ScrubWorkerSolveResult(FLayoutSolveResult& Result);

	/** Returns true when one solve result no longer carries live module/composite placement pointers. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateWorkerSafeSolveResult(const FLayoutSolveResult& Result, FString& OutFailureReason);
}
