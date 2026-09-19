// Copyright 2026 Spotted Loaf Studio

#include "Layout/Async/LayoutPreSubmitFrozenSnapshot.h"
#include "Layout/Async/LayoutManifestPrewarm.h"

bool FLayoutPreSubmitFrozenSnapshot::ValidateNoLiveObjectCarriers(FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	if (SnapshotId.IsNone())
	{
		OutFailureReason = TEXT("Pre-submit frozen snapshot requires a stable snapshot id.");
		return false;
	}
	if (!bHasWorkerSolvePacket)
	{
		OutFailureReason = TEXT("Pre-submit frozen snapshot requires a captured worker packet.");
		return false;
	}
	if (!WorkerSolvePacket.bHasRequestManifest)
	{
		OutFailureReason = TEXT("Pre-submit frozen snapshot requires a frozen request manifest.");
		return false;
	}
	return WorkerSolvePacket.ValidateNoLiveObjectCarriers(OutFailureReason);
}

bool FLayoutPreSubmitFrozenSnapshot::TryBuildManifestPrewarmInput(
	FLayoutManifestPrewarmInput& OutInput,
	FString& OutFailureReason) const
{
	OutInput = FLayoutManifestPrewarmInput();
	if (!ValidateNoLiveObjectCarriers(OutFailureReason))
	{
		return false;
	}

	OutInput.PrewarmId = SnapshotId;
	OutInput.Kind = PrewarmKind;
	OutInput.bHasFrozenRequestManifest = true;
	OutInput.FrozenRequestManifest = WorkerSolvePacket.RequestManifest;
	return true;
}
