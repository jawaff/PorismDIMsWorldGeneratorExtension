// Copyright 2026 Spotted Loaf Studio

#include "Layout/Async/LayoutRegionPrewarmDescriptorProducer.h"

namespace LayoutFrozenSubmissionDescriptorProducer
{
	bool TryBuildProducedDescriptorArtifactFromSelectedRootScoutResult(
		const FLayoutSelectedRootScoutResultPrewarmDescriptorInput& Input,
		FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
		FString& OutFailureReason);

	bool TryBuildProducedDescriptorArtifactFromSelectedContinuationScoutResult(
		const FLayoutSelectedContinuationScoutResultPrewarmDescriptorInput& Input,
		FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
		FString& OutFailureReason);

	bool TryBuildProducedDescriptorArtifactFromSelectedChildScoutResult(
		const FLayoutSelectedChildScoutResultPrewarmDescriptorInput& Input,
		FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
		FString& OutFailureReason);

	bool TryGetSeedForSolveSubmissionInternal(
		const FLayoutProducedFrozenDescriptorArtifact* ProducedArtifact,
		FLayoutFrozenSubmissionDescriptorSeed& OutSeed,
		FString& OutFailureReason);

	bool TryGetManifestPrewarmInputForSolveSubmissionInternal(
		const FLayoutProducedFrozenDescriptorArtifact* ProducedArtifact,
		FLayoutManifestPrewarmInput& OutInput,
		FString& OutFailureReason);
}

namespace
{
	bool ValidateRegionPrewarmDescriptorIdentity(
		const FLayoutRegionPrewarmDescriptorInput& Input,
		FString& OutFailureReason)
	{
		if (Input.ArtifactId.IsNone())
		{
			OutFailureReason = TEXT("Region prewarm descriptor requires a produced descriptor artifact id.");
			return false;
		}
		if (Input.DescriptorId.IsNone())
		{
			OutFailureReason = TEXT("Region prewarm descriptor requires a frozen descriptor id.");
			return false;
		}
		if (Input.RegionGroupId == 0)
		{
			OutFailureReason = TEXT("Region prewarm descriptor requires a non-zero region group id.");
			return false;
		}
		if (Input.AttemptIndex < 0)
		{
			OutFailureReason = TEXT("Region prewarm descriptor requires a nonnegative attempt index.");
			return false;
		}
		return true;
	}

	ELayoutFrozenSubmissionRegionKind GetExpectedDescriptorKind(const ELayoutRegionPrewarmDescriptorSourceKind SourceKind)
	{
		switch (SourceKind)
		{
		case ELayoutRegionPrewarmDescriptorSourceKind::RootScoutResult:
			return ELayoutFrozenSubmissionRegionKind::Root;
		case ELayoutRegionPrewarmDescriptorSourceKind::ContinuationEdgeScoutResult:
			return ELayoutFrozenSubmissionRegionKind::Continuation;
		case ELayoutRegionPrewarmDescriptorSourceKind::ChildScoutResult:
			return ELayoutFrozenSubmissionRegionKind::Child;
		default:
			return ELayoutFrozenSubmissionRegionKind::Root;
		}
	}

	bool ValidateProducedArtifactMatchesInput(
		const FLayoutRegionPrewarmDescriptorInput& Input,
		const FLayoutProducedFrozenDescriptorArtifact& Artifact,
		FString& OutFailureReason)
	{
		if (!Artifact.bHasProducedArtifact)
		{
			OutFailureReason = TEXT("Region prewarm descriptor producer did not emit a produced artifact.");
			return false;
		}
		if (Artifact.ArtifactId != Input.ArtifactId
			|| Artifact.DescriptorSeed.DescriptorId != Input.DescriptorId
			|| Artifact.DescriptorSeed.RegionGroupId != Input.RegionGroupId
			|| Artifact.DescriptorSeed.Generation != Input.Generation
			|| Artifact.DescriptorSeed.AttemptIndex != Input.AttemptIndex)
		{
			OutFailureReason = TEXT("Region prewarm descriptor producer emitted stale descriptor identity.");
			return false;
		}
		if (Artifact.DescriptorSeed.RegionKind != GetExpectedDescriptorKind(Input.SourceKind))
		{
			OutFailureReason = TEXT("Region prewarm descriptor producer emitted a descriptor kind that does not match the source kind.");
			return false;
		}
		return true;
	}

	bool ValidateSourceKindMatchesFrozenSnapshot(
		const FLayoutRegionPrewarmDescriptorInput& Input,
		FString& OutFailureReason)
	{
		if (!Input.Snapshot.bHasWorkerSolvePacket)
		{
			OutFailureReason = TEXT("Region prewarm descriptor source kind requires a frozen worker packet.");
			return false;
		}

		switch (Input.SourceKind)
		{
		case ELayoutRegionPrewarmDescriptorSourceKind::RootScoutResult:
			if (Input.Snapshot.PrewarmKind != ELayoutManifestPrewarmKind::Root)
			{
				OutFailureReason = TEXT("Root scout descriptor source kind requires a root prewarm snapshot.");
				return false;
			}
			if (Input.Snapshot.WorkerSolvePacket.Kind != ELayoutWorkerSolvePacketKind::PlanningRoot
				&& Input.Snapshot.WorkerSolvePacket.Kind != ELayoutWorkerSolvePacketKind::ObservedFallbackRoot
				&& Input.Snapshot.WorkerSolvePacket.Kind != ELayoutWorkerSolvePacketKind::ExplicitPreviewRoot)
			{
				OutFailureReason = TEXT("Root scout descriptor source kind requires a root worker packet.");
				return false;
			}
			return true;
		case ELayoutRegionPrewarmDescriptorSourceKind::ContinuationEdgeScoutResult:
			if (Input.Snapshot.PrewarmKind != ELayoutManifestPrewarmKind::Continuation)
			{
				OutFailureReason = TEXT("Continuation edge descriptor source kind requires a continuation prewarm snapshot.");
				return false;
			}
			if (Input.Snapshot.WorkerSolvePacket.Kind != ELayoutWorkerSolvePacketKind::Continuation)
			{
				OutFailureReason = TEXT("Continuation edge descriptor source kind requires a continuation worker packet.");
				return false;
			}
			return true;
		case ELayoutRegionPrewarmDescriptorSourceKind::ChildScoutResult:
			if (Input.Snapshot.WorkerSolvePacket.Kind != ELayoutWorkerSolvePacketKind::Child)
			{
				OutFailureReason = TEXT("Child scout descriptor source kind requires a child worker packet.");
				return false;
			}
			return true;
		default:
			OutFailureReason = TEXT("Region prewarm descriptor rejected an unknown selected result kind.");
			return false;
		}
	}

	bool ValidateProducedArtifactKindForConsumption(
	const ELayoutRegionPrewarmDescriptorSourceKind ExpectedSourceKind,
	const FLayoutProducedFrozenDescriptorArtifact* const ProducedArtifact,
	FString& OutFailureReason)
{
	if (ProducedArtifact == nullptr || !ProducedArtifact->bHasProducedArtifact)
	{
		OutFailureReason = TEXT("Region prewarm descriptor consumption requires an explicit produced descriptor artifact.");
		return false;
	}
	const ELayoutFrozenSubmissionRegionKind ExpectedDescriptorKind = GetExpectedDescriptorKind(ExpectedSourceKind);
	if (ProducedArtifact->DescriptorSeed.RegionKind != ExpectedDescriptorKind)
	{
		OutFailureReason = TEXT("Region prewarm descriptor consumption rejected a descriptor kind that does not match the expected source kind.");
		return false;
	}
	return true;
}
}

bool LayoutRegionPrewarmDescriptorProducer::TryBuildProducedDescriptorArtifact(
	const FLayoutRegionPrewarmDescriptorInput& Input,
	FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedFrozenDescriptorArtifact();
	OutFailureReason.Reset();

	if (!ValidateRegionPrewarmDescriptorIdentity(Input, OutFailureReason))
	{
		return false;
	}
	if (Input.SelectedResultId.IsNone())
	{
		OutFailureReason = TEXT("Region prewarm descriptor requires a selected scout or branch result id.");
		return false;
	}
	if (!ValidateSourceKindMatchesFrozenSnapshot(Input, OutFailureReason))
	{
		return false;
	}

	switch (Input.SourceKind)
	{
	case ELayoutRegionPrewarmDescriptorSourceKind::RootScoutResult:
	{
		FLayoutSelectedRootScoutResultPrewarmDescriptorInput RootInput;
		RootInput.ArtifactId = Input.ArtifactId;
		RootInput.DescriptorId = Input.DescriptorId;
		RootInput.ScoutResultId = Input.SelectedResultId;
		RootInput.RegionGroupId = Input.RegionGroupId;
		RootInput.Generation = Input.Generation;
		RootInput.AttemptIndex = Input.AttemptIndex;
		RootInput.FinalizedSiteCenterBlockWorldPos = Input.FinalizedPrimaryBlockWorldPos;
		RootInput.WorldSeed = Input.WorldSeed;
		RootInput.Snapshot = Input.Snapshot;
		if (!LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedRootScoutResult(
				RootInput,
				OutArtifact,
				OutFailureReason))
		{
			return false;
		}
		return ValidateProducedArtifactMatchesInput(Input, OutArtifact, OutFailureReason);
	}
	case ELayoutRegionPrewarmDescriptorSourceKind::ContinuationEdgeScoutResult:
	{
		FLayoutSelectedContinuationScoutResultPrewarmDescriptorInput ContinuationInput;
		ContinuationInput.ArtifactId = Input.ArtifactId;
		ContinuationInput.DescriptorId = Input.DescriptorId;
		ContinuationInput.EdgeScoutResultId = Input.SelectedResultId;
		ContinuationInput.RegionGroupId = Input.RegionGroupId;
		ContinuationInput.Generation = Input.Generation;
		ContinuationInput.AttemptIndex = Input.AttemptIndex;
		ContinuationInput.ExpectedSolveSeed = Input.ExpectedSolveSeed;
		ContinuationInput.FinalizedPrimaryBlockWorldPos = Input.FinalizedPrimaryBlockWorldPos;
		ContinuationInput.Snapshot = Input.Snapshot;
		if (!LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedContinuationScoutResult(
				ContinuationInput,
				OutArtifact,
				OutFailureReason))
		{
			return false;
		}
		return ValidateProducedArtifactMatchesInput(Input, OutArtifact, OutFailureReason);
	}
	case ELayoutRegionPrewarmDescriptorSourceKind::ChildScoutResult:
	{
		FLayoutSelectedChildScoutResultPrewarmDescriptorInput ChildInput;
		ChildInput.ArtifactId = Input.ArtifactId;
		ChildInput.DescriptorId = Input.DescriptorId;
		ChildInput.ChildScoutResultId = Input.SelectedResultId;
		ChildInput.RegionGroupId = Input.RegionGroupId;
		ChildInput.Generation = Input.Generation;
		ChildInput.AttemptIndex = Input.AttemptIndex;
		ChildInput.ExpectedSolveSeed = Input.ExpectedSolveSeed;
		ChildInput.ExpectedStableChildKey = Input.ExpectedStableChildKey;
		ChildInput.ExpectedProofCertificateId = Input.ExpectedProofCertificateId;
		ChildInput.FinalizedPrimaryBlockWorldPos = Input.FinalizedPrimaryBlockWorldPos;
		ChildInput.Snapshot = Input.Snapshot;
		if (!LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedChildScoutResult(
				ChildInput,
				OutArtifact,
				OutFailureReason))
		{
			return false;
		}
		return ValidateProducedArtifactMatchesInput(Input, OutArtifact, OutFailureReason);
	}
	default:
		OutFailureReason = TEXT("Region prewarm descriptor rejected an unknown selected result kind.");
		return false;
	}
}

bool LayoutRegionPrewarmDescriptorProducer::TryGetDescriptorSeedForSolveSubmission(
	const ELayoutRegionPrewarmDescriptorSourceKind ExpectedSourceKind,
	const FLayoutProducedFrozenDescriptorArtifact* const ProducedArtifact,
	FLayoutFrozenSubmissionDescriptorSeed& OutSeed,
	FString& OutFailureReason)
{
	OutSeed = FLayoutFrozenSubmissionDescriptorSeed();
	OutFailureReason.Reset();
	if (!ValidateProducedArtifactKindForConsumption(
			ExpectedSourceKind,
			ProducedArtifact,
			OutFailureReason))
	{
		return false;
	}
	return LayoutFrozenSubmissionDescriptorProducer::TryGetSeedForSolveSubmissionInternal(
		ProducedArtifact,
		OutSeed,
		OutFailureReason);
}

bool LayoutRegionPrewarmDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmission(
	const ELayoutRegionPrewarmDescriptorSourceKind ExpectedSourceKind,
	const FLayoutProducedFrozenDescriptorArtifact* const ProducedArtifact,
	FLayoutManifestPrewarmInput& OutInput,
	FString& OutFailureReason)
{
	OutInput = FLayoutManifestPrewarmInput();
	OutFailureReason.Reset();
	if (!ValidateProducedArtifactKindForConsumption(
			ExpectedSourceKind,
			ProducedArtifact,
			OutFailureReason))
	{
		return false;
	}
	return LayoutFrozenSubmissionDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmissionInternal(
		ProducedArtifact,
		OutInput,
		OutFailureReason);
}
