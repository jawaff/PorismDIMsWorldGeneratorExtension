// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Async/LayoutWorkerSolvePacket.h"

// Forward declarations to break include cycle with LayoutManifestPrewarm.h
enum class ELayoutManifestPrewarmKind : uint8;
struct FLayoutManifestPrewarmInput;

/**
 * Pointer-free pre-submit snapshot payload consumed by descriptor-store paths.
 *
 * Contains a fully captured worker packet with frozen request manifest and mode plan.
 * Runtime no longer builds this snapshot from live root or connector inputs; upstream
 * scout/prewarm producers must provide it before solve submission can proceed.
 */
struct FLayoutPreSubmitFrozenSnapshot
{
	/** Stable snapshot id for diagnostics and prewarm identity. */
	FLayoutId SnapshotId;

	/** Root or continuation prewarm kind represented by this snapshot; always set explicitly by callers. */
	ELayoutManifestPrewarmKind PrewarmKind;

	/** True when a fully captured pointer-free worker packet has been produced. */
	bool bHasWorkerSolvePacket = false;

	/** Pointer-free worker packet captured before submit; contains manifest and solve-stage inputs. */
	FLayoutWorkerSolvePacket WorkerSolvePacket;

	/**
	 * Returns true when the snapshot carries no live UObject-backed pointers
	 * and is safe for background prewarm submission.
	 */
	bool ValidateNoLiveObjectCarriers(FString& OutFailureReason) const;

	/** Builds the background manifest-prewarm input from the frozen packet manifest. */
	bool TryBuildManifestPrewarmInput(FLayoutManifestPrewarmInput& OutInput, FString& OutFailureReason) const;
};
