// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutFrozenSubmissionDescriptorProducer.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutFrozenSubmissionStore.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutManifestPrewarm.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutRegionPrewarmDescriptorProducer.h"

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Contracts/LayoutContractPlacementCandidates.h"
#include "Layout/Contracts/LayoutContractModeSelection.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"
#include "Layout/Solver/LayoutSolveExecutionBudget.h"

#include "Misc/AutomationTest.h"

namespace
{
	FLayoutPreSubmitFrozenSnapshot BuildTestFrozenSubmissionSnapshot(UObject* Outer)
	{
		ULayoutProfileAsset* const Profile = NewObject<ULayoutProfileAsset>(Outer);
		ULayoutRegionContentSetAsset* const ContentSet = NewObject<ULayoutRegionContentSetAsset>(Outer);
		Profile->ContentSet = ContentSet;

		FLayoutWorldBindingRuntimeView RuntimeView;
		RuntimeView.BindingId = TEXT("FrozenSubmissionBinding");
		RuntimeView.CandidateId = TEXT("FrozenSubmissionCandidate");
		RuntimeView.LayoutProfile = Profile;
		RuntimeView.ContentSet = ContentSet;
		RuntimeView.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
		RuntimeView.TemplatePlacementZOffsetBlocks = 4;

		FPlannedLayoutSiteRecord PendingRecord;
		FLayoutPlannedSiteReservationSourceSelection ReservationSelection;
		ReservationSelection.ReservationKey = FIntPoint(3, 5);
		ReservationSelection.SiteCenterBlockWorldPos = FIntVector(48, 80, 16);
		PendingRecord.SetPlannedSiteReservationSourceSelection(ReservationSelection);
		FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata;
		LifecycleMetadata.StableRecordKey = TEXT("FrozenSubmission.Record");
		PendingRecord.SetPlannedSiteLifecycleMetadata(LifecycleMetadata);
		FLayoutSiteSolveSourceSelection SolveSourceSelection;
		SolveSourceSelection.SolveSeed = 1187;
		PendingRecord.SetSiteSolveSourceSelection(SolveSourceSelection);
		FLayoutWorldBindingSiteFrontendSelection FrontendSelection;
		FrontendSelection.BiomeRowName = TEXT("FrozenSubmissionBiome");
		PendingRecord.SetWorldBindingFrontendSelection(FrontendSelection);

		FLayoutRegionSolveRequest Request;
		Request.Seed = 1187;
		Request.TemplatePlacementZOffsetBlocks = 4;
		Request.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
		Request.ProfileSnapshot.bSupportsSteppedTerrainSolve = true;
		Request.ProfileSnapshot.bEnableTerrainSeams = false;
		Request.WorldBindingId = RuntimeView.BindingId;
		Request.ProfileSnapshot.SourceProfile = Profile;
		Request.ContentSetSnapshot.SourceContentSet = ContentSet;

		FLayoutWorkerSolvePacket WorkerPacket = FLayoutWorkerSolvePacket::CapturePlanningRoot(
			TEXT("FrozenSubmission Root"),
			RuntimeView,
			PendingRecord,
			0);
		WorkerPacket.RequestManifest = FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(Request);
	WorkerPacket.bHasRequestManifest = true;
		WorkerPacket.bHasSelectedModePlan = true;
		WorkerPacket.SelectedModePlan.Scope = ELayoutContractRegionScope::Root;
		WorkerPacket.SelectedModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::NonSteppedWorldPlacement;
		WorkerPacket.SelectedModePlan.ModePlanId = TEXT("ModePlan.Root.Test");
		WorkerPacket.RequestManifest.bHasSelectedModePlan = true;
		WorkerPacket.RequestManifest.SelectedModePlan = WorkerPacket.SelectedModePlan;
		WorkerPacket.RequestManifest.bHasRootPlacementSubmission = true;
		WorkerPacket.RequestManifest.RootSiteCenterBlockWorldPos = ReservationSelection.SiteCenterBlockWorldPos;
		WorkerPacket.RequestManifest.RootReservationKey = ReservationSelection.ReservationKey;

		FLayoutPreSubmitFrozenSnapshot Snapshot;
		Snapshot.SnapshotId = TEXT("FrozenSubmission.Root.0");
		Snapshot.PrewarmKind = ELayoutManifestPrewarmKind::Root;
		Snapshot.bHasWorkerSolvePacket = true;
		Snapshot.WorkerSolvePacket = MoveTemp(WorkerPacket);
		return Snapshot;
	}

	/** Supplies one frozen flat support cell for descriptor tests, then selects the real mode for their final packet metadata. */
	void PrepareTerrainPacketForTest(FLayoutWorkerSolvePacket& Packet, const FIntVector SiteCenter, const int32 WorldSeed)
	{
		FLayoutRegionSolveRequest Request;
		Packet.RequestManifest.PopulateSolveRequest(Request);
		Request.Seed = Packet.SolveSeed;
		Request.RootPlacementKind = Packet.RuntimeSnapshot.PlacementKind;
		Request.RootContinuationSelection = Packet.RuntimeSnapshot.ContinuationSelection;
		Request.FootprintSize = FIntPoint(1, 1);
		Request.PlannedCells.Add({FIntVector::ZeroValue, ELayoutCellIntent::Interior});
		Request.ContentSetSnapshot.SharedCellSizeInBlocks = Packet.RuntimeSnapshot.SharedCellSizeInBlocks;
		FLayoutSteppedTerrainSupportSample& Support = Request.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
		Support.LocalCell = FIntVector::ZeroValue;
		Support.SupportSurfaceZ = 15;
		Support.SnappedSupportFloorZ = 0;
		Support.SnappedSupportCeilingZ = 16;
		Request.bHasFrozenTerrainBiomeAdapterInput = true;
		Request.FrozenTerrainBiomeAdapterInput = FLayoutTerrainSampling::BuildFrozenTerrainBiomeAdapterInputFromSteppedSupportMap(
			SiteCenter, FIntVector(SiteCenter.X, SiteCenter.Y, 0), Packet.RuntimeSnapshot.SharedCellSizeInBlocks,
			FIntPoint(16, 16), Request.WorldBindingPlacementPolicy.SurfaceSearch,
			Request.WorldBindingPlacementPolicy.TerrainTransition, FLayoutNoiseCoordinateSettings(), 16,
			TEXT("FrozenSubmissionBiome"), {}, Request.SteppedTerrainSupportMap);
		FLayoutContractModeSelectionInput ModeInput;
		ModeInput.SolveRequest = &Request;
		ModeInput.SiteCenterBlockWorldPos = SiteCenter;
		ModeInput.WorldSeed = WorldSeed;
		Packet.SelectedModePlan = FLayoutContractModeSelection::SelectModePlan(ModeInput);
		Request.SelectedModePlan = Packet.SelectedModePlan;
		Request.bHasSelectedModePlan = true;
		Packet.RequestManifest = FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(Request);
	}

	/** Metadata-only snapshots intentionally exercise early rejection; positive root producers also need terrain and a real mode id. */
	FLayoutPreSubmitFrozenSnapshot BuildSelectedRootSnapshotForTest(const FIntVector SiteCenter, const int32 WorldSeed)
	{
		FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestFrozenSubmissionSnapshot(GetTransientPackage());
		auto& Packet = Snapshot.WorkerSolvePacket;
		Packet.SolveSeed = FLayoutContractPlacementCandidates::BuildFinalizedScoutResultSolveSeed(SiteCenter, WorldSeed);
		Packet.RuntimeSnapshot.PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
		PrepareTerrainPacketForTest(Packet, SiteCenter, WorldSeed);
		Packet.RequestManifest.bHasRootPlacementSubmission = true;
		Packet.RequestManifest.RootSiteCenterBlockWorldPos = SiteCenter;
		Packet.RequestManifest.RootReservationKey = FIntPoint(3, 5);
		return Snapshot;
	}

	FLayoutPreSubmitFrozenSnapshot BuildTestContinuationFrozenSubmissionSnapshot(UObject* Outer)
	{
		FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestFrozenSubmissionSnapshot(Outer);
		Snapshot.SnapshotId = TEXT("FrozenSubmission.Continuation.0");
		Snapshot.PrewarmKind = ELayoutManifestPrewarmKind::Continuation;
		Snapshot.WorkerSolvePacket.Kind = ELayoutWorkerSolvePacketKind::Continuation;
		Snapshot.WorkerSolvePacket.DebugName = TEXT("FrozenSubmission Continuation");
		Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos = FIntVector(128, 64, 16);
		Snapshot.WorkerSolvePacket.SecondaryBlockWorldPos = FIntVector(256, 64, 16);
		Snapshot.WorkerSolvePacket.StableNumericKey = 9001;
		Snapshot.WorkerSolvePacket.StableTextKey = TEXT("Continuation.RootSolve");
		Snapshot.WorkerSolvePacket.RuntimeSnapshot.PlacementKind = ELayoutWorldBindingPlacementKind::SurfacePath;
		auto& Selection = Snapshot.WorkerSolvePacket.RuntimeSnapshot.ContinuationSelection;
		Selection.FamilyId = TEXT("FrozenSubmissionPath");
		Selection.PlacementKind = ELayoutWorldBindingPlacementKind::SurfacePath;
		Selection.ResolvedEntryLevel = 0;
		Snapshot.WorkerSolvePacket.RequestManifest.ProfileSnapshot.bRequireAllTraversalChannelsReachable = true;
		PrepareTerrainPacketForTest(Snapshot.WorkerSolvePacket, Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos, 0);
		return Snapshot;
	}

	bool BuildProducedArtifactFromSeedSnapshotForTest(
		const FLayoutId ArtifactId,
		const FLayoutId DescriptorId,
		const uint64 RegionGroupId,
		const uint64 Generation,
		const int32 AttemptIndex,
		const ELayoutFrozenSubmissionRegionKind RegionKind,
		const FLayoutPreSubmitFrozenSnapshot& Snapshot,
		FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
		FString& OutFailureReason)
	{
		FLayoutFrozenSubmissionDescriptorSeed Seed;
		if (!LayoutFrozenSubmissionDescriptorProducer::TryBuildPreflightDescriptorSeedForTests(
				DescriptorId,
				RegionGroupId,
				Generation,
				AttemptIndex,
				RegionKind,
				Snapshot,
				Seed,
				OutFailureReason))
		{
			return false;
		}
		return LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSeedForTests(
			ArtifactId,
			Seed,
			OutArtifact,
			OutFailureReason);
	}

	FLayoutPreSubmitFrozenSnapshot BuildTestChildFrozenSubmissionSnapshot(UObject* Outer)
	{
		FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestFrozenSubmissionSnapshot(Outer);
		Snapshot.SnapshotId = TEXT("FrozenSubmission.Child.0");

		FLayoutChildSolveHandoff Handoff;
		Handoff.ParentRegionDebugPath = TEXT("Parent.Region");
		Handoff.ChildRegionDebugPath = TEXT("Child.Region");
		Handoff.ParentArtifactOrResumeId = TEXT("ParentArtifact.0");
		Handoff.StableChildKey = TEXT("Child.Stable.0");
		Handoff.AttemptIndex = 0;
		Handoff.ChildRegionCellOffset = FIntVector(3, 4, 0);
		Handoff.bHasChildBlockWorldAnchor = true;
		Handoff.ChildBlockWorldAnchor = FIntVector(192, 256, 32);
		Handoff.ContentMetadata.SourceContentEntryId = TEXT("ContentEntry.Child.0");
		Handoff.ContentMetadata.ChildProfilePath = FSoftObjectPath(TEXT("/Game/Layout/Test/ChildProfile.ChildProfile"));
		Handoff.RefreshProofCertificate(TEXT("ChildCertificate.0"));

		FLayoutRegionSolveRequest Request;
		Request.Seed = 2219;
		Request.TemplatePlacementZOffsetBlocks = Snapshot.WorkerSolvePacket.RuntimeSnapshot.TemplatePlacementZOffsetBlocks;
		Request.FootprintSize = FIntPoint(1, 1);
		Request.ContentSetSnapshot.SharedCellSizeInBlocks = Snapshot.WorkerSolvePacket.RuntimeSnapshot.SharedCellSizeInBlocks;
		Request.RegionCellOffset = Handoff.ChildRegionCellOffset;
		Request.RegionDebugPath = Handoff.ChildRegionDebugPath;
		Request.SourceParentRegionDebugPath = Handoff.ParentRegionDebugPath;
		Request.SourceContentEntryId = Handoff.ContentMetadata.SourceContentEntryId;
		Request.ProfilePath = Handoff.ContentMetadata.ChildProfilePath;

		FLayoutWorkerSolvePacket WorkerPacket = FLayoutWorkerSolvePacket::CaptureChild(
			TEXT("FrozenSubmission Child"),
			Snapshot.WorkerSolvePacket.RuntimeSnapshot,
			Handoff,
			Request.Seed,
			0);
		WorkerPacket.RequestManifest = FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(Request);
	WorkerPacket.bHasRequestManifest = true;
		WorkerPacket.bHasSelectedModePlan = true;
		FLayoutContractModeSelectionInput ModeInput;
		ModeInput.SolveRequest = &Request;
		ModeInput.SiteCenterBlockWorldPos = Handoff.ChildBlockWorldAnchor;
		WorkerPacket.SelectedModePlan = FLayoutContractModeSelection::SelectModePlan(ModeInput);
		WorkerPacket.RequestManifest.bHasSelectedModePlan = true;
		WorkerPacket.RequestManifest.SelectedModePlan = WorkerPacket.SelectedModePlan;

		Snapshot.bHasWorkerSolvePacket = true;
		Snapshot.WorkerSolvePacket = MoveTemp(WorkerPacket);
		return Snapshot;
	}

	FPlannedLayoutSiteRecord BuildTestDiscoveredPlanningRecord()
	{
		FPlannedLayoutSiteRecord Record;
		FLayoutPlannedSiteReservationSourceSelection ReservationSelection;
		ReservationSelection.SiteCenterBlockWorldPos = FIntVector(48, 80, 16);
		ReservationSelection.WorldSeed = 1337;
		Record.SetPlannedSiteReservationSourceSelection(ReservationSelection);
		Record.DiscoveryCandidateId = TEXT("RootScout.Discovery.0");
		Record.DiscoveryOrderIndex = 3;
		Record.DiscoveryPlacementShiftCells = FIntVector::ZeroValue;
		Record.DiscoveryPlacementShiftId = FLayoutContractPlacementCandidates::BuildPlacementShiftId(Record.DiscoveryPlacementShiftCells);
		Record.DiscoverySolveSeed = FLayoutContractPlacementCandidates::BuildFinalizedScoutResultSolveSeed(
			ReservationSelection.SiteCenterBlockWorldPos,
			ReservationSelection.WorldSeed);
		Record.DiscoveryOrderingId = TEXT("RootScout.Ordering.0");
		return Record;
	}

	FLayoutFrozenSubmissionDescriptorSeed BuildTestFrozenSubmissionDescriptorSeed(UObject* Outer)
	{
		FLayoutFrozenSubmissionDescriptorSeed Seed;
		Seed.DescriptorId = TEXT("Region.Test.0");
		Seed.RegionGroupId = 42;
		Seed.Generation = 7;
		Seed.AttemptIndex = 0;
		Seed.RegionKind = ELayoutFrozenSubmissionRegionKind::Root;
		Seed.Snapshot = BuildTestFrozenSubmissionSnapshot(Outer);
		return Seed;
	}

	FLayoutFrozenSubmissionDescriptor BuildTestFrozenSubmissionDescriptor(UObject* Outer)
	{
		FLayoutFrozenSubmissionDescriptor Descriptor;
		Descriptor.DescriptorId = TEXT("Region.Test.0");
		Descriptor.RegionGroupId = 42;
		Descriptor.Generation = 7;
		Descriptor.AttemptIndex = 0;
		Descriptor.RegionKind = ELayoutFrozenSubmissionRegionKind::Root;
		Descriptor.Snapshot = BuildTestFrozenSubmissionSnapshot(Outer);
		Descriptor.AuditHash = FLayoutFrozenSubmissionStore::BuildAuditHash(
			Descriptor.DescriptorId,
			Descriptor.RegionGroupId,
			Descriptor.Generation,
			Descriptor.AttemptIndex,
			Descriptor.RegionKind,
			Descriptor.Snapshot);
		return Descriptor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerBuildsPreflightSeedTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerBuildsPreflightSeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerBuildsPreflightSeedTest::RunTest(const FString& Parameters)
{
	const FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestFrozenSubmissionSnapshot(GetTransientPackage());
	FLayoutFrozenSubmissionDescriptorSeed Seed;
	FString FailureReason;
	TestTrue(
		TEXT("Pointer-free snapshot builds preflight descriptor seed"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildPreflightDescriptorSeedForTests(
			TEXT("Region.Test.0"),
			42,
			7,
			0,
			ELayoutFrozenSubmissionRegionKind::Root,
			Snapshot,
			Seed,
			FailureReason));

	FLayoutManifestPrewarmInput PrewarmInput;
	TestTrue(
		TEXT("Descriptor seed builds manifest prewarm input"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildManifestPrewarmInputFromSeedForTests(
			Seed,
			PrewarmInput,
			FailureReason));
	TestTrue(TEXT("Prewarm input carries frozen request manifest"), PrewarmInput.bHasFrozenRequestManifest);
	TestFalse(TEXT("Prewarm input preserves disabled terrain seam mode"), PrewarmInput.FrozenRequestManifest.ProfileSnapshot.bEnableTerrainSeams);

	FLayoutFrozenSubmissionDescriptorSeed InvalidAttemptSeed = Seed;
	InvalidAttemptSeed.AttemptIndex = -1;
	FailureReason.Reset();
	TestFalse(
		TEXT("Manifest prewarm input rejects invalid seed attempt"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildManifestPrewarmInputFromSeedForTests(
			InvalidAttemptSeed,
			PrewarmInput,
			FailureReason));
	TestTrue(TEXT("Invalid seed attempt failure is reported"), FailureReason.Contains(TEXT("attempt")));

	FLayoutFrozenSubmissionDescriptorSeed MismatchedKindSeed = Seed;
	MismatchedKindSeed.RegionKind = ELayoutFrozenSubmissionRegionKind::Continuation;
	FailureReason.Reset();
	TestFalse(
		TEXT("Manifest prewarm input rejects mismatched seed kind"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildManifestPrewarmInputFromSeedForTests(
			MismatchedKindSeed,
			PrewarmInput,
			FailureReason));
	TestTrue(TEXT("Mismatched seed kind failure is reported"), FailureReason.Contains(TEXT("Continuation")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerManifestPrewarmRejectsContinuationWithoutReachabilityTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ManifestPrewarmRejectsContinuationWithoutReachability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerManifestPrewarmRejectsContinuationWithoutResolvedEntryTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ManifestPrewarmRejectsContinuationWithoutResolvedEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerManifestPrewarmAcceptsReachableContinuationMetadataTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ManifestPrewarmAcceptsReachableContinuationMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionManifestPrewarmPublishesReservedOpenTerrainAuthorityTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ManifestPrewarmPublishesReservedOpenTerrainAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionManifestRoundTripsReservedOpenTerrainAuthorityTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ManifestRoundTripsReservedOpenTerrainAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionManifestRoundTripsPreparedChildPlacementHintsTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ManifestRoundTripsPreparedChildPlacementHints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionManifestRoundTripsExternalPlannedNeighborFacesTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ManifestRoundTripsExternalPlannedNeighborFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsMismatchedPreflightSeedKindTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsMismatchedPreflightSeedKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerManifestPrewarmRejectsContinuationWithoutReachabilityTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestContinuationFrozenSubmissionSnapshot(GetTransientPackage());
	Snapshot.WorkerSolvePacket.RequestManifest.RootContinuationSelection.ResolvedEntryLevel = 0;
	Snapshot.WorkerSolvePacket.RequestManifest.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;

	FLayoutManifestPrewarmInput PrewarmInput;
	PrewarmInput.PrewarmId = Snapshot.SnapshotId;
	PrewarmInput.Kind = ELayoutManifestPrewarmKind::Continuation;
	PrewarmInput.bHasFrozenRequestManifest = true;
	PrewarmInput.FrozenRequestManifest = Snapshot.WorkerSolvePacket.RequestManifest;

	const FLayoutManifestPrewarmResult Result = FLayoutManifestPrewarmSequence::RunPrewarm(PrewarmInput);
	TestTrue(TEXT("Continuation prewarm rejects profile snapshots without traversal reachability"), Result.bTerminalRejection);
	TestTrue(TEXT("Continuation reachability failure is reported"), Result.FailureReason.Contains(TEXT("traversal reachability")));
	return true;
}

bool FLayoutFrozenSubmissionDescriptorProducerManifestPrewarmRejectsContinuationWithoutResolvedEntryTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestContinuationFrozenSubmissionSnapshot(GetTransientPackage());
	Snapshot.WorkerSolvePacket.RequestManifest.ProfileSnapshot.bRequireAllTraversalChannelsReachable = true;
	Snapshot.WorkerSolvePacket.RequestManifest.RootContinuationSelection.ResolvedEntryLevel = INDEX_NONE;

	FLayoutManifestPrewarmInput PrewarmInput;
	PrewarmInput.PrewarmId = Snapshot.SnapshotId;
	PrewarmInput.Kind = ELayoutManifestPrewarmKind::Continuation;
	PrewarmInput.bHasFrozenRequestManifest = true;
	PrewarmInput.FrozenRequestManifest = Snapshot.WorkerSolvePacket.RequestManifest;

	const FLayoutManifestPrewarmResult Result = FLayoutManifestPrewarmSequence::RunPrewarm(PrewarmInput);
	TestTrue(TEXT("Continuation prewarm rejects missing resolved entry metadata"), Result.bTerminalRejection);
	TestTrue(TEXT("Continuation resolved-entry failure is reported"), Result.FailureReason.Contains(TEXT("resolved continuation entry")));
	return true;
}

bool FLayoutFrozenSubmissionDescriptorProducerManifestPrewarmAcceptsReachableContinuationMetadataTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestContinuationFrozenSubmissionSnapshot(GetTransientPackage());
	Snapshot.WorkerSolvePacket.RequestManifest.ProfileSnapshot.bRequireAllTraversalChannelsReachable = true;
	Snapshot.WorkerSolvePacket.RequestManifest.RootContinuationSelection.ResolvedEntryLevel = 0;

	FLayoutManifestPrewarmInput PrewarmInput;
	PrewarmInput.PrewarmId = Snapshot.SnapshotId;
	PrewarmInput.Kind = ELayoutManifestPrewarmKind::Continuation;
	PrewarmInput.bHasFrozenRequestManifest = true;
	PrewarmInput.FrozenRequestManifest = Snapshot.WorkerSolvePacket.RequestManifest;

	const FLayoutManifestPrewarmResult Result = FLayoutManifestPrewarmSequence::RunPrewarm(PrewarmInput);
	TestTrue(TEXT("Reachable continuation prewarm reports deterministic outcome"),
		Result.bTerminalRejection || Result.bHasFrozenRequestManifest);
	if (Result.bTerminalRejection)
	{
		AddInfo(FString::Printf(TEXT("Reachable continuation prewarm rejected after request validation: %s"), *Result.FailureReason));
	}
	return true;
}

bool FLayoutFrozenSubmissionManifestPrewarmPublishesReservedOpenTerrainAuthorityTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestFrozenSubmissionSnapshot(GetTransientPackage());
	Snapshot.WorkerSolvePacket.bHasPrecomputedAdapterOutput = true;
	FLayoutAdapterOutput& AdapterOutput = Snapshot.WorkerSolvePacket.PrecomputedAdapterOutput;
	AdapterOutput.bSucceeded = true;
	AdapterOutput.ModePlan = Snapshot.WorkerSolvePacket.SelectedModePlan;
	FLayoutCellReservationRecord& Reservation = AdapterOutput.FrozenTerrainContract.ReservedOpenTerrainReservations.AddDefaulted_GetRef();
	Reservation.ReservationId = TEXT("Prewarm.ClearReservedCell");
	Reservation.Cell = FIntVector(1, 1, 0);
	Reservation.ReservationKind = ELayoutCellReservationKind::ReservedEmpty;
	Reservation.TerrainBehavior = ELayoutReservedOpenTerrainBehavior::ClearReservedCell;

	FLayoutManifestPrewarmInput PrewarmInput;
	PrewarmInput.PrewarmId = Snapshot.SnapshotId;
	PrewarmInput.Kind = ELayoutManifestPrewarmKind::Root;
	PrewarmInput.bHasFrozenRequestManifest = true;
	PrewarmInput.FrozenRequestManifest = Snapshot.WorkerSolvePacket.RequestManifest;
	PrewarmInput.bHasPreSubmitSnapshot = true;
	PrewarmInput.PreSubmitSnapshot = Snapshot;

	const FLayoutManifestPrewarmResult Result = FLayoutManifestPrewarmSequence::RunPrewarm(PrewarmInput);
	TestFalse(TEXT("Precomputed root prewarm succeeds"), Result.bTerminalRejection);
	TestEqual(TEXT("Finalized manifest publishes reserved-open terrain authority"), Result.FinalizedManifest.ReservedOpenTerrainReservations.Num(), 1);
	if (!Result.FinalizedManifest.ReservedOpenTerrainReservations.IsEmpty())
	{
		TestEqual(
			TEXT("Finalized manifest preserves ClearReservedCell behavior"),
			Result.FinalizedManifest.ReservedOpenTerrainReservations[0].TerrainBehavior,
			ELayoutReservedOpenTerrainBehavior::ClearReservedCell);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutManifestPrewarmInheritedBudgetTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ManifestPrewarmInheritedBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Cached adapter output cannot bypass termination of its enclosing prewarm invocation. */
bool FLayoutManifestPrewarmInheritedBudgetTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestFrozenSubmissionSnapshot(GetTransientPackage());
	Snapshot.WorkerSolvePacket.bHasPrecomputedAdapterOutput = true;
	Snapshot.WorkerSolvePacket.PrecomputedAdapterOutput.bSucceeded = true;
	Snapshot.WorkerSolvePacket.PrecomputedAdapterOutput.ModePlan = Snapshot.WorkerSolvePacket.SelectedModePlan;
	FLayoutManifestPrewarmInput Input;
	Input.PrewarmId = Snapshot.SnapshotId;
	Input.bHasFrozenRequestManifest = true;
	Input.FrozenRequestManifest = Snapshot.WorkerSolvePacket.RequestManifest;
	Input.bHasPreSubmitSnapshot = true;
	Input.PreSubmitSnapshot = Snapshot;
	enum class EStop { BeforePreparation, BeforePublication, Deadline };
	for (const EStop Stop : {EStop::BeforePreparation, EStop::BeforePublication, EStop::Deadline})
	{
		LayoutSolveExecution::FScope Scope(0.0, 1);
		auto* Work = LayoutSolveExecution::CurrentThreadLedger();
		const bool bExpired = Stop == EStop::Deadline;
		if (bExpired) Work->DeadlineSeconds = FPlatformTime::Seconds() - 1.0;
		else if (Stop == EStop::BeforePreparation) Work->UsedWorkUnits = Work->MaxWorkUnits;
		const auto Result = FLayoutManifestPrewarmSequence::RunPrewarm(Input);
		TestTrue(TEXT("Prewarm honors inherited stop even on cached adapter path"), Result.bTerminalRejection);
		TestFalse(TEXT("Stopped prewarm publishes no manifest"), Result.bHasFrozenRequestManifest);
		TestFalse(TEXT("Stopped prewarm grants no adapter fast-path authority"), Result.bHasPrecomputedAdapterOutput);
		TestTrue(TEXT("Existing adapter evidence survives for rejected preview"), Result.bHasRejectedAdapterPreview);
		TestTrue(TEXT("Prewarm retains execution diagnostic, not content infeasibility"),
			Result.FailureReason.Contains(bExpired ? TEXT("deadline") : TEXT("work budget")));
		TestTrue(TEXT("Prewarm does not replace caller ledger"), Work == LayoutSolveExecution::CurrentThreadLedger());
	}
	return true;
}

bool FLayoutFrozenSubmissionManifestRoundTripsReservedOpenTerrainAuthorityTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest SourceRequest;
	FLayoutCellReservationRecord& SourceReservation =
		SourceRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations.AddDefaulted_GetRef();
	SourceReservation.ReservationId = TEXT("ManifestRoundTrip.ClearReservedCell");
	SourceReservation.Cell = FIntVector(2, 3, 1);
	SourceReservation.ReservationKind = ELayoutCellReservationKind::ReservedEmpty;
	SourceReservation.TerrainBehavior = ELayoutReservedOpenTerrainBehavior::ClearReservedCell;

	const FLayoutWorkerSolveRequestManifest Manifest =
		FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(SourceRequest);
	TestEqual(TEXT("Manifest captures reserved-open terrain authority"), Manifest.ReservedOpenTerrainReservations.Num(), 1);

	FLayoutRegionSolveRequest RehydratedRequest;
	Manifest.PopulateSolveRequest(RehydratedRequest);
	const TArray<FLayoutCellReservationRecord>& RehydratedReservations =
		RehydratedRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations;
	TestEqual(TEXT("Manifest rehydrates reserved-open terrain authority"), RehydratedReservations.Num(), 1);
	if (!RehydratedReservations.IsEmpty())
	{
		TestEqual(TEXT("Manifest preserves reserved cell"), RehydratedReservations[0].Cell, SourceReservation.Cell);
		TestEqual(
			TEXT("Manifest preserves reserved terrain behavior"),
			RehydratedReservations[0].TerrainBehavior,
			ELayoutReservedOpenTerrainBehavior::ClearReservedCell);
	}
	return true;
}

bool FLayoutFrozenSubmissionManifestRoundTripsPreparedChildPlacementHintsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest SourceRequest;
	FLayoutPreparedChildPlacementHint& SourceHint =
		SourceRequest.PreparedChildPlacementHints.AddDefaulted_GetRef();
	SourceHint.SourceContentEntryId = TEXT("Room");
	SourceHint.InstanceOrdinal = 1;
	SourceHint.RegionCellOffset = FIntVector(2, 0, 0);
	SourceHint.StageMappingId = TEXT("StageMapping.Room.1");

	const FLayoutWorkerSolveRequestManifest Manifest =
		FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(SourceRequest);
	FLayoutRegionSolveRequest RehydratedRequest;
	Manifest.PopulateSolveRequest(RehydratedRequest);

	if (!TestEqual(
		TEXT("Manifest rehydrates one prepared child-placement hint"),
		RehydratedRequest.PreparedChildPlacementHints.Num(),
		1))
	{
		return false;
	}
	const FLayoutPreparedChildPlacementHint& RehydratedHint =
		RehydratedRequest.PreparedChildPlacementHints[0];
	TestEqual(TEXT("Prepared hint preserves child source"), RehydratedHint.SourceContentEntryId, SourceHint.SourceContentEntryId);
	TestEqual(TEXT("Prepared hint preserves instance ordinal"), RehydratedHint.InstanceOrdinal, SourceHint.InstanceOrdinal);
	TestEqual(TEXT("Prepared hint preserves transform"), RehydratedHint.RegionCellOffset, SourceHint.RegionCellOffset);
	return TestEqual(TEXT("Prepared hint preserves stage mapping"), RehydratedHint.StageMappingId, SourceHint.StageMappingId);
}

bool FLayoutFrozenSubmissionManifestRoundTripsExternalPlannedNeighborFacesTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest SourceRequest;
	SourceRequest.ExternalPlannedNeighborFaceMasks.Add(
		FIntVector(2, 3, 1),
		LayoutFaceDirectionMask(ELayoutFaceDirection::PosX)
			| LayoutFaceDirectionMask(ELayoutFaceDirection::NegY));

	const FLayoutWorkerSolveRequestManifest Manifest =
		FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(SourceRequest);
	FLayoutRegionSolveRequest RehydratedRequest;
	Manifest.PopulateSolveRequest(RehydratedRequest);

	return TestEqual(
		TEXT("Manifest preserves external planned-neighbor face masks"),
		RehydratedRequest.ExternalPlannedNeighborFaceMasks.FindRef(FIntVector(2, 3, 1)),
		SourceRequest.ExternalPlannedNeighborFaceMasks.FindRef(FIntVector(2, 3, 1)));
}

bool FLayoutFrozenSubmissionDescriptorProducerRejectsMismatchedPreflightSeedKindTest::RunTest(const FString& Parameters)
{
	const FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestFrozenSubmissionSnapshot(GetTransientPackage());
	FLayoutFrozenSubmissionDescriptorSeed Seed;
	FString FailureReason;
	TestFalse(
		TEXT("Root snapshot cannot seed continuation descriptor"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildPreflightDescriptorSeedForTests(
			TEXT("Region.Test.0"),
			42,
			7,
			0,
			ELayoutFrozenSubmissionRegionKind::Continuation,
			Snapshot,
			Seed,
			FailureReason));
	TestTrue(TEXT("Mismatched seed reports continuation/root mismatch"), FailureReason.Contains(TEXT("Continuation")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerBuildsProducedArtifactFromSnapshotTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerBuildsProducedArtifactFromSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerBuildsProducedArtifactFromSnapshotTest::RunTest(const FString& Parameters)
{
	const FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestFrozenSubmissionSnapshot(GetTransientPackage());
	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit produced artifact builds from frozen snapshot"),
		BuildProducedArtifactFromSeedSnapshotForTest(
			TEXT("Artifact.Region.Test.0"),
			TEXT("Region.Test.0"),
			42,
			7,
			0,
			ELayoutFrozenSubmissionRegionKind::Root,
			Snapshot,
			Artifact,
			FailureReason));

	FLayoutFrozenSubmissionDescriptorSeed SubmitSeed;
	TestTrue(
		TEXT("Produced artifact from snapshot exposes solve-submission seed"),
		LayoutFrozenSubmissionDescriptorProducer::TryGetSeedForSolveSubmissionForTests(
			&Artifact,
			SubmitSeed,
			FailureReason));
	TestEqual(TEXT("Descriptor id preserved"), SubmitSeed.DescriptorId, FLayoutId(TEXT("Region.Test.0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsStoreWithoutRequiredMetadataTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsStoreWithoutRequiredMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerRejectsStoreWithoutRequiredMetadataTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestFrozenSubmissionSnapshot(GetTransientPackage());
	Snapshot.WorkerSolvePacket.bHasSelectedModePlan = false;
	Snapshot.WorkerSolvePacket.RequestManifest.bHasSelectedModePlan = false;
	FLayoutFrozenSubmissionDescriptor Descriptor = LayoutFrozenSubmissionDescriptorProducer::BuildDescriptorFromSnapshotForTests(
		TEXT("Region.Test.MissingMetadata"),
		42,
		7,
		0,
		ELayoutFrozenSubmissionRegionKind::Root,
		Snapshot);
	FLayoutFrozenSubmissionStore Store;
	FString FailureReason;
	TestFalse(
		TEXT("Accepted descriptor storage rejects missing preflight metadata"),
		LayoutFrozenSubmissionDescriptorProducer::StoreAcceptedDescriptorForTests(Store, Descriptor, FailureReason));
	TestTrue(TEXT("Missing metadata storage failure is reported"), FailureReason.Contains(TEXT("selected mode plan")));
	TestNull(
		TEXT("Rejected descriptor is not stored"),
		Store.FindValid(
			Descriptor.DescriptorId,
			Descriptor.RegionGroupId,
			Descriptor.Generation,
			Descriptor.AttemptIndex,
			Descriptor.RegionKind,
			Descriptor.AuditHash,
			FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorLifecycleValidationRejectsStaleIdentityTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.LifecycleValidationRejectsStaleIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorLifecycleValidationRejectsStaleIdentityTest::RunTest(const FString& Parameters)
{
	const FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestFrozenSubmissionSnapshot(GetTransientPackage());
	FLayoutFrozenSubmissionDescriptor Descriptor = LayoutFrozenSubmissionDescriptorProducer::BuildDescriptorFromSnapshotForTests(
		TEXT("Region.Test.0"),
		42,
		7,
		0,
		ELayoutFrozenSubmissionRegionKind::Root,
		Snapshot);
	FLayoutFrozenSubmissionStore Store;
	FString FailureReason;
	TestTrue(TEXT("Valid descriptor enters transient store"), Store.AddOrReplace(Descriptor, FailureReason));
	TestTrue(
		TEXT("Lifecycle validation accepts matching descriptor identity"),
		LayoutFrozenSubmissionDescriptorProducer::ValidateStoredDescriptorForLifecycleUse(
			Store,
			Descriptor.DescriptorId,
			Descriptor.RegionGroupId,
			Descriptor.Generation,
			Descriptor.AttemptIndex,
			Descriptor.RegionKind,
			Descriptor.AuditHash,
			TEXT("Test publish"),
			FailureReason));
	TestFalse(
		TEXT("Lifecycle validation rejects stale generation before enqueue or publish"),
		LayoutFrozenSubmissionDescriptorProducer::ValidateStoredDescriptorForLifecycleUse(
			Store,
			Descriptor.DescriptorId,
			Descriptor.RegionGroupId,
			Descriptor.Generation + 1,
			Descriptor.AttemptIndex,
			Descriptor.RegionKind,
			Descriptor.AuditHash,
			TEXT("Test publish"),
			FailureReason));
	TestTrue(TEXT("Stale generation failure includes lifecycle context"), FailureReason.Contains(TEXT("Test publish")));
	TestFalse(
		TEXT("Lifecycle validation rejects stale audit hash before enqueue or publish"),
		LayoutFrozenSubmissionDescriptorProducer::ValidateStoredDescriptorForLifecycleUse(
			Store,
			Descriptor.DescriptorId,
			Descriptor.RegionGroupId,
			Descriptor.Generation,
			Descriptor.AttemptIndex,
			Descriptor.RegionKind,
			Descriptor.AuditHash + 1,
			TEXT("Test publish"),
			FailureReason));
	TestTrue(TEXT("Stale audit hash failure reports hash mismatch"), FailureReason.Contains(TEXT("audit hash")));
	TestFalse(
		TEXT("Lifecycle validation rejects missing descriptor id before enqueue or publish"),
		LayoutFrozenSubmissionDescriptorProducer::ValidateStoredDescriptorForLifecycleUse(
			Store,
			NAME_None,
			Descriptor.RegionGroupId,
			Descriptor.Generation,
			Descriptor.AttemptIndex,
			Descriptor.RegionKind,
			Descriptor.AuditHash,
			TEXT("Test enqueue"),
			FailureReason));
	TestTrue(TEXT("Missing descriptor id failure reports descriptor id"), FailureReason.Contains(TEXT("descriptor id")));
	TestFalse(
		TEXT("Lifecycle validation rejects missing audit hash before enqueue or publish"),
		LayoutFrozenSubmissionDescriptorProducer::ValidateStoredDescriptorForLifecycleUse(
			Store,
			Descriptor.DescriptorId,
			Descriptor.RegionGroupId,
			Descriptor.Generation,
			Descriptor.AttemptIndex,
			Descriptor.RegionKind,
			0,
			TEXT("Test enqueue"),
			FailureReason));
	TestTrue(TEXT("Missing audit hash failure reports audit hash"), FailureReason.Contains(TEXT("audit hash")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsSnapshotArtifactWithoutModePlanTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsSnapshotArtifactWithoutModePlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsSnapshotArtifactWithoutRootPlacementTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsSnapshotArtifactWithoutRootPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsStaleProducedArtifactAtSolveSubmissionTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsStaleProducedArtifactAtSolveSubmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsStaleProducedArtifactAuditHashesTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsStaleProducedArtifactAuditHashes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsStaleManifestPrewarmPayloadTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsStaleManifestPrewarmPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerBuildsSelectedRootScoutArtifactTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerBuildsSelectedRootScoutArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerRejectsSnapshotArtifactWithoutModePlanTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestFrozenSubmissionSnapshot(GetTransientPackage());
	Snapshot.WorkerSolvePacket.bHasSelectedModePlan = false;
	Snapshot.WorkerSolvePacket.RequestManifest.bHasSelectedModePlan = false;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Generic root descriptor artifact rejects missing selected mode plan"),
		BuildProducedArtifactFromSeedSnapshotForTest(
			TEXT("Artifact.Region.Test.0"),
			TEXT("Region.Test.0"),
			42,
			7,
			0,
			ELayoutFrozenSubmissionRegionKind::Root,
			Snapshot,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing generic selected mode failure reported"), FailureReason.Contains(TEXT("selected mode plan")));
	return true;
}

bool FLayoutFrozenSubmissionDescriptorProducerRejectsSnapshotArtifactWithoutRootPlacementTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestFrozenSubmissionSnapshot(GetTransientPackage());
	Snapshot.WorkerSolvePacket.RequestManifest.bHasRootPlacementSubmission = false;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Generic root descriptor artifact rejects missing root placement submission"),
		BuildProducedArtifactFromSeedSnapshotForTest(
			TEXT("Artifact.Region.Test.0"),
			TEXT("Region.Test.0"),
			42,
			7,
			0,
			ELayoutFrozenSubmissionRegionKind::Root,
			Snapshot,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing generic root placement failure reported"), FailureReason.Contains(TEXT("root placement submission")));
	return true;
}

bool FLayoutFrozenSubmissionDescriptorProducerRejectsStaleProducedArtifactAtSolveSubmissionTest::RunTest(const FString& Parameters)
{
	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit produced artifact builds before mutation"),
		BuildProducedArtifactFromSeedSnapshotForTest(
			TEXT("Artifact.Region.Test.0"),
			TEXT("Region.Test.0"),
			42,
			7,
			0,
			ELayoutFrozenSubmissionRegionKind::Root,
			BuildTestFrozenSubmissionSnapshot(GetTransientPackage()),
			Artifact,
			FailureReason));

	Artifact.DescriptorSeed.Snapshot.WorkerSolvePacket.RequestManifest.bHasRootPlacementSubmission = false;
	FLayoutFrozenSubmissionDescriptorSeed SubmitSeed;
	TestFalse(
		TEXT("Solve submission rejects produced artifacts whose descriptor payload went stale after production"),
		LayoutFrozenSubmissionDescriptorProducer::TryGetSeedForSolveSubmissionForTests(
			&Artifact,
			SubmitSeed,
			FailureReason));
	TestTrue(TEXT("Stale produced artifact failure reports root placement submission"), FailureReason.Contains(TEXT("root placement submission")));
	return true;
}

bool FLayoutFrozenSubmissionDescriptorProducerRejectsStaleProducedArtifactAuditHashesTest::RunTest(const FString& Parameters)
{
	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit produced artifact builds before audit mutation"),
		BuildProducedArtifactFromSeedSnapshotForTest(
			TEXT("Artifact.Region.Test.0"),
			TEXT("Region.Test.0"),
			42,
			7,
			0,
			ELayoutFrozenSubmissionRegionKind::Root,
			BuildTestFrozenSubmissionSnapshot(GetTransientPackage()),
			Artifact,
			FailureReason));

	FLayoutProducedFrozenDescriptorArtifact MissingDescriptorAuditArtifact = Artifact;
	MissingDescriptorAuditArtifact.DescriptorAuditHash = 0;
	FLayoutFrozenSubmissionDescriptorSeed SubmitSeed;
	TestFalse(
		TEXT("Solve submission rejects missing descriptor audit hash"),
		LayoutFrozenSubmissionDescriptorProducer::TryGetSeedForSolveSubmissionForTests(
			&MissingDescriptorAuditArtifact,
			SubmitSeed,
			FailureReason));
	TestTrue(TEXT("Missing descriptor audit hash failure is reported"), FailureReason.Contains(TEXT("descriptor audit hash")));

	FLayoutProducedFrozenDescriptorArtifact StaleDescriptorAuditArtifact = Artifact;
	StaleDescriptorAuditArtifact.DescriptorAuditHash += 1;
	TestFalse(
		TEXT("Solve submission rejects stale descriptor audit hash"),
		LayoutFrozenSubmissionDescriptorProducer::TryGetSeedForSolveSubmissionForTests(
			&StaleDescriptorAuditArtifact,
			SubmitSeed,
			FailureReason));
	TestTrue(TEXT("Stale descriptor audit hash failure is reported"), FailureReason.Contains(TEXT("descriptor audit hash")));

	FLayoutProducedFrozenDescriptorArtifact MissingManifestAuditArtifact = Artifact;
	MissingManifestAuditArtifact.ManifestPrewarmAuditHash = 0;
	FLayoutManifestPrewarmInput ManifestPrewarmInput;
	TestFalse(
		TEXT("Solve submission rejects missing manifest prewarm audit hash"),
		LayoutFrozenSubmissionDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmissionForTests(
			&MissingManifestAuditArtifact,
			ManifestPrewarmInput,
			FailureReason));
	TestTrue(TEXT("Missing manifest prewarm audit hash failure is reported"), FailureReason.Contains(TEXT("manifest prewarm audit hash")));

	FLayoutProducedFrozenDescriptorArtifact StaleManifestAuditArtifact = Artifact;
	StaleManifestAuditArtifact.ManifestPrewarmAuditHash += 1;
	TestFalse(
		TEXT("Solve submission rejects stale manifest prewarm audit hash"),
		LayoutFrozenSubmissionDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmissionForTests(
			&StaleManifestAuditArtifact,
			ManifestPrewarmInput,
			FailureReason));
	TestTrue(TEXT("Stale manifest prewarm audit hash failure is reported"), FailureReason.Contains(TEXT("manifest prewarm audit hash")));
	return true;
}

bool FLayoutFrozenSubmissionDescriptorProducerRejectsStaleManifestPrewarmPayloadTest::RunTest(const FString& Parameters)
{
	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit produced artifact builds before manifest mutation"),
		BuildProducedArtifactFromSeedSnapshotForTest(
			TEXT("Artifact.Region.Test.0"),
			TEXT("Region.Test.0"),
			42,
			7,
			0,
			ELayoutFrozenSubmissionRegionKind::Root,
			BuildTestFrozenSubmissionSnapshot(GetTransientPackage()),
			Artifact,
			FailureReason));

	FLayoutProducedFrozenDescriptorArtifact StalePrewarmIdArtifact = Artifact;
	StalePrewarmIdArtifact.ManifestPrewarmInput.PrewarmId = TEXT("Stale.PrewarmId");
	FLayoutManifestPrewarmInput ManifestPrewarmInput;
	TestFalse(
		TEXT("Solve submission rejects produced artifacts whose manifest prewarm id went stale after production"),
		LayoutFrozenSubmissionDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmissionForTests(
			&StalePrewarmIdArtifact,
			ManifestPrewarmInput,
			FailureReason));
	TestTrue(TEXT("Stale manifest prewarm id failure is reported"), FailureReason.Contains(TEXT("prewarm id")));

	FLayoutProducedFrozenDescriptorArtifact StalePrewarmKindArtifact = Artifact;
	StalePrewarmKindArtifact.ManifestPrewarmInput.Kind = ELayoutManifestPrewarmKind::Continuation;
	TestFalse(
		TEXT("Solve submission rejects produced artifacts whose manifest prewarm kind went stale after production"),
		LayoutFrozenSubmissionDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmissionForTests(
			&StalePrewarmKindArtifact,
			ManifestPrewarmInput,
			FailureReason));
	TestTrue(TEXT("Stale manifest prewarm kind failure is reported"), FailureReason.Contains(TEXT("prewarm kind")));

	FLayoutProducedFrozenDescriptorArtifact StaleManifestSeedArtifact = Artifact;
	StaleManifestSeedArtifact.ManifestPrewarmInput.FrozenRequestManifest.CapturedSeed += 1;
	TestFalse(
		TEXT("Solve submission rejects produced artifacts whose manifest solve seed went stale after production"),
		LayoutFrozenSubmissionDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmissionForTests(
			&StaleManifestSeedArtifact,
			ManifestPrewarmInput,
			FailureReason));
	TestTrue(TEXT("Stale manifest solve seed failure is reported"), FailureReason.Contains(TEXT("solve seed")));

	FLayoutProducedFrozenDescriptorArtifact StaleManifestModeArtifact = Artifact;
	StaleManifestModeArtifact.ManifestPrewarmInput.FrozenRequestManifest.SelectedModePlan.ModePlanId = TEXT("ModePlan.Stale");
	TestFalse(
		TEXT("Solve submission rejects produced artifacts whose manifest selected mode plan went stale after production"),
		LayoutFrozenSubmissionDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmissionForTests(
			&StaleManifestModeArtifact,
			ManifestPrewarmInput,
			FailureReason));
	TestTrue(TEXT("Stale manifest mode plan failure is reported"), FailureReason.Contains(TEXT("selected mode plan")));

	Artifact.ManifestPrewarmInput.FrozenRequestManifest.bHasRootPlacementSubmission = false;
	TestFalse(
		TEXT("Solve submission rejects produced artifacts whose manifest prewarm payload went stale after production"),
		LayoutFrozenSubmissionDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmissionForTests(
			&Artifact,
			ManifestPrewarmInput,
			FailureReason));
	TestTrue(TEXT("Stale manifest prewarm payload failure reports root placement submission"), FailureReason.Contains(TEXT("root placement submission")));
	return true;
}

bool FLayoutFrozenSubmissionDescriptorProducerBuildsSelectedRootScoutArtifactTest::RunTest(const FString& Parameters)
{
	const FIntVector FinalizedSiteCenter(48, 80, 16);
	const int32 WorldSeed = 37;
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildSelectedRootSnapshotForTest(FinalizedSiteCenter, WorldSeed);

	FLayoutSelectedRootScoutResultPrewarmDescriptorInput Input;
	Input.ArtifactId = TEXT("Artifact.RootScout.Test.0");
	Input.DescriptorId = TEXT("Descriptor.RootScout.Test.0");
	Input.ScoutResultId = TEXT("Scout.Root.Test.0");
	Input.RegionGroupId = 42;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.FinalizedSiteCenterBlockWorldPos = FinalizedSiteCenter;
	Input.WorldSeed = WorldSeed;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Selected root scout result builds explicit descriptor artifact"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedRootScoutResult(
			Input,
			Artifact,
			FailureReason));
	if (!Artifact.bHasProducedArtifact) AddInfo(FailureReason);
	TestTrue(TEXT("Selected root scout artifact produced"), Artifact.bHasProducedArtifact);
	TestEqual(TEXT("Selected root scout descriptor id preserved"), Artifact.DescriptorSeed.DescriptorId, Input.DescriptorId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsSelectedRootScoutWithoutModePlanTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsSelectedRootScoutWithoutModePlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsSelectedRootScoutWithoutPlacementSubmissionTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsSelectedRootScoutWithoutPlacementSubmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsStaleSelectedRootScoutSeedTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsStaleSelectedRootScoutSeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerRejectsSelectedRootScoutWithoutModePlanTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestFrozenSubmissionSnapshot(GetTransientPackage());
	const FIntVector FinalizedSiteCenter(48, 80, 16);
	const int32 WorldSeed = 37;
	const int32 ExpectedSolveSeed = FLayoutContractPlacementCandidates::BuildFinalizedScoutResultSolveSeed(
		FinalizedSiteCenter,
		WorldSeed);
	Snapshot.WorkerSolvePacket.SolveSeed = ExpectedSolveSeed;
	Snapshot.WorkerSolvePacket.RequestManifest.CapturedSeed = ExpectedSolveSeed;
	Snapshot.WorkerSolvePacket.bHasSelectedModePlan = false;
	Snapshot.WorkerSolvePacket.RequestManifest.bHasSelectedModePlan = false;

	FLayoutSelectedRootScoutResultPrewarmDescriptorInput Input;
	Input.ArtifactId = TEXT("Artifact.RootScout.Test.0");
	Input.DescriptorId = TEXT("Descriptor.RootScout.Test.0");
	Input.ScoutResultId = TEXT("Scout.Root.Test.0");
	Input.RegionGroupId = 42;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.FinalizedSiteCenterBlockWorldPos = FinalizedSiteCenter;
	Input.WorldSeed = WorldSeed;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Selected root scout descriptor rejects missing selected mode plan"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedRootScoutResult(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing root selected mode failure reported"), FailureReason.Contains(TEXT("selected mode plan")));
	return true;
}

bool FLayoutFrozenSubmissionDescriptorProducerRejectsSelectedRootScoutWithoutPlacementSubmissionTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestFrozenSubmissionSnapshot(GetTransientPackage());
	const FIntVector FinalizedSiteCenter(48, 80, 16);
	const int32 WorldSeed = 37;
	const int32 ExpectedSolveSeed = FLayoutContractPlacementCandidates::BuildFinalizedScoutResultSolveSeed(
		FinalizedSiteCenter,
		WorldSeed);
	Snapshot.WorkerSolvePacket.SolveSeed = ExpectedSolveSeed;
	Snapshot.WorkerSolvePacket.RequestManifest.CapturedSeed = ExpectedSolveSeed;
	Snapshot.WorkerSolvePacket.RequestManifest.bHasRootPlacementSubmission = false;

	FLayoutSelectedRootScoutResultPrewarmDescriptorInput Input;
	Input.ArtifactId = TEXT("Artifact.RootScout.Test.0");
	Input.DescriptorId = TEXT("Descriptor.RootScout.Test.0");
	Input.ScoutResultId = TEXT("Scout.Root.Test.0");
	Input.RegionGroupId = 42;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.FinalizedSiteCenterBlockWorldPos = FinalizedSiteCenter;
	Input.WorldSeed = WorldSeed;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Selected root scout descriptor rejects missing root placement submission"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedRootScoutResult(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing root placement submission failure reported"), FailureReason.Contains(TEXT("root placement submission")));
	return true;
}

bool FLayoutFrozenSubmissionDescriptorProducerRejectsStaleSelectedRootScoutSeedTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestFrozenSubmissionSnapshot(GetTransientPackage());
	const FIntVector FinalizedSiteCenter(48, 80, 16);
	const int32 WorldSeed = 37;
	Snapshot.WorkerSolvePacket.SolveSeed = 100;
	Snapshot.WorkerSolvePacket.RequestManifest.CapturedSeed = 100;
	Snapshot.WorkerSolvePacket.RequestManifest.bHasRootPlacementSubmission = true;
	Snapshot.WorkerSolvePacket.RequestManifest.RootSiteCenterBlockWorldPos = FinalizedSiteCenter;

	FLayoutSelectedRootScoutResultPrewarmDescriptorInput Input;
	Input.ArtifactId = TEXT("Artifact.RootScout.Test.0");
	Input.DescriptorId = TEXT("Descriptor.RootScout.Test.0");
	Input.ScoutResultId = TEXT("Scout.Root.Test.0");
	Input.RegionGroupId = 42;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.FinalizedSiteCenterBlockWorldPos = FinalizedSiteCenter;
	Input.WorldSeed = WorldSeed;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Selected root scout descriptor rejects stale solve seed"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedRootScoutResult(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Stale seed failure reported"), FailureReason.Contains(TEXT("solve seed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerBuildsSelectedRootInputFromPlanningRecordTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerBuildsSelectedRootInputFromPlanningRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerBuildsSelectedRootInputFromPlanningRecordTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestFrozenSubmissionSnapshot(GetTransientPackage());
	FLayoutSelectedRootScoutResultPrewarmDescriptorInput Input;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit planning discovery metadata builds selected-root input"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildSelectedRootScoutResultPrewarmInputFromPlanningRecord(
			TEXT("Artifact.RootScout.Test.0"),
			TEXT("Descriptor.RootScout.Test.0"),
			42,
			7,
			0,
			BuildTestDiscoveredPlanningRecord(),
			Snapshot,
			Input,
			FailureReason));
	TestEqual(TEXT("Scout result id copied from discovery candidate"), Input.ScoutResultId, FLayoutId(TEXT("RootScout.Discovery.0")));
	TestEqual(TEXT("Finalized site center copied from reservation selection"), Input.FinalizedSiteCenterBlockWorldPos, FIntVector(48, 80, 16));
	TestEqual(TEXT("World seed copied from reservation selection"), Input.WorldSeed, 1337);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsMissingSelectedRootDiscoveryIdTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsMissingSelectedRootDiscoveryId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerRejectsMissingSelectedRootDiscoveryIdTest::RunTest(const FString& Parameters)
{
	FPlannedLayoutSiteRecord PlanningRecord = BuildTestDiscoveredPlanningRecord();
	PlanningRecord.DiscoveryCandidateId = NAME_None;
	FLayoutSelectedRootScoutResultPrewarmDescriptorInput Input;
	FString FailureReason;
	TestFalse(
		TEXT("Missing discovery candidate id fails closed"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildSelectedRootScoutResultPrewarmInputFromPlanningRecord(
			TEXT("Artifact.RootScout.Test.0"),
			TEXT("Descriptor.RootScout.Test.0"),
			42,
			7,
			0,
			PlanningRecord,
			BuildTestFrozenSubmissionSnapshot(GetTransientPackage()),
			Input,
			FailureReason));
	TestTrue(TEXT("Missing discovery id failure reported"), FailureReason.Contains(TEXT("discovery candidate")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsStaleSelectedRootScoutOrderMetadataTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsStaleSelectedRootScoutOrderMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerRejectsStaleSelectedRootScoutOrderMetadataTest::RunTest(const FString& Parameters)
{
	FLayoutSelectedRootScoutResultPrewarmDescriptorInput Input;
	FString FailureReason;

	FPlannedLayoutSiteRecord MissingOrderRecord = BuildTestDiscoveredPlanningRecord();
	MissingOrderRecord.DiscoveryOrderingId = NAME_None;
	TestFalse(
		TEXT("Missing discovery ordering id fails closed"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildSelectedRootScoutResultPrewarmInputFromPlanningRecord(
			TEXT("Artifact.RootScout.Test.0"),
			TEXT("Descriptor.RootScout.Test.0"),
			42,
			7,
			0,
			MissingOrderRecord,
			BuildTestFrozenSubmissionSnapshot(GetTransientPackage()),
			Input,
			FailureReason));
	TestTrue(TEXT("Missing ordering id failure reported"), FailureReason.Contains(TEXT("ordering id")));

	FPlannedLayoutSiteRecord InvalidOrderIndexRecord = BuildTestDiscoveredPlanningRecord();
	InvalidOrderIndexRecord.DiscoveryOrderIndex = -2;
	TestFalse(
		TEXT("Negative discovery order index fails closed"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildSelectedRootScoutResultPrewarmInputFromPlanningRecord(
			TEXT("Artifact.RootScout.Test.3"),
			TEXT("Descriptor.RootScout.Test.3"),
			42,
			7,
			0,
			InvalidOrderIndexRecord,
			BuildTestFrozenSubmissionSnapshot(GetTransientPackage()),
			Input,
			FailureReason));
	TestTrue(TEXT("Negative order index failure reported"), FailureReason.Contains(TEXT("order index")));

	FPlannedLayoutSiteRecord StaleShiftRecord = BuildTestDiscoveredPlanningRecord();
	StaleShiftRecord.DiscoveryPlacementShiftCells = FIntVector(1, 0, 0);
	TestFalse(
		TEXT("Mismatched placement shift id/cells fail closed"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildSelectedRootScoutResultPrewarmInputFromPlanningRecord(
			TEXT("Artifact.RootScout.Test.1"),
			TEXT("Descriptor.RootScout.Test.1"),
			42,
			7,
			0,
			StaleShiftRecord,
			BuildTestFrozenSubmissionSnapshot(GetTransientPackage()),
			Input,
			FailureReason));
	TestTrue(TEXT("Stale shift failure reported"), FailureReason.Contains(TEXT("placement shift")));

	FPlannedLayoutSiteRecord StaleSeedRecord = BuildTestDiscoveredPlanningRecord();
	++StaleSeedRecord.DiscoverySolveSeed;
	TestFalse(
		TEXT("Mismatched discovery solve seed fails closed"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildSelectedRootScoutResultPrewarmInputFromPlanningRecord(
			TEXT("Artifact.RootScout.Test.2"),
			TEXT("Descriptor.RootScout.Test.2"),
			42,
			7,
			0,
			StaleSeedRecord,
			BuildTestFrozenSubmissionSnapshot(GetTransientPackage()),
			Input,
			FailureReason));
	TestTrue(TEXT("Stale solve seed failure reported"), FailureReason.Contains(TEXT("solve seed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerBuildsSelectedContinuationScoutArtifactTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerBuildsSelectedContinuationScoutArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerBuildsSelectedContinuationScoutArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestContinuationFrozenSubmissionSnapshot(GetTransientPackage());
	const int32 ExpectedSolveSeed = 1187;
	Snapshot.WorkerSolvePacket.SolveSeed = ExpectedSolveSeed;
	Snapshot.WorkerSolvePacket.RequestManifest.CapturedSeed = ExpectedSolveSeed;

	FLayoutSelectedContinuationScoutResultPrewarmDescriptorInput Input;
	Input.ArtifactId = TEXT("Artifact.ContinuationScout.Test.0");
	Input.DescriptorId = TEXT("Descriptor.ContinuationScout.Test.0");
	Input.EdgeScoutResultId = TEXT("EdgeScout.Continuation.Test.0");
	Input.RegionGroupId = 9001;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.ExpectedSolveSeed = ExpectedSolveSeed;
	Input.FinalizedPrimaryBlockWorldPos = Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Selected continuation scout result builds explicit descriptor artifact"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedContinuationScoutResult(
			Input,
			Artifact,
			FailureReason));
	if (!Artifact.bHasProducedArtifact) AddInfo(FailureReason);
	TestTrue(TEXT("Selected continuation scout artifact produced"), Artifact.bHasProducedArtifact);
	TestEqual(TEXT("Continuation descriptor id preserved"), Artifact.DescriptorSeed.DescriptorId, Input.DescriptorId);
	TestEqual(TEXT("Continuation descriptor kind preserved"), static_cast<uint8>(Artifact.DescriptorSeed.RegionKind), static_cast<uint8>(ELayoutFrozenSubmissionRegionKind::Continuation));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsSelectedContinuationScoutWithoutModePlanTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsSelectedContinuationScoutWithoutModePlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsSelectedContinuationScoutWithoutReachabilityTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsSelectedContinuationScoutWithoutReachability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsSelectedContinuationScoutWithoutResolvedEntryTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsSelectedContinuationScoutWithoutResolvedEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsStaleSelectedContinuationScoutSeedTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsStaleSelectedContinuationScoutSeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerRejectsSelectedContinuationScoutWithoutModePlanTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestContinuationFrozenSubmissionSnapshot(GetTransientPackage());
	const int32 ExpectedSolveSeed = 1187;
	Snapshot.WorkerSolvePacket.SolveSeed = ExpectedSolveSeed;
	Snapshot.WorkerSolvePacket.RequestManifest.CapturedSeed = ExpectedSolveSeed;
	Snapshot.WorkerSolvePacket.bHasSelectedModePlan = false;
	Snapshot.WorkerSolvePacket.RequestManifest.bHasSelectedModePlan = false;

	FLayoutSelectedContinuationScoutResultPrewarmDescriptorInput Input;
	Input.ArtifactId = TEXT("Artifact.ContinuationScout.Test.0");
	Input.DescriptorId = TEXT("Descriptor.ContinuationScout.Test.0");
	Input.EdgeScoutResultId = TEXT("EdgeScout.Continuation.Test.0");
	Input.RegionGroupId = 9001;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.ExpectedSolveSeed = ExpectedSolveSeed;
	Input.FinalizedPrimaryBlockWorldPos = Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Selected continuation scout descriptor rejects missing selected mode plan"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedContinuationScoutResult(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing continuation selected mode failure reported"), FailureReason.Contains(TEXT("selected mode plan")));
	return true;
}

bool FLayoutFrozenSubmissionDescriptorProducerRejectsSelectedContinuationScoutWithoutReachabilityTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestContinuationFrozenSubmissionSnapshot(GetTransientPackage());
	const int32 ExpectedSolveSeed = 1187;
	Snapshot.WorkerSolvePacket.SolveSeed = ExpectedSolveSeed;
	Snapshot.WorkerSolvePacket.RequestManifest.CapturedSeed = ExpectedSolveSeed;
	Snapshot.WorkerSolvePacket.RequestManifest.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;

	FLayoutSelectedContinuationScoutResultPrewarmDescriptorInput Input;
	Input.ArtifactId = TEXT("Artifact.ContinuationScout.Test.0");
	Input.DescriptorId = TEXT("Descriptor.ContinuationScout.Test.0");
	Input.EdgeScoutResultId = TEXT("EdgeScout.Continuation.Test.0");
	Input.RegionGroupId = 9001;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.ExpectedSolveSeed = ExpectedSolveSeed;
	Input.FinalizedPrimaryBlockWorldPos = Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Selected continuation scout descriptor rejects profiles without traversal reachability"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedContinuationScoutResult(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing continuation reachability failure reported"), FailureReason.Contains(TEXT("traversal-reachable")));
	return true;
}

bool FLayoutFrozenSubmissionDescriptorProducerRejectsSelectedContinuationScoutWithoutResolvedEntryTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestContinuationFrozenSubmissionSnapshot(GetTransientPackage());
	const int32 ExpectedSolveSeed = 1187;
	Snapshot.WorkerSolvePacket.SolveSeed = ExpectedSolveSeed;
	Snapshot.WorkerSolvePacket.RequestManifest.CapturedSeed = ExpectedSolveSeed;
	Snapshot.WorkerSolvePacket.RequestManifest.RootContinuationSelection.ResolvedEntryLevel = INDEX_NONE;

	FLayoutSelectedContinuationScoutResultPrewarmDescriptorInput Input;
	Input.ArtifactId = TEXT("Artifact.ContinuationScout.Test.0");
	Input.DescriptorId = TEXT("Descriptor.ContinuationScout.Test.0");
	Input.EdgeScoutResultId = TEXT("EdgeScout.Continuation.Test.0");
	Input.RegionGroupId = 9001;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.ExpectedSolveSeed = ExpectedSolveSeed;
	Input.FinalizedPrimaryBlockWorldPos = Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Selected continuation scout descriptor rejects missing resolved entry metadata"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedContinuationScoutResult(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing continuation resolved entry failure reported"), FailureReason.Contains(TEXT("resolved continuation entry")));
	return true;
}

bool FLayoutFrozenSubmissionDescriptorProducerRejectsStaleSelectedContinuationScoutSeedTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestContinuationFrozenSubmissionSnapshot(GetTransientPackage());
	Snapshot.WorkerSolvePacket.SolveSeed = 100;
	Snapshot.WorkerSolvePacket.RequestManifest.CapturedSeed = 100;

	FLayoutSelectedContinuationScoutResultPrewarmDescriptorInput Input;
	Input.ArtifactId = TEXT("Artifact.ContinuationScout.Test.0");
	Input.DescriptorId = TEXT("Descriptor.ContinuationScout.Test.0");
	Input.EdgeScoutResultId = TEXT("EdgeScout.Continuation.Test.0");
	Input.RegionGroupId = 9001;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.ExpectedSolveSeed = 1187;
	Input.FinalizedPrimaryBlockWorldPos = Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Selected continuation scout descriptor rejects stale solve seed"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedContinuationScoutResult(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Stale continuation seed failure reported"), FailureReason.Contains(TEXT("solve seed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsMismatchedSelectedContinuationEndpointTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsMismatchedSelectedContinuationEndpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerRejectsMismatchedSelectedContinuationEndpointTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestContinuationFrozenSubmissionSnapshot(GetTransientPackage());
	const int32 ExpectedSolveSeed = 1187;
	Snapshot.WorkerSolvePacket.SolveSeed = ExpectedSolveSeed;
	Snapshot.WorkerSolvePacket.RequestManifest.CapturedSeed = ExpectedSolveSeed;

	FLayoutSelectedContinuationScoutResultPrewarmDescriptorInput Input;
	Input.ArtifactId = TEXT("Artifact.ContinuationScout.Test.0");
	Input.DescriptorId = TEXT("Descriptor.ContinuationScout.Test.0");
	Input.EdgeScoutResultId = TEXT("EdgeScout.Continuation.Test.0");
	Input.RegionGroupId = 9001;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.ExpectedSolveSeed = ExpectedSolveSeed;
	Input.FinalizedPrimaryBlockWorldPos = FIntVector(999, 64, 16);
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Selected continuation scout descriptor rejects mismatched primary endpoint"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedContinuationScoutResult(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Mismatched endpoint failure reported"), FailureReason.Contains(TEXT("primary endpoint")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerBuildsSelectedChildScoutArtifactTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerBuildsSelectedChildScoutArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerBuildsSelectedChildScoutArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestChildFrozenSubmissionSnapshot(GetTransientPackage());

	FLayoutSelectedChildScoutResultPrewarmDescriptorInput Input;
	Input.ArtifactId = TEXT("Artifact.ChildScout.Test.0");
	Input.DescriptorId = TEXT("Descriptor.ChildScout.Test.0");
	Input.ChildScoutResultId = TEXT("ChildScout.Test.0");
	Input.RegionGroupId = 42;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.ExpectedSolveSeed = Snapshot.WorkerSolvePacket.SolveSeed;
	Input.ExpectedStableChildKey = Snapshot.WorkerSolvePacket.FrozenChildHandoff.StableChildKey;
	Input.ExpectedProofCertificateId = Snapshot.WorkerSolvePacket.FrozenChildHandoff.ProofCertificate.CertificateId;
	Input.FinalizedPrimaryBlockWorldPos = Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Selected child scout result builds explicit descriptor artifact"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedChildScoutResult(
			Input,
			Artifact,
			FailureReason));
	if (!Artifact.bHasProducedArtifact) AddInfo(FailureReason);
	TestTrue(TEXT("Selected child scout artifact produced"), Artifact.bHasProducedArtifact);
	TestEqual(TEXT("Child descriptor id preserved"), Artifact.DescriptorSeed.DescriptorId, Input.DescriptorId);
	TestEqual(TEXT("Child descriptor kind preserved"), static_cast<uint8>(Artifact.DescriptorSeed.RegionKind), static_cast<uint8>(ELayoutFrozenSubmissionRegionKind::Child));
	FLayoutManifestPrewarmInput ManifestPrewarmInput;
	TestFalse(
		TEXT("Child descriptor artifact does not expose root/continuation manifest prewarm input"),
		LayoutFrozenSubmissionDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmissionForTests(
			&Artifact,
			ManifestPrewarmInput,
			FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsSelectedChildScoutWithoutModePlanTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsSelectedChildScoutWithoutModePlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsSelectedChildScoutMismatchedProfileMetadataTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsSelectedChildScoutMismatchedProfileMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsSelectedChildScoutMismatchedSourceEntryTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsSelectedChildScoutMismatchedSourceEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsStaleSelectedChildScoutSeedTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsStaleSelectedChildScoutSeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerRejectsSelectedChildScoutWithoutModePlanTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestChildFrozenSubmissionSnapshot(GetTransientPackage());
	Snapshot.WorkerSolvePacket.bHasSelectedModePlan = false;
	Snapshot.WorkerSolvePacket.RequestManifest.bHasSelectedModePlan = false;

	FLayoutSelectedChildScoutResultPrewarmDescriptorInput Input;
	Input.ArtifactId = TEXT("Artifact.ChildScout.Test.0");
	Input.DescriptorId = TEXT("Descriptor.ChildScout.Test.0");
	Input.ChildScoutResultId = TEXT("ChildScout.Test.0");
	Input.RegionGroupId = 42;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.ExpectedSolveSeed = Snapshot.WorkerSolvePacket.SolveSeed;
	Input.ExpectedStableChildKey = Snapshot.WorkerSolvePacket.FrozenChildHandoff.StableChildKey;
	Input.ExpectedProofCertificateId = Snapshot.WorkerSolvePacket.FrozenChildHandoff.ProofCertificate.CertificateId;
	Input.FinalizedPrimaryBlockWorldPos = Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Selected child scout descriptor rejects missing selected mode plan"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedChildScoutResult(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing child selected mode failure reported"), FailureReason.Contains(TEXT("selected mode plan")));
	return true;
}

bool FLayoutFrozenSubmissionDescriptorProducerRejectsSelectedChildScoutMismatchedProfileMetadataTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestChildFrozenSubmissionSnapshot(GetTransientPackage());
	Snapshot.WorkerSolvePacket.RequestManifest.ProfilePath = FSoftObjectPath(TEXT("/Game/Layout/Test/OtherProfile.OtherProfile"));

	FLayoutSelectedChildScoutResultPrewarmDescriptorInput Input;
	Input.ArtifactId = TEXT("Artifact.ChildScout.Test.0");
	Input.DescriptorId = TEXT("Descriptor.ChildScout.Test.0");
	Input.ChildScoutResultId = TEXT("ChildScout.Test.0");
	Input.RegionGroupId = 42;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.ExpectedSolveSeed = Snapshot.WorkerSolvePacket.SolveSeed;
	Input.ExpectedStableChildKey = Snapshot.WorkerSolvePacket.FrozenChildHandoff.StableChildKey;
	Input.ExpectedProofCertificateId = Snapshot.WorkerSolvePacket.FrozenChildHandoff.ProofCertificate.CertificateId;
	Input.FinalizedPrimaryBlockWorldPos = Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Selected child scout descriptor rejects stale child profile metadata"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedChildScoutResult(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Mismatched child profile failure reported"), FailureReason.Contains(TEXT("child profile")));
	return true;
}

bool FLayoutFrozenSubmissionDescriptorProducerRejectsSelectedChildScoutMismatchedSourceEntryTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestChildFrozenSubmissionSnapshot(GetTransientPackage());
	Snapshot.WorkerSolvePacket.RequestManifest.SourceContentEntryId = TEXT("ContentEntry.Other");

	FLayoutSelectedChildScoutResultPrewarmDescriptorInput Input;
	Input.ArtifactId = TEXT("Artifact.ChildScout.Test.0");
	Input.DescriptorId = TEXT("Descriptor.ChildScout.Test.0");
	Input.ChildScoutResultId = TEXT("ChildScout.Test.0");
	Input.RegionGroupId = 42;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.ExpectedSolveSeed = Snapshot.WorkerSolvePacket.SolveSeed;
	Input.ExpectedStableChildKey = Snapshot.WorkerSolvePacket.FrozenChildHandoff.StableChildKey;
	Input.ExpectedProofCertificateId = Snapshot.WorkerSolvePacket.FrozenChildHandoff.ProofCertificate.CertificateId;
	Input.FinalizedPrimaryBlockWorldPos = Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Selected child scout descriptor rejects stale source content-entry metadata"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedChildScoutResult(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Mismatched source entry failure reported"), FailureReason.Contains(TEXT("source content-entry")));
	return true;
}

bool FLayoutFrozenSubmissionDescriptorProducerRejectsStaleSelectedChildScoutSeedTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestChildFrozenSubmissionSnapshot(GetTransientPackage());

	FLayoutSelectedChildScoutResultPrewarmDescriptorInput Input;
	Input.ArtifactId = TEXT("Artifact.ChildScout.Test.0");
	Input.DescriptorId = TEXT("Descriptor.ChildScout.Test.0");
	Input.ChildScoutResultId = TEXT("ChildScout.Test.0");
	Input.RegionGroupId = 42;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.ExpectedSolveSeed = Snapshot.WorkerSolvePacket.SolveSeed + 1;
	Input.ExpectedStableChildKey = Snapshot.WorkerSolvePacket.FrozenChildHandoff.StableChildKey;
	Input.ExpectedProofCertificateId = Snapshot.WorkerSolvePacket.FrozenChildHandoff.ProofCertificate.CertificateId;
	Input.FinalizedPrimaryBlockWorldPos = Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Selected child scout descriptor rejects stale solve seed"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedChildScoutResult(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Stale child seed failure reported"), FailureReason.Contains(TEXT("solve seed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsMismatchedSelectedChildCertificateTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsMismatchedSelectedChildCertificate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerRejectsMismatchedSelectedChildCertificateTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestChildFrozenSubmissionSnapshot(GetTransientPackage());

	FLayoutSelectedChildScoutResultPrewarmDescriptorInput Input;
	Input.ArtifactId = TEXT("Artifact.ChildScout.Test.0");
	Input.DescriptorId = TEXT("Descriptor.ChildScout.Test.0");
	Input.ChildScoutResultId = TEXT("ChildScout.Test.0");
	Input.RegionGroupId = 42;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.ExpectedSolveSeed = Snapshot.WorkerSolvePacket.SolveSeed;
	Input.ExpectedStableChildKey = Snapshot.WorkerSolvePacket.FrozenChildHandoff.StableChildKey;
	Input.ExpectedProofCertificateId = TEXT("ChildCertificate.Other");
	Input.FinalizedPrimaryBlockWorldPos = Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Selected child scout descriptor rejects mismatched certificate"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedChildScoutResult(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Mismatched certificate failure reported"), FailureReason.Contains(TEXT("certificate")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmDescriptorProducerBuildsRootArtifactTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.RegionPrewarmDescriptorBuildsRootArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmDescriptorProducerBuildsRootArtifactTest::RunTest(const FString& Parameters)
{
	const FIntVector FinalizedSiteCenter(48, 80, 16);
	const int32 WorldSeed = 37;
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildSelectedRootSnapshotForTest(FinalizedSiteCenter, WorldSeed);

	FLayoutRegionPrewarmDescriptorInput Input;
	Input.SourceKind = ELayoutRegionPrewarmDescriptorSourceKind::RootScoutResult;
	Input.ArtifactId = TEXT("Artifact.RegionPrewarm.Root.0");
	Input.DescriptorId = TEXT("Descriptor.RegionPrewarm.Root.0");
	Input.SelectedResultId = TEXT("Scout.Root.RegionPrewarm.0");
	Input.RegionGroupId = 42;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.WorldSeed = WorldSeed;
	Input.FinalizedPrimaryBlockWorldPos = FinalizedSiteCenter;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Generic region prewarm producer builds root descriptor artifact"),
		LayoutRegionPrewarmDescriptorProducer::TryBuildProducedDescriptorArtifact(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Root region prewarm artifact produced"), Artifact.bHasProducedArtifact);
	TestEqual(TEXT("Root region artifact id matches input"), Artifact.ArtifactId, Input.ArtifactId);
	TestEqual(TEXT("Root region descriptor id matches input"), Artifact.DescriptorSeed.DescriptorId, Input.DescriptorId);
	TestEqual(TEXT("Root region group id matches input"), Artifact.DescriptorSeed.RegionGroupId, Input.RegionGroupId);
	TestEqual(TEXT("Root region descriptor kind preserved"), static_cast<uint8>(Artifact.DescriptorSeed.RegionKind), static_cast<uint8>(ELayoutFrozenSubmissionRegionKind::Root));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmDescriptorProducerBuildsContinuationArtifactTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.RegionPrewarmDescriptorBuildsContinuationArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmDescriptorProducerBuildsContinuationArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestContinuationFrozenSubmissionSnapshot(GetTransientPackage());
	const int32 ExpectedSolveSeed = 1187;
	Snapshot.WorkerSolvePacket.SolveSeed = ExpectedSolveSeed;
	Snapshot.WorkerSolvePacket.RequestManifest.CapturedSeed = ExpectedSolveSeed;

	FLayoutRegionPrewarmDescriptorInput Input;
	Input.SourceKind = ELayoutRegionPrewarmDescriptorSourceKind::ContinuationEdgeScoutResult;
	Input.ArtifactId = TEXT("Artifact.RegionPrewarm.Continuation.0");
	Input.DescriptorId = TEXT("Descriptor.RegionPrewarm.Continuation.0");
	Input.SelectedResultId = TEXT("Scout.Continuation.RegionPrewarm.0");
	Input.RegionGroupId = 9001;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.ExpectedSolveSeed = ExpectedSolveSeed;
	Input.FinalizedPrimaryBlockWorldPos = Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Generic region prewarm producer builds continuation descriptor artifact"),
		LayoutRegionPrewarmDescriptorProducer::TryBuildProducedDescriptorArtifact(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Continuation region prewarm artifact produced"), Artifact.bHasProducedArtifact);
	TestEqual(TEXT("Continuation region artifact id matches input"), Artifact.ArtifactId, Input.ArtifactId);
	TestEqual(TEXT("Continuation region descriptor id matches input"), Artifact.DescriptorSeed.DescriptorId, Input.DescriptorId);
	TestEqual(TEXT("Continuation region group id matches input"), Artifact.DescriptorSeed.RegionGroupId, Input.RegionGroupId);
	TestEqual(TEXT("Continuation region descriptor kind preserved"), static_cast<uint8>(Artifact.DescriptorSeed.RegionKind), static_cast<uint8>(ELayoutFrozenSubmissionRegionKind::Continuation));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmDescriptorProducerBuildsChildArtifactTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.RegionPrewarmDescriptorBuildsChildArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmDescriptorProducerBuildsChildArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestChildFrozenSubmissionSnapshot(GetTransientPackage());

	FLayoutRegionPrewarmDescriptorInput Input;
	Input.SourceKind = ELayoutRegionPrewarmDescriptorSourceKind::ChildScoutResult;
	Input.ArtifactId = TEXT("Artifact.RegionPrewarm.Child.0");
	Input.DescriptorId = TEXT("Descriptor.RegionPrewarm.Child.0");
	Input.SelectedResultId = TEXT("Scout.Child.RegionPrewarm.0");
	Input.RegionGroupId = 42;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.ExpectedSolveSeed = Snapshot.WorkerSolvePacket.SolveSeed;
	Input.ExpectedStableChildKey = Snapshot.WorkerSolvePacket.FrozenChildHandoff.StableChildKey;
	Input.ExpectedProofCertificateId = Snapshot.WorkerSolvePacket.FrozenChildHandoff.ProofCertificate.CertificateId;
	Input.FinalizedPrimaryBlockWorldPos = Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Generic region prewarm producer builds child descriptor artifact"),
		LayoutRegionPrewarmDescriptorProducer::TryBuildProducedDescriptorArtifact(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Child region prewarm artifact produced"), Artifact.bHasProducedArtifact);
	TestEqual(TEXT("Child region artifact id matches input"), Artifact.ArtifactId, Input.ArtifactId);
	TestEqual(TEXT("Child region descriptor id matches input"), Artifact.DescriptorSeed.DescriptorId, Input.DescriptorId);
	TestEqual(TEXT("Child region group id matches input"), Artifact.DescriptorSeed.RegionGroupId, Input.RegionGroupId);
	TestEqual(TEXT("Child region descriptor kind preserved"), static_cast<uint8>(Artifact.DescriptorSeed.RegionKind), static_cast<uint8>(ELayoutFrozenSubmissionRegionKind::Child));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmDescriptorProducerRejectsMissingIdentityTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.RegionPrewarmDescriptorRejectsMissingIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmDescriptorProducerRejectsMissingIdentityTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmDescriptorInput Input;
	Input.SourceKind = ELayoutRegionPrewarmDescriptorSourceKind::RootScoutResult;
	Input.ArtifactId = NAME_None;
	Input.DescriptorId = TEXT("Descriptor.RegionPrewarm.Root.0");
	Input.SelectedResultId = TEXT("Scout.Root.RegionPrewarm.0");
	Input.RegionGroupId = 42;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.WorldSeed = 37;
	Input.FinalizedPrimaryBlockWorldPos = FIntVector(48, 80, 16);
	Input.Snapshot = BuildTestFrozenSubmissionSnapshot(GetTransientPackage());

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Generic region prewarm producer rejects missing produced artifact id"),
		LayoutRegionPrewarmDescriptorProducer::TryBuildProducedDescriptorArtifact(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing artifact id failure reported"), FailureReason.Contains(TEXT("artifact id")));

	Input.ArtifactId = TEXT("Artifact.RegionPrewarm.Root.0");
	Input.DescriptorId = NAME_None;
	TestFalse(
		TEXT("Generic region prewarm producer rejects missing descriptor id"),
		LayoutRegionPrewarmDescriptorProducer::TryBuildProducedDescriptorArtifact(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing descriptor id failure reported"), FailureReason.Contains(TEXT("descriptor id")));

	Input.DescriptorId = TEXT("Descriptor.RegionPrewarm.Root.0");
	Input.RegionGroupId = 0;
	TestFalse(
		TEXT("Generic region prewarm producer rejects missing region group id"),
		LayoutRegionPrewarmDescriptorProducer::TryBuildProducedDescriptorArtifact(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing region group failure reported"), FailureReason.Contains(TEXT("region group")));

	Input.RegionGroupId = 42;
	Input.AttemptIndex = -1;
	TestFalse(
		TEXT("Generic region prewarm producer rejects negative attempt index"),
		LayoutRegionPrewarmDescriptorProducer::TryBuildProducedDescriptorArtifact(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Negative attempt failure reported"), FailureReason.Contains(TEXT("attempt index")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmDescriptorProducerRejectsMissingSelectedResultTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.RegionPrewarmDescriptorRejectsMissingSelectedResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmDescriptorProducerRejectsMissingSelectedResultTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmDescriptorInput Input;
	Input.SourceKind = ELayoutRegionPrewarmDescriptorSourceKind::RootScoutResult;
	Input.ArtifactId = TEXT("Artifact.RegionPrewarm.Root.0");
	Input.DescriptorId = TEXT("Descriptor.RegionPrewarm.Root.0");
	Input.SelectedResultId = NAME_None;
	Input.RegionGroupId = 42;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.Snapshot = BuildTestFrozenSubmissionSnapshot(GetTransientPackage());

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Generic region prewarm producer rejects missing selected result ids"),
		LayoutRegionPrewarmDescriptorProducer::TryBuildProducedDescriptorArtifact(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing selected result failure reported"), FailureReason.Contains(TEXT("selected scout or branch result")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmDescriptorProducerRejectsSourceKindSnapshotMismatchTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.RegionPrewarmDescriptorRejectsSourceKindSnapshotMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmDescriptorProducerRejectsSourceKindSnapshotMismatchTest::RunTest(const FString& Parameters)
{
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildTestContinuationFrozenSubmissionSnapshot(GetTransientPackage());
	Snapshot.WorkerSolvePacket.SolveSeed = 1187;
	Snapshot.WorkerSolvePacket.RequestManifest.CapturedSeed = 1187;

	FLayoutRegionPrewarmDescriptorInput Input;
	Input.SourceKind = ELayoutRegionPrewarmDescriptorSourceKind::RootScoutResult;
	Input.ArtifactId = TEXT("Artifact.RegionPrewarm.Root.0");
	Input.DescriptorId = TEXT("Descriptor.RegionPrewarm.Root.0");
	Input.SelectedResultId = TEXT("Scout.Root.RegionPrewarm.0");
	Input.RegionGroupId = 42;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.WorldSeed = 37;
	Input.FinalizedPrimaryBlockWorldPos = FIntVector(48, 80, 16);
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Region prewarm descriptor rejects source kind/snapshot mismatches"),
		LayoutRegionPrewarmDescriptorProducer::TryBuildProducedDescriptorArtifact(
			Input,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Source kind mismatch failure reported"), FailureReason.Contains(TEXT("source kind")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmDescriptorProducerRejectsWrongKindConsumptionTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.RegionPrewarmDescriptorRejectsWrongKindConsumption",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmDescriptorProducerRejectsWrongKindConsumptionTest::RunTest(const FString& Parameters)
{
	const FIntVector FinalizedSiteCenter(48, 80, 16);
	const int32 WorldSeed = 37;
	FLayoutPreSubmitFrozenSnapshot Snapshot = BuildSelectedRootSnapshotForTest(FinalizedSiteCenter, WorldSeed);

	FLayoutRegionPrewarmDescriptorInput Input;
	Input.SourceKind = ELayoutRegionPrewarmDescriptorSourceKind::RootScoutResult;
	Input.ArtifactId = TEXT("Artifact.RegionPrewarm.Root.Consumption");
	Input.DescriptorId = TEXT("Descriptor.RegionPrewarm.Root.Consumption");
	Input.SelectedResultId = TEXT("Scout.Root.RegionPrewarm.Consumption");
	Input.RegionGroupId = 42;
	Input.Generation = 7;
	Input.AttemptIndex = 0;
	Input.WorldSeed = WorldSeed;
	Input.FinalizedPrimaryBlockWorldPos = FinalizedSiteCenter;
	Input.Snapshot = Snapshot;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Generic region prewarm producer builds root descriptor artifact for consumption"),
		LayoutRegionPrewarmDescriptorProducer::TryBuildProducedDescriptorArtifact(
			Input,
			Artifact,
			FailureReason));

	FLayoutManifestPrewarmInput ManifestInput;
	TestTrue(
		TEXT("Root descriptor artifact is consumable as root manifest prewarm input"),
		LayoutRegionPrewarmDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmission(
			ELayoutRegionPrewarmDescriptorSourceKind::RootScoutResult,
			&Artifact,
			ManifestInput,
			FailureReason));

	TestFalse(
		TEXT("Root descriptor artifact is not consumable as continuation manifest prewarm input"),
		LayoutRegionPrewarmDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmission(
			ELayoutRegionPrewarmDescriptorSourceKind::ContinuationEdgeScoutResult,
			&Artifact,
			ManifestInput,
			FailureReason));
	TestTrue(TEXT("Wrong source kind consumption failure reported"), FailureReason.Contains(TEXT("source kind")));

	FLayoutFrozenSubmissionDescriptorSeed Seed;
	TestFalse(
		TEXT("Root descriptor seed is not consumable as child seed"),
		LayoutRegionPrewarmDescriptorProducer::TryGetDescriptorSeedForSolveSubmission(
			ELayoutRegionPrewarmDescriptorSourceKind::ChildScoutResult,
			&Artifact,
			Seed,
			FailureReason));
	TestTrue(TEXT("Wrong seed source kind failure reported"), FailureReason.Contains(TEXT("source kind")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerBuildsProducedArtifactTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerBuildsProducedArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerBuildsProducedArtifactTest::RunTest(const FString& Parameters)
{
	const FLayoutFrozenSubmissionDescriptorSeed Seed = BuildTestFrozenSubmissionDescriptorSeed(GetTransientPackage());
	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit produced artifact builds from seed"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSeedForTests(
			TEXT("Artifact.Region.Test.0"),
			Seed,
			Artifact,
			FailureReason));

	FLayoutFrozenSubmissionDescriptorSeed SubmitSeed;
	TestTrue(
		TEXT("Solve submission reads seed from explicit artifact"),
		LayoutFrozenSubmissionDescriptorProducer::TryGetSeedForSolveSubmissionForTests(
			&Artifact,
			SubmitSeed,
			FailureReason));
	TestEqual(TEXT("Artifact seed id preserved"), SubmitSeed.DescriptorId, Seed.DescriptorId);

	FLayoutManifestPrewarmInput PrewarmInput;
	TestTrue(
		TEXT("Solve submission reads prewarm input from explicit artifact"),
		LayoutFrozenSubmissionDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmissionForTests(
			&Artifact,
			PrewarmInput,
			FailureReason));
	TestTrue(TEXT("Produced artifact carries frozen manifest prewarm input"), PrewarmInput.bHasFrozenRequestManifest);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsMissingProducedArtifactTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsMissingProducedArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerRejectsMissingProducedArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenSubmissionDescriptorSeed SubmitSeed;
	FString FailureReason;
	TestFalse(
		TEXT("Missing produced artifact rejects seed consumption"),
		LayoutFrozenSubmissionDescriptorProducer::TryGetSeedForSolveSubmissionForTests(
			nullptr,
			SubmitSeed,
			FailureReason));

	FLayoutManifestPrewarmInput PrewarmInput;
	TestFalse(
		TEXT("Missing produced artifact rejects prewarm consumption"),
		LayoutFrozenSubmissionDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmissionForTests(
			nullptr,
			PrewarmInput,
			FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerStoresAfterSuccessfulPreflightTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerStoresAfterSuccessfulPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerStoresAfterSuccessfulPreflightTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenSubmissionStore Store;
	const FLayoutFrozenSubmissionDescriptorSeed Seed = BuildTestFrozenSubmissionDescriptorSeed(GetTransientPackage());

	FLayoutBackgroundSolveCompletion Completion;
	Completion.LifecycleStage = ELayoutBackgroundSolveLifecycleStage::AdmissibilityPreflight;
	Completion.bWorkSucceeded = true;
	FLayoutBackgroundAdmissibilityPreflightResult PreflightResult;
	PreflightResult.bAdmissible = true;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit produced artifact builds from seed"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSeedForTests(
			TEXT("Artifact.Region.StoreAfterPreflight"),
			Seed,
			Artifact,
			FailureReason));

	FLayoutFrozenSubmissionDescriptor StoredDescriptor;
	TestTrue(
		TEXT("Successful preflight stores descriptor only from explicit produced artifact"),
		LayoutFrozenSubmissionDescriptorProducer::TryStoreDescriptorFromSuccessfulPreflightArtifact(
			Completion,
			PreflightResult,
			Store,
			&Artifact,
			StoredDescriptor,
			FailureReason));
	TestNotNull(
		TEXT("Stored descriptor can be looked up by audit identity"),
		Store.FindValid(
			StoredDescriptor.DescriptorId,
			StoredDescriptor.RegionGroupId,
			StoredDescriptor.Generation,
			StoredDescriptor.AttemptIndex,
			StoredDescriptor.RegionKind,
			StoredDescriptor.AuditHash,
			FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerStoresProducedArtifactAfterSuccessfulPreflightTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerStoresProducedArtifactAfterSuccessfulPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerStoresProducedArtifactAfterSuccessfulPreflightTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenSubmissionStore Store;
	const FLayoutFrozenSubmissionDescriptorSeed Seed = BuildTestFrozenSubmissionDescriptorSeed(GetTransientPackage());
	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit produced artifact builds from seed"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSeedForTests(
			TEXT("Artifact.Region.Test.0"),
			Seed,
			Artifact,
			FailureReason));

	FLayoutBackgroundSolveCompletion Completion;
	Completion.LifecycleStage = ELayoutBackgroundSolveLifecycleStage::AdmissibilityPreflight;
	Completion.bWorkSucceeded = true;
	FLayoutBackgroundAdmissibilityPreflightResult PreflightResult;
	PreflightResult.bAdmissible = true;

	FLayoutFrozenSubmissionDescriptor StoredDescriptor;
	TestTrue(
		TEXT("Successful preflight stores descriptor from explicit produced artifact"),
		LayoutFrozenSubmissionDescriptorProducer::TryStoreDescriptorFromSuccessfulPreflightArtifact(
			Completion,
			PreflightResult,
			Store,
			&Artifact,
			StoredDescriptor,
			FailureReason));
	TestNotNull(
		TEXT("Produced artifact descriptor can be looked up by audit identity"),
		Store.FindValid(
			StoredDescriptor.DescriptorId,
			StoredDescriptor.RegionGroupId,
			StoredDescriptor.Generation,
			StoredDescriptor.AttemptIndex,
			StoredDescriptor.RegionKind,
			StoredDescriptor.AuditHash,
			FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsMissingProducedArtifactStoreTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsMissingProducedArtifactStore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerRejectsMissingProducedArtifactStoreTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenSubmissionStore Store;
	FLayoutBackgroundSolveCompletion Completion;
	Completion.LifecycleStage = ELayoutBackgroundSolveLifecycleStage::AdmissibilityPreflight;
	Completion.bWorkSucceeded = true;
	FLayoutBackgroundAdmissibilityPreflightResult PreflightResult;
	PreflightResult.bAdmissible = true;

	FLayoutFrozenSubmissionDescriptor StoredDescriptor;
	FString FailureReason;
	TestFalse(
		TEXT("Missing produced artifact cannot store descriptor"),
		LayoutFrozenSubmissionDescriptorProducer::TryStoreDescriptorFromSuccessfulPreflightArtifact(
			Completion,
			PreflightResult,
			Store,
			nullptr,
			StoredDescriptor,
			FailureReason));
	TestTrue(TEXT("Missing produced artifact failure reported"), FailureReason.Contains(TEXT("explicit produced")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionDescriptorProducerRejectsFailedPreflightTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.ProducerRejectsFailedPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionDescriptorProducerRejectsFailedPreflightTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenSubmissionStore Store;
	const FLayoutFrozenSubmissionDescriptorSeed Seed = BuildTestFrozenSubmissionDescriptorSeed(GetTransientPackage());

	FLayoutBackgroundSolveCompletion Completion;
	Completion.LifecycleStage = ELayoutBackgroundSolveLifecycleStage::AdmissibilityPreflight;
	Completion.bWorkSucceeded = false;
	Completion.FailureReason = TEXT("Expected preflight rejection.");
	FLayoutBackgroundAdmissibilityPreflightResult PreflightResult;
	PreflightResult.bAdmissible = false;

	FLayoutProducedFrozenDescriptorArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit produced artifact builds before failed preflight storage"),
		LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSeedForTests(
			TEXT("Artifact.Region.FailedPreflight"),
			Seed,
			Artifact,
			FailureReason));

	FLayoutFrozenSubmissionDescriptor StoredDescriptor;
	TestFalse(
		TEXT("Failed preflight does not store descriptor even with produced artifact"),
		LayoutFrozenSubmissionDescriptorProducer::TryStoreDescriptorFromSuccessfulPreflightArtifact(
			Completion,
			PreflightResult,
			Store,
			&Artifact,
			StoredDescriptor,
			FailureReason));
	TestNull(
		TEXT("Rejected preflight leaves store empty"),
		Store.FindValid(
			Seed.DescriptorId,
			Seed.RegionGroupId,
			Seed.Generation,
			Seed.AttemptIndex,
			Seed.RegionKind,
			0,
			FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionStoreAcceptsValidDescriptorTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.AcceptsValidDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionStoreAcceptsValidDescriptorTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenSubmissionStore Store;
	const FLayoutFrozenSubmissionDescriptor Descriptor = BuildTestFrozenSubmissionDescriptor(GetTransientPackage());

	FString FailureReason;
	TestTrue(TEXT("Valid descriptor inserts"), Store.AddOrReplace(Descriptor, FailureReason));
	const FLayoutFrozenSubmissionDescriptor* const FoundDescriptor = Store.FindValid(
		Descriptor.DescriptorId,
		Descriptor.RegionGroupId,
		Descriptor.Generation,
		Descriptor.AttemptIndex,
		Descriptor.RegionKind,
		Descriptor.AuditHash,
		FailureReason);
	TestNotNull(TEXT("Valid descriptor lookup succeeds"), FoundDescriptor);
	TestTrue(TEXT("Descriptor remove succeeds"), Store.Remove(Descriptor.DescriptorId));
	TestNull(
		TEXT("Removed descriptor lookup fails"),
		Store.FindValid(
			Descriptor.DescriptorId,
			Descriptor.RegionGroupId,
			Descriptor.Generation,
			Descriptor.AttemptIndex,
			Descriptor.RegionKind,
			Descriptor.AuditHash,
			FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionStoreRejectsPrewarmKindMismatchTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.RejectsPrewarmKindMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionStoreRejectsPrewarmKindMismatchTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenSubmissionStore Store;
	FLayoutFrozenSubmissionDescriptor Descriptor = BuildTestFrozenSubmissionDescriptor(GetTransientPackage());
	Descriptor.DescriptorId = TEXT("Region.PrewarmKindMismatch.0");
	Descriptor.RegionKind = ELayoutFrozenSubmissionRegionKind::Root;
	Descriptor.Snapshot.PrewarmKind = ELayoutManifestPrewarmKind::Continuation;
	Descriptor.AuditHash = FLayoutFrozenSubmissionStore::BuildAuditHash(
		Descriptor.DescriptorId,
		Descriptor.RegionGroupId,
		Descriptor.Generation,
		Descriptor.AttemptIndex,
		Descriptor.RegionKind,
		Descriptor.Snapshot);

	FString FailureReason;
	TestFalse(TEXT("Descriptor rejects mismatched region kind and prewarm kind"), Store.AddOrReplace(Descriptor, FailureReason));
	TestTrue(TEXT("Prewarm kind mismatch failure is reported"), FailureReason.Contains(TEXT("prewarm snapshot")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionStoreRejectsWorkerPacketKindMismatchTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.RejectsWorkerPacketKindMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionStoreRejectsWorkerPacketKindMismatchTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenSubmissionStore Store;
	FLayoutFrozenSubmissionDescriptor Descriptor = BuildTestFrozenSubmissionDescriptor(GetTransientPackage());
	Descriptor.DescriptorId = TEXT("Region.KindMismatch.0");
	Descriptor.RegionKind = ELayoutFrozenSubmissionRegionKind::Continuation;
	Descriptor.Snapshot = BuildTestContinuationFrozenSubmissionSnapshot(GetTransientPackage());
	Descriptor.Snapshot.WorkerSolvePacket.Kind = ELayoutWorkerSolvePacketKind::PlanningRoot;
	Descriptor.AuditHash = FLayoutFrozenSubmissionStore::BuildAuditHash(
		Descriptor.DescriptorId,
		Descriptor.RegionGroupId,
		Descriptor.Generation,
		Descriptor.AttemptIndex,
		Descriptor.RegionKind,
		Descriptor.Snapshot);

	FString FailureReason;
	TestFalse(TEXT("Descriptor rejects mismatched region kind and worker packet kind"), Store.AddOrReplace(Descriptor, FailureReason));
	TestTrue(TEXT("Kind mismatch failure is reported"), FailureReason.Contains(TEXT("worker packet kind")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionStoreRejectsActiveDescriptorReplacementMismatchTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.RejectsActiveDescriptorReplacementMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionStoreRejectsActiveDescriptorReplacementMismatchTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenSubmissionStore Store;
	const FLayoutFrozenSubmissionDescriptor Descriptor = BuildTestFrozenSubmissionDescriptor(GetTransientPackage());
	FLayoutFrozenSubmissionDescriptor ConflictingDescriptor = Descriptor;
	ConflictingDescriptor.Generation += 1;
	ConflictingDescriptor.AuditHash = FLayoutFrozenSubmissionStore::BuildAuditHash(
		ConflictingDescriptor.DescriptorId,
		ConflictingDescriptor.RegionGroupId,
		ConflictingDescriptor.Generation,
		ConflictingDescriptor.AttemptIndex,
		ConflictingDescriptor.RegionKind,
		ConflictingDescriptor.Snapshot);

	FString FailureReason;
	TestTrue(TEXT("Initial descriptor inserts"), Store.AddOrReplace(Descriptor, FailureReason));
	TestFalse(TEXT("Conflicting active descriptor replacement fails closed"), Store.AddOrReplace(ConflictingDescriptor, FailureReason));
	TestTrue(TEXT("Conflicting replacement reports active descriptor id"), FailureReason.Contains(TEXT("active descriptor id")));
	TestFalse(TEXT("Duplicate active descriptor replacement also fails closed"), Store.AddOrReplace(Descriptor, FailureReason));
	TestTrue(TEXT("Duplicate replacement reports active descriptor id"), FailureReason.Contains(TEXT("active descriptor id")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionStoreRejectsActiveRegionGenerationConflictTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.RejectsActiveRegionGenerationConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionStoreRejectsActiveRegionGenerationConflictTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenSubmissionStore Store;
	const FLayoutFrozenSubmissionDescriptor Descriptor = BuildTestFrozenSubmissionDescriptor(GetTransientPackage());
	FLayoutFrozenSubmissionDescriptor ConflictingDescriptor = BuildTestFrozenSubmissionDescriptor(GetTransientPackage());
	ConflictingDescriptor.DescriptorId = TEXT("Region.Test.1");
	ConflictingDescriptor.AttemptIndex = Descriptor.AttemptIndex + 1;
	ConflictingDescriptor.AuditHash = FLayoutFrozenSubmissionStore::BuildAuditHash(
		ConflictingDescriptor.DescriptorId,
		ConflictingDescriptor.RegionGroupId,
		ConflictingDescriptor.Generation,
		ConflictingDescriptor.AttemptIndex,
		ConflictingDescriptor.RegionKind,
		ConflictingDescriptor.Snapshot);

	FString FailureReason;
	TestTrue(TEXT("Initial descriptor inserts"), Store.AddOrReplace(Descriptor, FailureReason));
	TestFalse(TEXT("Second active descriptor for same region generation fails closed"), Store.AddOrReplace(ConflictingDescriptor, FailureReason));
	TestTrue(TEXT("Region-generation conflict reports active region group"), FailureReason.Contains(TEXT("active region group")));
	TestTrue(TEXT("Original descriptor remains valid"), Store.FindValid(
		Descriptor.DescriptorId,
		Descriptor.RegionGroupId,
		Descriptor.Generation,
		Descriptor.AttemptIndex,
		Descriptor.RegionKind,
		Descriptor.AuditHash,
		FailureReason) != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionStoreRejectsStaleAuditHashTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.RejectsStaleAuditHash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionStoreRejectsStaleAuditHashTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenSubmissionStore Store;
	const FLayoutFrozenSubmissionDescriptor Descriptor = BuildTestFrozenSubmissionDescriptor(GetTransientPackage());

	FString FailureReason;
	TestTrue(TEXT("Valid descriptor inserts"), Store.AddOrReplace(Descriptor, FailureReason));
	const FLayoutFrozenSubmissionDescriptor* const FoundDescriptor = Store.FindValid(
		Descriptor.DescriptorId,
		Descriptor.RegionGroupId,
		Descriptor.Generation,
		Descriptor.AttemptIndex,
		Descriptor.RegionKind,
		Descriptor.AuditHash + 1,
		FailureReason);
	TestNull(TEXT("Stale audit hash lookup fails"), FoundDescriptor);
	TestTrue(TEXT("Stale audit hash reports stale"), FailureReason.Contains(TEXT("stale")));
	FailureReason.Reset();
	TestNull(
		TEXT("Missing descriptor id lookup fails before store lookup"),
		Store.FindValid(
			NAME_None,
			Descriptor.RegionGroupId,
			Descriptor.Generation,
			Descriptor.AttemptIndex,
			Descriptor.RegionKind,
			Descriptor.AuditHash,
			FailureReason));
	TestTrue(TEXT("Missing descriptor id lookup reports descriptor id"), FailureReason.Contains(TEXT("descriptor id")));
	FailureReason.Reset();
	TestNull(
		TEXT("Invalid attempt lookup fails before store lookup"),
		Store.FindValid(
			Descriptor.DescriptorId,
			Descriptor.RegionGroupId,
			Descriptor.Generation,
			-1,
			Descriptor.RegionKind,
			Descriptor.AuditHash,
			FailureReason));
	TestTrue(TEXT("Invalid attempt lookup reports attempt"), FailureReason.Contains(TEXT("attempt")));
	FailureReason.Reset();
	TestNull(
		TEXT("Missing audit hash lookup fails before store lookup"),
		Store.FindValid(
			Descriptor.DescriptorId,
			Descriptor.RegionGroupId,
			Descriptor.Generation,
			Descriptor.AttemptIndex,
			Descriptor.RegionKind,
			0,
			FailureReason));
	TestTrue(TEXT("Missing audit hash lookup reports audit hash"), FailureReason.Contains(TEXT("audit hash")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFrozenSubmissionStoreRejectsPayloadAuditMismatchTest,
	"PorismExtension.Layout.Async.FrozenSubmissionStore.RejectsPayloadAuditMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFrozenSubmissionStoreRejectsPayloadAuditMismatchTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenSubmissionStore Store;
	FLayoutFrozenSubmissionDescriptor Descriptor = BuildTestFrozenSubmissionDescriptor(GetTransientPackage());
	Descriptor.Snapshot.SnapshotId = TEXT("Region.Test.MutatedSnapshot");

	FString FailureReason;
	TestFalse(TEXT("Descriptor insert rejects payload mutated after audit hash"), Store.AddOrReplace(Descriptor, FailureReason));
	TestTrue(TEXT("Payload audit mismatch reports audit hash failure"), FailureReason.Contains(TEXT("audit hash")));
	return true;
}
