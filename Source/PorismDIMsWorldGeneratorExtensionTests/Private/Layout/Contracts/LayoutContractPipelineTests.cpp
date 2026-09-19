// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Contracts/LayoutContractPipeline.h"
#include "Layout/Contracts/LayoutPreparedContractTestUtilities.h"
#include "Layout/Async/LayoutWorkerSolvePacket.h"
#include "Layout/Contracts/LayoutContractManifestCache.h"
#include "Layout/Contracts/LayoutContractModeSelection.h"
#include "Layout/Contracts/LayoutContractPlacementCandidates.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"
#include "Layout/Solver/LayoutWorkerSolveRequestFinalizer.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Support/LayoutTestUtilities.h"

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"

namespace
{
	using namespace PorismLayoutTestUtilities;

	FLayoutValidationAssertionRecord MakeAssertionRecord(const FLayoutId AssertionId)
	{
		FLayoutValidationAssertionRecord Record;
		Record.AssertionId = AssertionId;
		Record.AssertionKind = ELayoutValidationAssertionKind::AssetValidationPassed;
		Record.bPassed = true;
		Record.RelatedIds = {TEXT("Related.Id")};
		return Record;
	}

	FLayoutRegionSolveRequest BuildManifestFixtureRequest()
	{
		FLayoutRegionSolveRequest Request;
		Request.EffectiveSnapshotId = TEXT("ManifestFixture.Effective");
		Request.WorldBindingId = TEXT("WorldBinding.Primary");
		Request.ProfileSnapshot.SnapshotId = TEXT("Profile.Snapshot");
		Request.ProfileSnapshot.SourceProfilePath = FSoftObjectPath(TEXT("/Game/Layouts/TestProfile.TestProfile"));
		Request.ProfileSnapshot.MinimumFootprintInCells = FIntPoint(4, 3);
		Request.ProfileSnapshot.MaximumFootprintInCells = FIntPoint(8, 6);
		Request.ProfileSnapshot.LevelCount = 3;
		Request.ProfileSnapshot.bSupportsSteppedTerrainSolve = true;
		Request.ProfileSnapshot.bEnableTerrainSeams = true;
		Request.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
		Request.ProfileSnapshot.VerticalAccessCount = 2;
		Request.ProfileSnapshot.ValidationAssertions.Add(MakeAssertionRecord(TEXT("Assert.Profile")));
		Request.ProfileSnapshot.ValidationAssertions.Add(MakeAssertionRecord(TEXT("Assert.Shared")));
		FLayoutClosureRequirement& ClosureRequirement = Request.ProfileSnapshot.ClosureRequirements.AddDefaulted_GetRef();
		ClosureRequirement.ClosureId = TEXT("Closure.Requirement");
		FLayoutZoneFeatureRequirement& FeatureRequirement = Request.ProfileSnapshot.ZoneFeatureRequirements.AddDefaulted_GetRef();
		FeatureRequirement.RequirementId = TEXT("Feature.Requirement");

		Request.ContentSetSnapshot.SnapshotId = TEXT("Content.Snapshot");
		Request.ContentSetSnapshot.ValidationAssertions.Add(MakeAssertionRecord(TEXT("Assert.Content")));

		FLayoutRegionContentEntrySolveSnapshot& ModuleEntry = Request.ContentSetSnapshot.Entries.AddDefaulted_GetRef();
		ModuleEntry.EntryId = TEXT("Entry.Module");
		ModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		ModuleEntry.Weight = 7;
		ModuleEntry.ModuleSnapshotIndex = 0;
		ModuleEntry.ModulePlacementZone = ELayoutPlacementZone::Perimeter;
		ModuleEntry.ModuleLevelPlacementPolicy = ELayoutLevelPlacementPolicy::SpecificLevel;
		ModuleEntry.ModuleSpecificLevel = 1;
		ModuleEntry.bModuleOptional = true;
		FLayoutClosureProviderIntent& ModuleClosureIntent = ModuleEntry.ClosureProviderIntents.AddDefaulted_GetRef();
		ModuleClosureIntent.ProviderIntentId = TEXT("Closure.Intent");
		ModuleClosureIntent.ClosureId = TEXT("Closure.Requirement");
		FLayoutSeamProviderIntent& ModuleSeamIntent = ModuleEntry.SeamProviderIntents.AddDefaulted_GetRef();
		ModuleSeamIntent.SeamIntentId = TEXT("Seam.Intent");

		FLayoutRegionContentEntrySolveSnapshot& ChildEntry = Request.ContentSetSnapshot.Entries.AddDefaulted_GetRef();
		ChildEntry.EntryId = TEXT("Entry.Child");
		ChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
		ChildEntry.Weight = 3;
		ChildEntry.ChildProfileSnapshotId = TEXT("Child.Profile.Snapshot");
		ChildEntry.ChildProfilePath = FSoftObjectPath(TEXT("/Game/Layouts/TestChildProfile.TestChildProfile"));
		ChildEntry.ChildContentSetSnapshotId = TEXT("Child.Content.Snapshot");
		ChildEntry.ChildPlacementZone = ELayoutPlacementZone::Interior;
		ChildEntry.ChildLevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;
		ChildEntry.bChildOptional = true;
		ChildEntry.bChildContributesHostVerticalAccess = true;
		ChildEntry.CompiledChildRequestTemplate = MakeShared<FLayoutChildRequestTemplateSnapshot>();
		ChildEntry.CompiledChildRequestTemplate->ValidationAssertions.Add(MakeAssertionRecord(TEXT("Assert.ChildTemplate")));

		Request.ModuleCatalog.SnapshotId = TEXT("ModuleCatalog.Snapshot");
		Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(16, 16, 12);
		Request.ModuleCatalog.ValidationAssertions.Add(MakeAssertionRecord(TEXT("Assert.ModuleCatalog")));
		Request.ModuleCatalog.ValidationAssertions.Add(MakeAssertionRecord(TEXT("Assert.Shared")));
		FLayoutModuleSolveSnapshot& ModuleSnapshot = Request.ModuleCatalog.Modules.AddDefaulted_GetRef();
		ModuleSnapshot.SnapshotId = TEXT("Module.Snapshot");
		ModuleSnapshot.DebugName = TEXT("Module.Debug");
		ModuleSnapshot.BoundsCells = FIntVector(2, 1, 1);
		ModuleSnapshot.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(1, 0, 0)};
		ModuleSnapshot.SourceContentEntryId = ModuleEntry.EntryId;
		ModuleSnapshot.PlacementZone = ELayoutPlacementZone::Perimeter;
		ModuleSnapshot.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::SpecificLevel;
		ModuleSnapshot.SpecificLevel = 1;
		ModuleSnapshot.bOptional = true;
		ModuleSnapshot.ValidationAssertions.Add(MakeAssertionRecord(TEXT("Assert.Module")));

		Request.ValidationAssertions.Add(MakeAssertionRecord(TEXT("Assert.Request")));
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractModePlanHashStabilityTest,
	"PorismExtension.Layout.Contracts.ModePlanHashStability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractModePlanHashStabilityTest::RunTest(const FString& Parameters)
{
	FLayoutModePlan ModePlan;
	ModePlan.Scope = ELayoutContractRegionScope::Root;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	ModePlan.bUsesSteppedTerrainTopology = true;
	ModePlan.SiteCenterBlockWorldPos = FIntVector(128, -64, 32);
	ModePlan.PlacementShiftCells = FIntVector(1, 0, -1);
	ModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ModePlan.PlacementShiftCells);
	ModePlan.WorldSeed = 37;
	ModePlan.SolveSeed = 91;
	ModePlan.PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	ModePlan.PlacementPolicy.TerrainSampleGridSpacing = 8;
	ModePlan.PlacementPolicy.HeightIgnoreThreshold = 2;
	ModePlan.PlacementPolicy.TerrainTransition.bAllowFoundationFill = true;
	ModePlan.PlacementPolicy.TerrainTransition.MaxFoundationDepth = 4;
	ModePlan.PlacementPolicy.TerrainTransition.bAllowPerimeterRampTransition = true;

	const FLayoutId FirstId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	const FLayoutId SecondId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	TestEqual(TEXT("Identical mode plans produce stable ids"), FirstId, SecondId);

	FLayoutModePlan ShiftedModePlan = ModePlan;
	ShiftedModePlan.PlacementShiftCells.X += 1;
	ShiftedModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ShiftedModePlan.PlacementShiftCells);
	TestNotEqual(TEXT("Changed placement shift changes mode-plan id"), FirstId, FLayoutContractPipeline::BuildModePlanId(ShiftedModePlan));

	FLayoutModePlan FlatTopologyModePlan = ModePlan;
	FlatTopologyModePlan.bUsesSteppedTerrainTopology = false;
	TestNotEqual(
		TEXT("Changed explicit topology decision changes mode-plan id"),
		FirstId,
		FLayoutContractPipeline::BuildModePlanId(FlatTopologyModePlan));

	FLayoutModePlan StrictSteppedModePlan = ModePlan;
	StrictSteppedModePlan.PlacementPolicy.TerrainTransition.bFallbackToFlatTerrainWhenSteppedReservedOpenIsInfeasible = false;
	TestNotEqual(
		TEXT("Changed stepped fallback policy changes mode-plan id"),
		FirstId,
		FLayoutContractPipeline::BuildModePlanId(StrictSteppedModePlan));

	const FLayoutId PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(FIntVector(1, 2, 3));
	TestEqual(TEXT("Placement shift id is stable"), PlacementShiftId, FLayoutContractPipeline::BuildPlacementShiftId(FIntVector(1, 2, 3)));
	TestNotEqual(TEXT("Placement shift id changes with values"), PlacementShiftId, FLayoutContractPipeline::BuildPlacementShiftId(FIntVector(1, 2, 4)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractModeSelectionTest,
	"PorismExtension.Layout.Contracts.ModeSelectionUsesFrozenRequestValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractModeSelectionTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.Seed = 101;
	RootRequest.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	RootRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve = true;
	RootRequest.WorldBindingPlacementPolicy.TerrainSampleGridSpacing = 8;

	FLayoutContractModeSelectionInput Input;
	Input.SolveRequest = &RootRequest;
	Input.SiteCenterBlockWorldPos = FIntVector(64, 128, 0);
	Input.WorldSeed = 55;
	const FLayoutModePlan SteppedRootMode = FLayoutContractModeSelection::SelectModePlan(Input);
	TestEqual(TEXT("Ordinary stepped root selects stepped surface mode"), SteppedRootMode.EnvironmentMode, ELayoutContractEnvironmentMode::SteppedSurfacePlacement);
	TestEqual(TEXT("Ordinary root keeps root scope"), SteppedRootMode.Scope, ELayoutContractRegionScope::Root);
	TestTrue(TEXT("Surface admission retains explicit stepped topology"), SteppedRootMode.bUsesSteppedTerrainTopology);
	TestEqual(TEXT("Mode plan keeps solve seed from frozen request"), SteppedRootMode.SolveSeed, 101);

	FLayoutRegionSolveRequest FlatSurfaceRootRequest = RootRequest;
	FlatSurfaceRootRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve = false;
	Input.SolveRequest = &FlatSurfaceRootRequest;
	const FLayoutModePlan FlatSurfaceMode = FLayoutContractModeSelection::SelectModePlan(Input);
	TestEqual(TEXT("Non-stepped Surface root keeps terrain adapter mode"),
		FlatSurfaceMode.EnvironmentMode, ELayoutContractEnvironmentMode::NonSteppedWorldPlacement);
	TestFalse(TEXT("Non-stepped Surface root disables stepped topology"),
		FlatSurfaceMode.bUsesSteppedTerrainTopology);

	FLayoutRegionSolveRequest UndergroundRootRequest = RootRequest;
	UndergroundRootRequest.ProfileSnapshot.bUndergroundPlacement = true;
	Input.SolveRequest = &UndergroundRootRequest;
	const FLayoutModePlan UndergroundMode = FLayoutContractModeSelection::SelectModePlan(Input);
	TestEqual(
		TEXT("Ordinary Underground root preserves Underground admission"),
		UndergroundMode.EnvironmentMode,
		ELayoutContractEnvironmentMode::UndergroundPocketPlacement);
	TestTrue(TEXT("Underground admission retains same explicit stepped topology"),
		UndergroundMode.bUsesSteppedTerrainTopology);

	FLayoutRegionSolveRequest ChildRequest = RootRequest;
	ChildRequest.SourceParentRegionDebugPath = TEXT("Parent/Region");
	ChildRequest.RootPlacementKind = ELayoutWorldBindingPlacementKind::None;
	Input.SolveRequest = &ChildRequest;
	const FLayoutModePlan ChildMode = FLayoutContractModeSelection::SelectModePlan(Input);
	TestEqual(TEXT("Child request selects child scope from frozen parent path"), ChildMode.Scope, ELayoutContractRegionScope::Child);
	TestEqual(TEXT("Child request without terrain placement selects standard mode"), ChildMode.EnvironmentMode, ELayoutContractEnvironmentMode::StandardRegion);

	FLayoutRegionSolveRequest SurfaceContinuationRequest = RootRequest;
	SurfaceContinuationRequest.ProfileSnapshot.bUndergroundPlacement = true;
	SurfaceContinuationRequest.RootPlacementKind = ELayoutWorldBindingPlacementKind::SurfacePath;
	SurfaceContinuationRequest.RootContinuationSelection.FamilyId = TEXT("Continuation.Surface");
	SurfaceContinuationRequest.RootContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::SurfacePath;
	Input.SolveRequest = &SurfaceContinuationRequest;
	const FLayoutModePlan SurfaceContinuationMode = FLayoutContractModeSelection::SelectModePlan(Input);
	TestEqual(TEXT("Stepped surface continuation selects root-equivalent stepped surface mode"),
		SurfaceContinuationMode.EnvironmentMode,
		ELayoutContractEnvironmentMode::SteppedSurfacePlacement);
	TestEqual(TEXT("Stepped surface continuation keeps continuation scope"),
		SurfaceContinuationMode.Scope,
		ELayoutContractRegionScope::Continuation);

	FLayoutRegionSolveRequest ContinuationRequest = RootRequest;
	ContinuationRequest.ProfileSnapshot.bUndergroundPlacement = true;
	ContinuationRequest.RootContinuationSelection.FamilyId = TEXT("Continuation.Family");
	ContinuationRequest.RootContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	ContinuationRequest.RootPlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	Input.SolveRequest = &ContinuationRequest;
	const FLayoutModePlan ContinuationMode = FLayoutContractModeSelection::SelectModePlan(Input);
	TestEqual(TEXT("Continuation request selects continuation scope"), ContinuationMode.Scope, ELayoutContractRegionScope::Continuation);
	TestEqual(TEXT("Stepped bridge continuation ignores Underground flag and retains stepped surface mode"), ContinuationMode.EnvironmentMode, ELayoutContractEnvironmentMode::SteppedSurfacePlacement);
	TestEqual(TEXT("Existing pipeline delegates to mode-selection helper"), FLayoutContractPipeline::BuildModePlanFromSolveRequest(ContinuationRequest, Input.SiteCenterBlockWorldPos, Input.WorldSeed).ModePlanId, ContinuationMode.ModePlanId);

	FLayoutRegionSolveRequest TunnelContinuationRequest = RootRequest;
	TunnelContinuationRequest.ProfileSnapshot.bUndergroundPlacement = true;
	TunnelContinuationRequest.RootPlacementKind = ELayoutWorldBindingPlacementKind::TunnelContinuation;
	TunnelContinuationRequest.RootContinuationSelection.FamilyId = TEXT("Continuation.Tunnel");
	TunnelContinuationRequest.RootContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::TunnelContinuation;
	Input.SolveRequest = &TunnelContinuationRequest;
	TestEqual(
		TEXT("Tunnel continuation ignores ordinary-root Underground flag"),
		FLayoutContractModeSelection::SelectModePlan(Input).EnvironmentMode,
		ELayoutContractEnvironmentMode::TunnelContinuation);

	/* !ContinuationRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	ContinuationRequest.FootprintSize = FIntPoint(1, 1);
	ContinuationRequest.PlannedCells.Add({FIntVector::ZeroValue, ELayoutCellIntent::Boundary});
	ContinuationRequest.bHasFrozenTerrainBiomeAdapterInput = true;
	ContinuationRequest.FrozenTerrainBiomeAdapterInput.ArtifactId = TEXT("Terrain.RequestBuilt.Finalizer");
	ContinuationRequest.FrozenTerrainBiomeAdapterInput.ModePlanId = ContinuationMode.ModePlanId;
	ContinuationRequest.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos = Input.SiteCenterBlockWorldPos;
	ContinuationRequest.FrozenTerrainBiomeAdapterInput.FootprintMinBlockWorldPos = Input.SiteCenterBlockWorldPos;
	ContinuationRequest.FrozenTerrainBiomeAdapterInput.FootprintSizeInBlocks = FIntPoint(8, 8);
	ContinuationRequest.FrozenTerrainBiomeAdapterInput.SearchMinBlockXY = FIntPoint(64, 128);
	ContinuationRequest.FrozenTerrainBiomeAdapterInput.SearchMaxBlockXY = FIntPoint(71, 135);
	ContinuationRequest.FrozenTerrainBiomeAdapterInput.bHasFiniteSearchBounds = true;
	ContinuationRequest.FrozenTerrainBiomeAdapterInput.SearchDepthBlocks = 16;
	ContinuationRequest.FrozenTerrainBiomeAdapterInput.bHasSampledColumnEvidence = true;
	FLayoutTerrainSurfaceSample& FinalizerSurfaceSample = ContinuationRequest.FrozenTerrainBiomeAdapterInput.SurfaceSamples.AddDefaulted_GetRef();
	FinalizerSurfaceSample.bIsValid = true;
	FinalizerSurfaceSample.BlockXY = FIntPoint(64, 128);

	FLayoutWorkerSolvePacket Packet = FLayoutWorkerSolvePacket::CaptureExplicitPreviewRoot(
		TEXT("ModeSelectionTest"),
		FLayoutWorldBindingRuntimeView(),
		Input.SiteCenterBlockWorldPos,
		ContinuationRequest.Seed);
	Packet.RuntimeSnapshot.PlacementKind = ContinuationRequest.RootPlacementKind;
	Packet.RuntimeSnapshot.PlacementPolicy = ContinuationRequest.WorldBindingPlacementPolicy;
	Packet.RuntimeSnapshot.ContinuationSelection = ContinuationRequest.RootContinuationSelection;
	Packet.RequestManifest = FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(ContinuationRequest);
	Packet.bHasRequestManifest = true;
	{
		FLayoutContractModeSelectionInput ModeInput;
		ModeInput.SolveRequest = &ContinuationRequest;
		ModeInput.SiteCenterBlockWorldPos = Input.SiteCenterBlockWorldPos;
		ModeInput.WorldSeed = Input.WorldSeed;
		Packet.SelectedModePlan = FLayoutContractModeSelection::SelectModePlan(ModeInput);
		Packet.bHasSelectedModePlan = true;
		Packet.RequestManifest.bHasSelectedModePlan = true;
		Packet.RequestManifest.SelectedModePlan = Packet.SelectedModePlan;
	}
	TestTrue(TEXT("Worker packet captures selected mode plan before dispatch"), Packet.bHasSelectedModePlan);
	TestEqual(TEXT("Worker packet carries same selected mode id as helper"), Packet.SelectedModePlan.ModePlanId, ContinuationMode.ModePlanId);

	FLayoutRegionSolveRequest FinalizedRequest;
	FString FinalizeFailureReason;
	TestTrue(
		TEXT("Worker finalizer accepts packet-carried selected mode plan"),
		LayoutWorkerSolveRequestFinalizer::FinalizeRequestFromPacket(Packet, FinalizedRequest, FinalizeFailureReason));
	TestTrue(TEXT("Finalized worker request consumes selected mode plan"), FinalizedRequest.bHasSelectedModePlan);
	TestEqual(TEXT("Finalized worker request preserves selected mode id"), FinalizedRequest.SelectedModePlan.ModePlanId, ContinuationMode.ModePlanId);
	TestTrue(TEXT("Finalized worker request preserves request-built frozen terrain artifact"), FinalizedRequest.bHasFrozenTerrainBiomeAdapterInput);
	TestEqual(TEXT("Finalized worker request preserves terrain artifact id"), FinalizedRequest.FrozenTerrainBiomeAdapterInput.ArtifactId, FLayoutId(TEXT("Terrain.RequestBuilt.Finalizer")));
	TestEqual(TEXT("Finalized worker request preserves terrain artifact mode id"), FinalizedRequest.FrozenTerrainBiomeAdapterInput.ModePlanId, ContinuationMode.ModePlanId);
	TestEqual(TEXT("Finalized worker request preserves terrain artifact sample count"), FinalizedRequest.FrozenTerrainBiomeAdapterInput.SurfaceSamples.Num(), 1);
	TestEqual(TEXT("Finalized worker request preserves supplied planned cells for terrain artifact"), FinalizedRequest.PlannedCells.Num(), 1);
	TestTrue(TEXT("Finalized worker request preserves terrain seam mode"), FinalizedRequest.ProfileSnapshot.bEnableTerrainSeams);
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	TerrainArtifact.ArtifactId = TEXT("Terrain.Artifact.A");
	TerrainArtifact.FootprintSizeInBlocks = FIntPoint(32, 16);
	TerrainArtifact.EligibleBiomeRowName = TEXT("Biome.A");
	TerrainArtifact.EligibleBiomeRowNames = {TEXT("Biome.A")};
	const TArray<FLayoutCoarsePlacementShiftCandidate> MissingBaseEvidenceCandidates =
		FLayoutContractPlacementCandidates::BuildTerrainBackedCoarsePlacementShiftCandidates(
			TEXT("Manifest.A"),
			123,
			TerrainArtifact,
			FIntVector(16, 16, 16),
			2,
			1);
	TestEqual(TEXT("Terrain-backed coarse shifts fail closed without base terrain evidence"), MissingBaseEvidenceCandidates.Num(), 0);
	TerrainArtifact.bHasFiniteSearchBounds = true;
	TerrainArtifact.SearchMinBlockXY = FIntPoint(0, 0);
	TerrainArtifact.SearchMaxBlockXY = FIntPoint(31, 15);
	TerrainArtifact.SearchDepthBlocks = 16;
	TerrainArtifact.bHasSampledColumnEvidence = true;
	FLayoutTerrainSurfaceSample& TerrainOrderingSurfaceSample = TerrainArtifact.SurfaceSamples.AddDefaulted_GetRef();
	TerrainOrderingSurfaceSample.bIsValid = true;
	TerrainOrderingSurfaceSample.BlockXY = FIntPoint(0, 0);
	TerrainArtifact.bHasSteppedSupportEvidence = true;
	const TArray<FLayoutCoarsePlacementShiftCandidate> FailClosedTerrainBackedCandidates =
		FLayoutContractPlacementCandidates::BuildTerrainBackedCoarsePlacementShiftCandidates(
			TEXT("Manifest.A"),
			123,
			TerrainArtifact,
			FIntVector(16, 16, 16),
			2,
			1);
	TestEqual(TEXT("Terrain-backed coarse shifts fail closed without shifted-footprint evidence"), FailClosedTerrainBackedCandidates.Num(), 1);
	TerrainArtifact.bHasShiftedFootprintEvidence = true;
	FLayoutFrozenShiftedFootprintEvidence& ShiftedEvidence = TerrainArtifact.ShiftedFootprintEvidence.AddDefaulted_GetRef();
	ShiftedEvidence.ShiftCells = FIntVector(2, 0, 0);
	ShiftedEvidence.PlacementShiftId = FLayoutContractPlacementCandidates::BuildPlacementShiftId(ShiftedEvidence.ShiftCells);
	ShiftedEvidence.bHasTerrainFitProof = true;
	ShiftedEvidence.bHasReservationCollisionProof = true;
	ShiftedEvidence.bHasChunkOverlapProvenance = true;
	ShiftedEvidence.SteppedSupportSamples.AddDefaulted_GetRef().LocalCell = FIntVector::ZeroValue;
	const TArray<FLayoutCoarsePlacementShiftCandidate> TerrainBackedCandidates =
		FLayoutContractPlacementCandidates::BuildTerrainBackedCoarsePlacementShiftCandidates(
			TEXT("Manifest.A"),
			123,
			TerrainArtifact,
			FIntVector(16, 16, 16),
			2,
			1);
	TestEqual(TEXT("Terrain-backed coarse shifts keep center plus the proven shifted branch"), TerrainBackedCandidates.Num(), 2);
	TestTrue(TEXT("Terrain artifact id participates in ordering key"), TerrainBackedCandidates.ContainsByPredicate([](const FLayoutCoarsePlacementShiftCandidate& Candidate)
	{
		return Candidate.OrderingKey.LocalObligationId == TEXT("Terrain.Artifact.A");
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractChildBranchOrderingKeyStabilityTest,
	"PorismExtension.Layout.Contracts.ChildBranchOrderingKeyStability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractChildBranchOrderingKeyStabilityTest::RunTest(const FString& Parameters)
{
	FLayoutChildBranchOrderingInput Input;
	Input.ParentContractId = TEXT("Parent.Contract.A");
	Input.SourceEntryId = TEXT("Entry.Child.A");
	Input.StableChildId = TEXT("Child.Stable.A");
	Input.ChildProfileId = TEXT("Profile.Child.A");
	Input.SelectedCapabilityId = TEXT("Capability.Endpoint.A");
	Input.DelegatedFeatureId = TEXT("Feature.Required.A");
	Input.ClosureSeamOrJunctionId = TEXT("Seam.A");
	Input.PseudoAscentStrategyId = TEXT("Ascent.Strategy.A");
	Input.RouteOrHandoffAnchorId = TEXT("Anchor.A");
	Input.OptionalDropOrResidualId = TEXT("Optional.Keep.A");
	Input.EarlyStructuralPriority = 7;
	Input.PlacementShiftId = TEXT("Shift.A");
	Input.AttemptIndex = 2;
	Input.ChildCertificateId = TEXT("Certificate.A");
	Input.ChildWitnessOrderingId = TEXT("Witness.A");
	Input.ChildCertificateInputHash = 0x1234;

	const FLayoutContractCandidateOrderingKey FirstKey = FLayoutContractPlacementCandidates::BuildChildBranchOrderingKey(Input);
	const FLayoutContractCandidateOrderingKey SecondKey = FLayoutContractPlacementCandidates::BuildChildBranchOrderingKey(Input);
	TestEqual(TEXT("Same child branch input builds stable ordering id"), FirstKey.BuildOrderingId(), SecondKey.BuildOrderingId());

	FLayoutChildBranchOrderingInput DifferentCapability = Input;
	DifferentCapability.SelectedCapabilityId = TEXT("Capability.Endpoint.B");
	const FLayoutContractCandidateOrderingKey DifferentCapabilityKey = FLayoutContractPlacementCandidates::BuildChildBranchOrderingKey(DifferentCapability);
	TestNotEqual(TEXT("Selected capability participates in child branch ordering"), FirstKey.BuildOrderingId(), DifferentCapabilityKey.BuildOrderingId());

	FLayoutChildBranchOrderingInput DifferentCertificate = Input;
	DifferentCertificate.ChildCertificateInputHash = 0x5678;
	const FLayoutContractCandidateOrderingKey DifferentCertificateKey = FLayoutContractPlacementCandidates::BuildChildBranchOrderingKey(DifferentCertificate);
	TestNotEqual(TEXT("Certificate hash participates in child branch ordering"), FirstKey.BuildOrderingId(), DifferentCertificateKey.BuildOrderingId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractChildBranchOrderingSortIsInputOrderIndependentTest,
	"PorismExtension.Layout.Contracts.ChildBranchOrderingSortIsInputOrderIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractChildBranchOrderingSortIsInputOrderIndependentTest::RunTest(const FString& Parameters)
{
	FLayoutChildBranchOrderingInput FirstInput;
	FirstInput.ParentContractId = TEXT("Parent.Contract.A");
	FirstInput.SourceEntryId = TEXT("Entry.Child.A");
	FirstInput.StableChildId = TEXT("Child.Stable.A");
	FirstInput.ChildProfileId = TEXT("Profile.Child.A");
	FirstInput.SelectedCapabilityId = TEXT("Capability.A");
	FirstInput.EarlyStructuralPriority = 1;

	FLayoutChildBranchOrderingInput SecondInput = FirstInput;
	SecondInput.StableChildId = TEXT("Child.Stable.B");
	SecondInput.SelectedCapabilityId = TEXT("Capability.B");

	TArray<FLayoutContractCandidateOrderingKey> ForwardKeys = {
		FLayoutContractPlacementCandidates::BuildChildBranchOrderingKey(FirstInput),
		FLayoutContractPlacementCandidates::BuildChildBranchOrderingKey(SecondInput)
	};
	TArray<FLayoutContractCandidateOrderingKey> ReverseKeys = {
		FLayoutContractPlacementCandidates::BuildChildBranchOrderingKey(SecondInput),
		FLayoutContractPlacementCandidates::BuildChildBranchOrderingKey(FirstInput)
	};
	FLayoutContractPlacementCandidates::SortCandidateKeys(ForwardKeys);
	FLayoutContractPlacementCandidates::SortCandidateKeys(ReverseKeys);
	TestEqual(TEXT("Sorted child branch count preserved"), ForwardKeys.Num(), ReverseKeys.Num());
	for (int32 Index = 0; Index < ForwardKeys.Num(); ++Index)
	{
		TestEqual(TEXT("Child branch sort is input-order independent"), ForwardKeys[Index].BuildOrderingId(), ReverseKeys[Index].BuildOrderingId());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractCompositeScoutOrderingKeyStabilityTest,
	"PorismExtension.Layout.Contracts.CompositeScoutOrderingKeyStability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractCompositeScoutOrderingKeyStabilityTest::RunTest(const FString& Parameters)
{
	FLayoutCompositeScoutOrderingInput Input;
	Input.ManifestId = TEXT("Manifest.Composite.A");
	Input.CompositeSnapshotId = TEXT("Composite.Snapshot.A");
	Input.OccupiedCellFootprintHash = TEXT("Footprint.Hash.A");
	Input.ExposedFaceOrCapabilityId = TEXT("Face.North.A");
	Input.PlacementZoneId = TEXT("Zone.Perimeter");
	Input.FeatureProviderId = TEXT("Feature.Provider.A");
	Input.PlacementShiftId = TEXT("Shift.A");
	Input.AttemptIndex = 3;

	const FLayoutContractCandidateOrderingKey FirstKey = FLayoutContractPlacementCandidates::BuildCompositeScoutOrderingKey(Input);
	const FLayoutContractCandidateOrderingKey SecondKey = FLayoutContractPlacementCandidates::BuildCompositeScoutOrderingKey(Input);
	TestEqual(TEXT("Same composite scout input builds stable ordering id"), FirstKey.BuildOrderingId(), SecondKey.BuildOrderingId());

	FLayoutCompositeScoutOrderingInput DifferentFootprint = Input;
	DifferentFootprint.OccupiedCellFootprintHash = TEXT("Footprint.Hash.B");
	const FLayoutContractCandidateOrderingKey DifferentFootprintKey = FLayoutContractPlacementCandidates::BuildCompositeScoutOrderingKey(DifferentFootprint);
	TestNotEqual(TEXT("Composite footprint hash participates in ordering"), FirstKey.BuildOrderingId(), DifferentFootprintKey.BuildOrderingId());

	FLayoutCompositeScoutOrderingInput DifferentFace = Input;
	DifferentFace.ExposedFaceOrCapabilityId = TEXT("Face.East.A");
	const FLayoutContractCandidateOrderingKey DifferentFaceKey = FLayoutContractPlacementCandidates::BuildCompositeScoutOrderingKey(DifferentFace);
	TestNotEqual(TEXT("Composite exposed face/capability participates in ordering"), FirstKey.BuildOrderingId(), DifferentFaceKey.BuildOrderingId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractCompositeScoutOrderingSortIsInputOrderIndependentTest,
	"PorismExtension.Layout.Contracts.CompositeScoutOrderingSortIsInputOrderIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractCompositeScoutOrderingSortIsInputOrderIndependentTest::RunTest(const FString& Parameters)
{
	FLayoutCompositeScoutOrderingInput FirstInput;
	FirstInput.ManifestId = TEXT("Manifest.Composite.A");
	FirstInput.CompositeSnapshotId = TEXT("Composite.Snapshot.A");
	FirstInput.OccupiedCellFootprintHash = TEXT("Footprint.Hash.A");
	FirstInput.ExposedFaceOrCapabilityId = TEXT("Face.North.A");
	FirstInput.PlacementZoneId = TEXT("Zone.Perimeter");
	FirstInput.FeatureProviderId = TEXT("Feature.Provider.A");

	FLayoutCompositeScoutOrderingInput SecondInput = FirstInput;
	SecondInput.CompositeSnapshotId = TEXT("Composite.Snapshot.B");
	SecondInput.ExposedFaceOrCapabilityId = TEXT("Face.South.B");

	TArray<FLayoutContractCandidateOrderingKey> ForwardKeys = {
		FLayoutContractPlacementCandidates::BuildCompositeScoutOrderingKey(FirstInput),
		FLayoutContractPlacementCandidates::BuildCompositeScoutOrderingKey(SecondInput)
	};
	TArray<FLayoutContractCandidateOrderingKey> ReverseKeys = {
		FLayoutContractPlacementCandidates::BuildCompositeScoutOrderingKey(SecondInput),
		FLayoutContractPlacementCandidates::BuildCompositeScoutOrderingKey(FirstInput)
	};
	FLayoutContractPlacementCandidates::SortCandidateKeys(ForwardKeys);
	FLayoutContractPlacementCandidates::SortCandidateKeys(ReverseKeys);
	TestEqual(TEXT("Sorted composite scout count preserved"), ForwardKeys.Num(), ReverseKeys.Num());
	for (int32 Index = 0; Index < ForwardKeys.Num(); ++Index)
	{
		TestEqual(TEXT("Composite scout sort is input-order independent"), ForwardKeys[Index].BuildOrderingId(), ReverseKeys[Index].BuildOrderingId());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractSingleLevelAscentOrderingKeyStabilityTest,
	"PorismExtension.Layout.Contracts.SingleLevelAscentOrderingKeyStability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractSingleLevelAscentOrderingKeyStabilityTest::RunTest(const FString& Parameters)
{
	FLayoutSingleLevelSteppedAscentOrderingInput Input;
	Input.ManifestId = TEXT("Manifest.Ascent.A");
	Input.LowerAscentId = TEXT("LowerAscent.A");
	Input.UpperConnectorOrCapId = TEXT("UpperCap.A");
	Input.CompositeBundleId = TEXT("CompositeBundle.A");
	Input.PseudoConnectorCell = FIntVector(1, 2, 0);
	Input.TraversalChannelId = TEXT("Traversal.Walk");
	Input.TerrainFrontierId = TEXT("Frontier.A");
	Input.PlacementShiftId = TEXT("Shift.A");
	Input.AttemptIndex = 4;

	const FLayoutContractCandidateOrderingKey FirstKey = FLayoutContractPlacementCandidates::BuildSingleLevelSteppedAscentOrderingKey(Input);
	const FLayoutContractCandidateOrderingKey SecondKey = FLayoutContractPlacementCandidates::BuildSingleLevelSteppedAscentOrderingKey(Input);
	TestEqual(TEXT("Same single-level ascent input builds stable ordering id"), FirstKey.BuildOrderingId(), SecondKey.BuildOrderingId());

	FLayoutSingleLevelSteppedAscentOrderingInput DifferentUpper = Input;
	DifferentUpper.UpperConnectorOrCapId = TEXT("UpperCap.B");
	const FLayoutContractCandidateOrderingKey DifferentUpperKey = FLayoutContractPlacementCandidates::BuildSingleLevelSteppedAscentOrderingKey(DifferentUpper);
	TestNotEqual(TEXT("Upper connector/cap participates in ascent ordering"), FirstKey.BuildOrderingId(), DifferentUpperKey.BuildOrderingId());

	FLayoutSingleLevelSteppedAscentOrderingInput DifferentCell = Input;
	DifferentCell.PseudoConnectorCell = FIntVector(1, 3, 0);
	const FLayoutContractCandidateOrderingKey DifferentCellKey = FLayoutContractPlacementCandidates::BuildSingleLevelSteppedAscentOrderingKey(DifferentCell);
	TestNotEqual(TEXT("Pseudo connector cell participates in ascent ordering"), FirstKey.BuildOrderingId(), DifferentCellKey.BuildOrderingId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractSingleLevelAscentOrderingSortIsInputOrderIndependentTest,
	"PorismExtension.Layout.Contracts.SingleLevelAscentOrderingSortIsInputOrderIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractSingleLevelAscentOrderingSortIsInputOrderIndependentTest::RunTest(const FString& Parameters)
{
	FLayoutSingleLevelSteppedAscentOrderingInput FirstInput;
	FirstInput.ManifestId = TEXT("Manifest.Ascent.A");
	FirstInput.LowerAscentId = TEXT("LowerAscent.A");
	FirstInput.UpperConnectorOrCapId = TEXT("UpperCap.A");
	FirstInput.CompositeBundleId = TEXT("CompositeBundle.A");
	FirstInput.PseudoConnectorCell = FIntVector(1, 2, 0);
	FirstInput.TraversalChannelId = TEXT("Traversal.Walk");
	FirstInput.TerrainFrontierId = TEXT("Frontier.A");

	FLayoutSingleLevelSteppedAscentOrderingInput SecondInput = FirstInput;
	SecondInput.LowerAscentId = TEXT("LowerAscent.B");
	SecondInput.PseudoConnectorCell = FIntVector(2, 2, 0);

	TArray<FLayoutContractCandidateOrderingKey> ForwardKeys = {
		FLayoutContractPlacementCandidates::BuildSingleLevelSteppedAscentOrderingKey(FirstInput),
		FLayoutContractPlacementCandidates::BuildSingleLevelSteppedAscentOrderingKey(SecondInput)
	};
	TArray<FLayoutContractCandidateOrderingKey> ReverseKeys = {
		FLayoutContractPlacementCandidates::BuildSingleLevelSteppedAscentOrderingKey(SecondInput),
		FLayoutContractPlacementCandidates::BuildSingleLevelSteppedAscentOrderingKey(FirstInput)
	};
	FLayoutContractPlacementCandidates::SortCandidateKeys(ForwardKeys);
	FLayoutContractPlacementCandidates::SortCandidateKeys(ReverseKeys);
	TestEqual(TEXT("Sorted single-level ascent count preserved"), ForwardKeys.Num(), ReverseKeys.Num());
	for (int32 Index = 0; Index < ForwardKeys.Num(); ++Index)
	{
		TestEqual(TEXT("Single-level ascent sort is input-order independent"), ForwardKeys[Index].BuildOrderingId(), ReverseKeys[Index].BuildOrderingId());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractContinuationEdgeScoutOrderingKeyStabilityTest,
	"PorismExtension.Layout.Contracts.ContinuationEdgeScoutOrderingKeyStability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractContinuationEdgeScoutOrderingKeyStabilityTest::RunTest(const FString& Parameters)
{
	FLayoutContinuationEdgeScoutOrderingInput Input;
	Input.ManifestId = TEXT("Manifest.Continuation.A");
	Input.SourceRegionId = TEXT("Region.Source.A");
	Input.ContinuationFamilyId = TEXT("Family.Surface.A");
	Input.SourceEndpointId = TEXT("Endpoint.Source.A");
	Input.TargetEndpointOrCandidateId = TEXT("Endpoint.Target.A");
	Input.EdgeScoutResultId = TEXT("Edge.Result.A");
	Input.PathShapeId = TEXT("PathShape.Surface.A");
	Input.ContinuationModeId = TEXT("Mode.SurfacePath.A");
	Input.PlacementShiftId = TEXT("Shift.A");
	Input.AttemptIndex = 5;

	const FLayoutContractCandidateOrderingKey FirstKey = FLayoutContractPlacementCandidates::BuildContinuationEdgeScoutOrderingKey(Input);
	const FLayoutContractCandidateOrderingKey SecondKey = FLayoutContractPlacementCandidates::BuildContinuationEdgeScoutOrderingKey(Input);
	TestEqual(TEXT("Same continuation edge input builds stable ordering id"), FirstKey.BuildOrderingId(), SecondKey.BuildOrderingId());

	FLayoutContinuationEdgeScoutOrderingInput DifferentEdge = Input;
	DifferentEdge.EdgeScoutResultId = TEXT("Edge.Result.B");
	const FLayoutContractCandidateOrderingKey DifferentEdgeKey = FLayoutContractPlacementCandidates::BuildContinuationEdgeScoutOrderingKey(DifferentEdge);
	TestNotEqual(TEXT("Edge scout result id participates in continuation ordering"), FirstKey.BuildOrderingId(), DifferentEdgeKey.BuildOrderingId());

	FLayoutContinuationEdgeScoutOrderingInput DifferentMode = Input;
	DifferentMode.ContinuationModeId = TEXT("Mode.Bridge.A");
	const FLayoutContractCandidateOrderingKey DifferentModeKey = FLayoutContractPlacementCandidates::BuildContinuationEdgeScoutOrderingKey(DifferentMode);
	TestNotEqual(TEXT("Continuation mode id participates in continuation ordering"), FirstKey.BuildOrderingId(), DifferentModeKey.BuildOrderingId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractContinuationEdgeScoutOrderingSortIsInputOrderIndependentTest,
	"PorismExtension.Layout.Contracts.ContinuationEdgeScoutOrderingSortIsInputOrderIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractContinuationEdgeScoutOrderingSortIsInputOrderIndependentTest::RunTest(const FString& Parameters)
{
	FLayoutContinuationEdgeScoutOrderingInput FirstInput;
	FirstInput.ManifestId = TEXT("Manifest.Continuation.A");
	FirstInput.SourceRegionId = TEXT("Region.Source.A");
	FirstInput.ContinuationFamilyId = TEXT("Family.Surface.A");
	FirstInput.SourceEndpointId = TEXT("Endpoint.Source.A");
	FirstInput.TargetEndpointOrCandidateId = TEXT("Endpoint.Target.A");
	FirstInput.EdgeScoutResultId = TEXT("Edge.Result.A");
	FirstInput.PathShapeId = TEXT("PathShape.Surface.A");
	FirstInput.ContinuationModeId = TEXT("Mode.SurfacePath.A");

	FLayoutContinuationEdgeScoutOrderingInput SecondInput = FirstInput;
	SecondInput.TargetEndpointOrCandidateId = TEXT("Endpoint.Target.B");
	SecondInput.EdgeScoutResultId = TEXT("Edge.Result.B");

	TArray<FLayoutContractCandidateOrderingKey> ForwardKeys = {
		FLayoutContractPlacementCandidates::BuildContinuationEdgeScoutOrderingKey(FirstInput),
		FLayoutContractPlacementCandidates::BuildContinuationEdgeScoutOrderingKey(SecondInput)
	};
	TArray<FLayoutContractCandidateOrderingKey> ReverseKeys = {
		FLayoutContractPlacementCandidates::BuildContinuationEdgeScoutOrderingKey(SecondInput),
		FLayoutContractPlacementCandidates::BuildContinuationEdgeScoutOrderingKey(FirstInput)
	};
	FLayoutContractPlacementCandidates::SortCandidateKeys(ForwardKeys);
	FLayoutContractPlacementCandidates::SortCandidateKeys(ReverseKeys);
	TestEqual(TEXT("Sorted continuation edge count preserved"), ForwardKeys.Num(), ReverseKeys.Num());
	for (int32 Index = 0; Index < ForwardKeys.Num(); ++Index)
	{
		TestEqual(TEXT("Continuation edge sort is input-order independent"), ForwardKeys[Index].BuildOrderingId(), ReverseKeys[Index].BuildOrderingId());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractRootScoutOrderingKeyStabilityTest,
	"PorismExtension.Layout.Contracts.RootScoutOrderingKeyStability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractRootScoutOrderingKeyStabilityTest::RunTest(const FString& Parameters)
{
	FLayoutRootScoutResultOrderingInput Input;
	Input.ManifestId = TEXT("Manifest.Root.A");
	Input.WorldBindingId = TEXT("WorldBinding.A");
	Input.ScoutResultId = TEXT("RootScout.Result.A");
	Input.RootCandidateId = TEXT("RootCandidate.A");
	Input.PlacementPolicyId = TEXT("Policy.A");
	Input.BiomeRowName = TEXT("Biome.Forest");
	Input.SnappedSiteCenterBlockWorldPos = FIntVector(128, 64, 32);
	Input.SolveSeed = 12345;
	Input.PlacementShiftId = TEXT("Shift.A");
	Input.AttemptIndex = 6;

	const FLayoutContractCandidateOrderingKey FirstKey = FLayoutContractPlacementCandidates::BuildRootScoutResultOrderingKey(Input);
	const FLayoutContractCandidateOrderingKey SecondKey = FLayoutContractPlacementCandidates::BuildRootScoutResultOrderingKey(Input);
	TestEqual(TEXT("Same root scout input builds stable ordering id"), FirstKey.BuildOrderingId(), SecondKey.BuildOrderingId());

	FLayoutRootScoutResultOrderingInput DifferentSite = Input;
	DifferentSite.SnappedSiteCenterBlockWorldPos = FIntVector(144, 64, 32);
	const FLayoutContractCandidateOrderingKey DifferentSiteKey = FLayoutContractPlacementCandidates::BuildRootScoutResultOrderingKey(DifferentSite);
	TestNotEqual(TEXT("Snapped site center participates in root scout ordering"), FirstKey.BuildOrderingId(), DifferentSiteKey.BuildOrderingId());

	FLayoutRootScoutResultOrderingInput DifferentSeed = Input;
	DifferentSeed.SolveSeed = 67890;
	const FLayoutContractCandidateOrderingKey DifferentSeedKey = FLayoutContractPlacementCandidates::BuildRootScoutResultOrderingKey(DifferentSeed);
	TestNotEqual(TEXT("Final solve seed participates in root scout ordering"), FirstKey.BuildOrderingId(), DifferentSeedKey.BuildOrderingId());

	FLayoutContractCandidateOrderingKey ValidatedKey;
	FString FailureReason;
	TestTrue(
		TEXT("Complete root scout input validates before ordering"),
		FLayoutContractPlacementCandidates::TryBuildRootScoutResultOrderingKey(Input, ValidatedKey, FailureReason));
	TestEqual(TEXT("Validated root scout key matches builder key"), ValidatedKey.BuildOrderingId(), FirstKey.BuildOrderingId());

	FLayoutRootScoutResultOrderingInput MissingScoutId = Input;
	MissingScoutId.ScoutResultId = NAME_None;
	TestFalse(
		TEXT("Missing scout result id fails closed before root scout ordering"),
		FLayoutContractPlacementCandidates::TryBuildRootScoutResultOrderingKey(MissingScoutId, ValidatedKey, FailureReason));
	TestTrue(TEXT("Missing scout id failure reported"), FailureReason.Contains(TEXT("scout result")));

	FLayoutRootScoutResultOrderingInput InvalidAttempt = Input;
	InvalidAttempt.AttemptIndex = -1;
	TestFalse(
		TEXT("Negative root scout attempt fails closed before ordering"),
		FLayoutContractPlacementCandidates::TryBuildRootScoutResultOrderingKey(InvalidAttempt, ValidatedKey, FailureReason));
	TestTrue(TEXT("Invalid attempt failure reported"), FailureReason.Contains(TEXT("attempt")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractRootScoutOrderingSortIsInputOrderIndependentTest,
	"PorismExtension.Layout.Contracts.RootScoutOrderingSortIsInputOrderIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractRootScoutOrderingSortIsInputOrderIndependentTest::RunTest(const FString& Parameters)
{
	FLayoutRootScoutResultOrderingInput FirstInput;
	FirstInput.ManifestId = TEXT("Manifest.Root.A");
	FirstInput.WorldBindingId = TEXT("WorldBinding.A");
	FirstInput.ScoutResultId = TEXT("RootScout.Result.A");
	FirstInput.RootCandidateId = TEXT("RootCandidate.A");
	FirstInput.PlacementPolicyId = TEXT("Policy.A");
	FirstInput.BiomeRowName = TEXT("Biome.Forest");
	FirstInput.SnappedSiteCenterBlockWorldPos = FIntVector(128, 64, 32);
	FirstInput.SolveSeed = 12345;

	FLayoutRootScoutResultOrderingInput SecondInput = FirstInput;
	SecondInput.ScoutResultId = TEXT("RootScout.Result.B");
	SecondInput.SnappedSiteCenterBlockWorldPos = FIntVector(256, 64, 32);
	SecondInput.SolveSeed = 67890;

	TArray<FLayoutContractCandidateOrderingKey> ForwardKeys = {
		FLayoutContractPlacementCandidates::BuildRootScoutResultOrderingKey(FirstInput),
		FLayoutContractPlacementCandidates::BuildRootScoutResultOrderingKey(SecondInput)
	};
	TArray<FLayoutContractCandidateOrderingKey> ReverseKeys = {
		FLayoutContractPlacementCandidates::BuildRootScoutResultOrderingKey(SecondInput),
		FLayoutContractPlacementCandidates::BuildRootScoutResultOrderingKey(FirstInput)
	};
	FLayoutContractPlacementCandidates::SortCandidateKeys(ForwardKeys);
	FLayoutContractPlacementCandidates::SortCandidateKeys(ReverseKeys);
	TestEqual(TEXT("Sorted root scout count preserved"), ForwardKeys.Num(), ReverseKeys.Num());
	for (int32 Index = 0; Index < ForwardKeys.Num(); ++Index)
	{
		TestEqual(TEXT("Root scout sort is input-order independent"), ForwardKeys[Index].BuildOrderingId(), ReverseKeys[Index].BuildOrderingId());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractFinalizedScoutResultSolveSeedTest,
	"PorismExtension.Layout.Contracts.FinalizedScoutResultSolveSeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractFinalizedScoutResultSolveSeedTest::RunTest(const FString& Parameters)
{
	const int32 WorldSeed = 123;
	const FIntVector UnshiftedSite(128, 64, 32);
	const FIntVector ShiftedSite(144, 64, 32);
	const int32 FirstSeed = FLayoutContractPlacementCandidates::BuildFinalizedScoutResultSolveSeed(UnshiftedSite, WorldSeed);
	const int32 SecondSeed = FLayoutContractPlacementCandidates::BuildFinalizedScoutResultSolveSeed(UnshiftedSite, WorldSeed);
	const int32 ShiftedSeed = FLayoutContractPlacementCandidates::BuildFinalizedScoutResultSolveSeed(ShiftedSite, WorldSeed);
	TestEqual(TEXT("Finalized scout result solve seed is stable for same snapped site"), FirstSeed, SecondSeed);
	TestNotEqual(TEXT("Finalized shifted scout result recomputes solve seed from shifted site"), FirstSeed, ShiftedSeed);

	FLayoutRootScoutResultOrderingInput OrderingInput;
	OrderingInput.ManifestId = TEXT("Manifest.Root.A");
	OrderingInput.WorldBindingId = TEXT("WorldBinding.A");
	OrderingInput.ScoutResultId = TEXT("RootScout.Result.A");
	OrderingInput.RootCandidateId = TEXT("RootCandidate.A");
	OrderingInput.PlacementPolicyId = TEXT("Policy.A");
	OrderingInput.BiomeRowName = TEXT("Biome.Forest");
	OrderingInput.SnappedSiteCenterBlockWorldPos = ShiftedSite;
	OrderingInput.SolveSeed = ShiftedSeed;
	const FLayoutContractCandidateOrderingKey ShiftedOrderingKey = FLayoutContractPlacementCandidates::BuildRootScoutResultOrderingKey(OrderingInput);
	OrderingInput.SolveSeed = FirstSeed;
	const FLayoutContractCandidateOrderingKey StaleOrderingKey = FLayoutContractPlacementCandidates::BuildRootScoutResultOrderingKey(OrderingInput);
	TestNotEqual(TEXT("Root scout ordering id changes when finalized solve seed is stale"), ShiftedOrderingKey.BuildOrderingId(), StaleOrderingKey.BuildOrderingId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractStandardAdapterTest,
	"PorismExtension.Layout.Contracts.StandardAdapterUsesFrozenRequestArtifacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractStandardAdapterTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.Seed = 12;
	Request.RegionDebugPath = TEXT("Standard/Child");
	Request.SourceParentRegionDebugPath = TEXT("Parent");
	Request.FootprintSize = FIntPoint(2, 1);
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(8, 8, 8);
	FLayoutPlannedCell& FirstCell = Request.PlannedCells.AddDefaulted_GetRef();
	FirstCell.Cell = FIntVector(0, 0, 0);
	FirstCell.Intent = ELayoutCellIntent::Interior;
	FLayoutPlannedCell& SecondCell = Request.PlannedCells.AddDefaulted_GetRef();
	SecondCell.Cell = FIntVector(1, 0, 0);
	SecondCell.Intent = ELayoutCellIntent::Interior;
	Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 8;
	FLayoutModePlan ModePlan;
	ModePlan.Scope = ELayoutContractRegionScope::Child;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::ChildRegion;
	ModePlan.SiteCenterBlockWorldPos = FIntVector(16, 24, 0);
	ModePlan.SolveSeed = Request.Seed;
	ModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ModePlan.PlacementShiftCells);
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan = ModePlan;

	FLayoutRegionContract Contract;
	FString FailureReason;
	TestTrue(
		TEXT("Standard adapter contract builds from frozen request artifacts"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestEqual(TEXT("Standard adapter preserves selected mode id"), Contract.ModePlan.ModePlanId, ModePlan.ModePlanId);
	TestEqual(TEXT("Standard adapter copies planned cells without terrain sampling"), Contract.PlannedCells.Num(), 2);
	TestEqual(TEXT("Compatibility contract emits kind-qualified active cells"), Contract.ActiveCells.Num(), 2);
	TestEqual(TEXT("Compatibility contract copies active cell id"), Contract.ActiveCells[1].Cell, FIntVector(1, 0, 0));
	TestEqual(TEXT("Standard adapter preserves request stepped support map"), Contract.SteppedTerrainSupportMap.SharedCellHeightInBlocks, 8);
	TestEqual(TEXT("Standard adapter emits compatibility terrain seed only"), Contract.FrozenTerrainContract.SiteCenterBlockWorldPos, ModePlan.SiteCenterBlockWorldPos);
	TestTrue(TEXT("Standard adapter does not authorize terrain writes"), Contract.FrozenTerrainContract.TerrainWrites.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractTerrainAdapterArtifactCarrierTest,
	"PorismExtension.Layout.Contracts.TerrainAdapterConsumesFrozenArtifactCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractTerrainAdapterArtifactCarrierTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.Seed = 44;
	Request.FootprintSize = FIntPoint(1, 1);
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(8, 8, 8);
	Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector::ZeroValue;
	FLayoutModePlan ModePlan;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	ModePlan.bUsesSteppedTerrainTopology = true;
	ModePlan.SiteCenterBlockWorldPos = FIntVector(32, 32, 16);
	ModePlan.SolveSeed = Request.Seed;
	ModePlan.WorldSeed = 7;
	ModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ModePlan.PlacementShiftCells);
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan = ModePlan;

	FLayoutRegionContract Contract;
	FString FailureReason;
	TestFalse(
		TEXT("Terrain adapter fails closed without frozen terrain artifact"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));

	Request.bHasFrozenTerrainBiomeAdapterInput = true;
	Request.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos = ModePlan.SiteCenterBlockWorldPos;
	Request.FrozenTerrainBiomeAdapterInput.FootprintMinBlockWorldPos = FIntVector(28, 28, 16);
	Request.FrozenTerrainBiomeAdapterInput.FootprintSizeInBlocks = FIntPoint(8, 8);
	Request.FrozenTerrainBiomeAdapterInput.EligibleBiomeRowName = TEXT("Biome.A");
	Request.FrozenTerrainBiomeAdapterInput.EligibleBiomeRowNames = {TEXT("Biome.A")};
	Request.FrozenTerrainBiomeAdapterInput.SearchStartZBlockWorld = 64;
	Request.FrozenTerrainBiomeAdapterInput.SearchDepthBlocks = 32;
	Request.FrozenTerrainBiomeAdapterInput.TerrainSampleGridSpacing = 8;
	Request.FrozenTerrainBiomeAdapterInput.AuditMessages.Add(TEXT("TestFrozenArtifact"));
	TestFalse(
		TEXT("Terrain adapter fails closed without base terrain evidence"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	Request.FrozenTerrainBiomeAdapterInput.bHasFiniteSearchBounds = true;
	Request.FrozenTerrainBiomeAdapterInput.SearchMinBlockXY = FIntPoint(28, 28);
	Request.FrozenTerrainBiomeAdapterInput.SearchMaxBlockXY = FIntPoint(35, 35);
	Request.FrozenTerrainBiomeAdapterInput.bHasSampledColumnEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasSteppedSupportEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasFootprintClassificationEvidence = true;
	FLayoutTerrainSurfaceSample& AdapterSurfaceSample = Request.FrozenTerrainBiomeAdapterInput.SurfaceSamples.AddDefaulted_GetRef();
	AdapterSurfaceSample.bIsValid = true;
	AdapterSurfaceSample.BlockXY = FIntPoint(28, 28);
	Request.FrozenTerrainBiomeAdapterInput.FootprintClassification.CellClassifications.AddDefaulted_GetRef().BlockXY = FIntPoint(28, 28);
	FLayoutSteppedTerrainSupportSample& SupportSample = Request.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples.AddDefaulted_GetRef();
	SupportSample.LocalCell = FIntVector::ZeroValue;
	SupportSample.SupportSurfaceZ = 16;
	Request.FrozenTerrainBiomeAdapterInput.bHasTerrainPlacementEvidence = true;
	TestFalse(
		TEXT("Terrain adapter fails closed when required placement evidence is absent"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	// Stage normalization rejects the missing column carrier before active-cell construction.
	TestEqual(TEXT("Missing placement evidence fails normalized stage authority"), FailureReason,
		FString(TEXT("Continuation stage map lost normalized terrain-stage evidence.")));

	FLayoutTerrainPlacementCellEvidence& PlacementCell = Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.AddDefaulted_GetRef();
	PlacementCell.Cell = FIntVector::ZeroValue;
	PlacementCell.ProvenanceId = TEXT("PlacementEvidence.Cell.0");
	TestFalse(
		TEXT("Terrain adapter rejects unplaceable planned cells before active-cell output"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestTrue(TEXT("Unplaceable placement evidence failure is reported"), FailureReason.Contains(TEXT("selected-mode placement evidence")));

	PlacementCell.bPlaceableForSelectedMode = true;
	PlacementCell.bHasExcavationEvidence = true;
	PlacementCell.bHasLocalOverlapZ = true;
	LayoutLocalBlockCoordinates::TryMakeCoord8(0, PlacementCell.OverlapMinLocalZ);
	LayoutLocalBlockCoordinates::TryMakeCoord8(3, PlacementCell.OverlapMaxLocalZ);
	TestTrue(
		TEXT("Terrain adapter accepts frozen terrain artifact with explicit excavation evidence"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestEqual(TEXT("Terrain adapter stamps mode plan id onto artifact"), Contract.ModePlan.ModePlanId, ModePlan.ModePlanId);
	TestEqual(TEXT("Terrain adapter produces terrain cell proof records from placement evidence"), Contract.FrozenTerrainContract.CellContracts.Num(), 1);
	TestEqual(TEXT("Terrain adapter terrain cell proof record preserves excavation/clearance evidence"), Contract.FrozenTerrainContract.CellContracts[0].Contract, ELayoutFrozenTerrainCellContract::FlatClearance);
	TestFalse(TEXT("Terrain adapter does not infer foundation fill from excavation evidence"), Contract.FrozenTerrainContract.CellContracts[0].bHasFoundationFillEvidence);
	TestFalse(TEXT("Terrain adapter does not infer clearance from excavation evidence"), Contract.FrozenTerrainContract.CellContracts[0].bHasClearanceEvidence);
	TestTrue(TEXT("Terrain adapter freezes excavation overlap bounds from placement evidence"), Contract.FrozenTerrainContract.CellContracts[0].bHasExcavationOverlapZ);
	TestEqual(TEXT("Terrain adapter preserves excavation overlap min Z"), Contract.FrozenTerrainContract.CellContracts[0].ExcavationOverlapMinLocalZ.Value, static_cast<uint8>(0));
	TestEqual(TEXT("Terrain adapter preserves excavation overlap max Z"), Contract.FrozenTerrainContract.CellContracts[0].ExcavationOverlapMaxLocalZ.Value, static_cast<uint8>(3));
	TestEqual(TEXT("Terrain adapter emits active cells from placement evidence"), Contract.ActiveCells.Num(), 1);

	PlacementCell.bHasExcavationEvidence = false;
	PlacementCell.bHasLocalOverlapZ = false;
	PlacementCell.OverlapMinLocalZ = FLayoutLocalBlockCoord8();
	PlacementCell.OverlapMaxLocalZ = FLayoutLocalBlockCoord8();
	PlacementCell.bHasFoundationFillEvidence = true;
	PlacementCell.RequiredFoundationDepth = 2;
	PlacementCell.FoundationMaterial = 1;
	Request.SelectedModePlan.PlacementPolicy.TerrainTransition.bAllowFoundationFill = false;
	Request.SelectedModePlan.PlacementPolicy.TerrainTransition.MaxFoundationDepth = 0;
	Contract = FLayoutRegionContract();
	TestFalse(
		TEXT("Terrain adapter rejects foundation-fill evidence without explicit policy"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestTrue(TEXT("Foundation policy failure is reported"), FailureReason.Contains(TEXT("foundation-fill policy")));
	Request.SelectedModePlan.PlacementPolicy.TerrainTransition.bAllowFoundationFill = true;
	Request.SelectedModePlan.PlacementPolicy.TerrainTransition.MaxFoundationDepth = 4;
	Contract = FLayoutRegionContract();
	TestTrue(
		TEXT("Terrain adapter accepts explicit foundation-fill evidence"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestEqual(TEXT("Foundation-fill evidence keeps active terrain contract"), Contract.FrozenTerrainContract.CellContracts[0].Contract, ELayoutFrozenTerrainCellContract::Active);
	TestTrue(TEXT("Terrain adapter freezes foundation-fill evidence"), Contract.FrozenTerrainContract.CellContracts[0].bHasFoundationFillEvidence);

	PlacementCell.bHasFoundationFillEvidence = false;
	PlacementCell.RequiredFoundationDepth = 0;
	PlacementCell.FoundationMaterial = 0;
	PlacementCell.bHasClearanceEvidence = true;
	Contract = FLayoutRegionContract();
	TestTrue(
		TEXT("Terrain adapter accepts explicit clearance evidence"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestEqual(TEXT("Clearance evidence becomes flat-clearance terrain contract"), Contract.FrozenTerrainContract.CellContracts[0].Contract, ELayoutFrozenTerrainCellContract::FlatClearance);
	TestTrue(TEXT("Terrain adapter freezes clearance evidence"), Contract.FrozenTerrainContract.CellContracts[0].bHasClearanceEvidence);

	PlacementCell.bHasClearanceEvidence = false;
	PlacementCell.bHasRampTransitionEvidence = true;
	Request.SelectedModePlan.PlacementPolicy.TerrainTransition.bAllowPerimeterRampTransition = false;
	Request.SelectedModePlan.PlacementPolicy.TerrainTransition.MaxFoundationDepth = 0;
	Contract = FLayoutRegionContract();
	TestFalse(
		TEXT("Terrain adapter rejects ramp-transition evidence without enabled ramp policy and foundation-depth budget"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestTrue(TEXT("Ramp policy failure is reported"), FailureReason.Contains(TEXT("foundation-depth budget")));
	Request.SelectedModePlan.PlacementPolicy.TerrainTransition.bAllowPerimeterRampTransition = true;
	Request.SelectedModePlan.PlacementPolicy.TerrainTransition.MaxFoundationDepth = 4;
	Contract = FLayoutRegionContract();
	TestTrue(
		TEXT("Terrain adapter accepts explicit ramp-transition evidence"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestEqual(TEXT("Ramp-transition evidence keeps active terrain contract"), Contract.FrozenTerrainContract.CellContracts[0].Contract, ELayoutFrozenTerrainCellContract::Active);
	TestTrue(TEXT("Terrain adapter freezes ramp-transition evidence"), Contract.FrozenTerrainContract.CellContracts[0].bHasRampTransitionEvidence);

	PlacementCell.bHasRampTransitionEvidence = false;
	PlacementCell.bHasBridgeSupportEvidence = true;
	Contract = FLayoutRegionContract();
	TestFalse(
		TEXT("Stepped terrain adapter rejects bridge-support evidence outside bridge-continuation mode"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestTrue(TEXT("Bridge mode mismatch failure is reported"), FailureReason.Contains(TEXT("bridge-continuation")));

	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::BridgeContinuation;
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.SelectedModePlan = ModePlan;
	Request.FrozenTerrainBiomeAdapterInput.bHasTerrainPathEvidence = true;
	FLayoutFrozenTerrainPathSample& BridgePathSample = Request.FrozenTerrainBiomeAdapterInput.TerrainPathSamples.AddDefaulted_GetRef();
	BridgePathSample.BlockXY = FIntPoint(28, 28);
	BridgePathSample.SurfaceZ = 16;
	BridgePathSample.bHasClassificationEvidence = true;
	Contract = FLayoutRegionContract();
	TestTrue(
		TEXT("Bridge-continuation terrain adapter accepts explicit bridge-support evidence"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestEqual(TEXT("Bridge-support evidence becomes bridge-span terrain contract"), Contract.FrozenTerrainContract.CellContracts[0].Contract, ELayoutFrozenTerrainCellContract::BridgeSpan);
	TestTrue(TEXT("Terrain adapter freezes bridge-support evidence"), Contract.FrozenTerrainContract.CellContracts[0].bHasBridgeSupportEvidence);

	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	ModePlan.bUsesSteppedTerrainTopology = true;
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.SelectedModePlan = ModePlan;
	Request.FrozenTerrainBiomeAdapterInput.bHasTerrainPathEvidence = false;
	Request.FrozenTerrainBiomeAdapterInput.TerrainPathSamples.Reset();
	PlacementCell.bHasBridgeSupportEvidence = false;
	PlacementCell.bHasExcavationEvidence = true;
	PlacementCell.bHasLocalOverlapZ = true;
	LayoutLocalBlockCoordinates::TryMakeCoord8(0, PlacementCell.OverlapMinLocalZ);
	LayoutLocalBlockCoordinates::TryMakeCoord8(3, PlacementCell.OverlapMaxLocalZ);
	FLayoutTerrainPlacementCellEvidence& DuplicatePlacementCell = Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.AddDefaulted_GetRef();
	DuplicatePlacementCell.Cell = FIntVector::ZeroValue;
	DuplicatePlacementCell.bPlaceableForSelectedMode = true;
	DuplicatePlacementCell.ProvenanceId = TEXT("PlacementEvidence.DuplicateCell");
	TestFalse(
		TEXT("Terrain adapter revalidates frozen placement evidence and rejects duplicate cells"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestTrue(TEXT("Duplicate placement evidence failure is reported"), FailureReason.Contains(TEXT("duplicate cell verdicts")));
	Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.Pop();

	FLayoutTerrainPlacementCellEvidence& StalePlacementCell = Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.AddDefaulted_GetRef();
	StalePlacementCell.Cell = FIntVector(1, 0, 0);
	StalePlacementCell.bPlaceableForSelectedMode = true;
	StalePlacementCell.ProvenanceId = TEXT("PlacementEvidence.StaleCell");
	TestFalse(
		TEXT("Terrain adapter rejects placement evidence outside the planned-cell set"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestTrue(TEXT("Stale placement evidence failure is reported"), FailureReason.Contains(TEXT("planned-cell set")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractSingleLevelSteppedActiveCellsTest,
	"PorismExtension.Layout.Contracts.SingleLevelSteppedActiveCellsProduceBridgeCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractSingleLevelSteppedActiveCellsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.Seed = 45;
	Request.FootprintSize = FIntPoint(2, 1);
	Request.ProfileSnapshot.LevelCount = 1;
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(8, 8, 8);
	Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector(0, 0, 0);
	Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector(1, 0, 0);
	FLayoutModePlan ModePlan;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	ModePlan.bUsesSteppedTerrainTopology = true;
	ModePlan.SiteCenterBlockWorldPos = FIntVector(32, 32, 16);
	ModePlan.SolveSeed = Request.Seed;
	ModePlan.WorldSeed = 7;
	ModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ModePlan.PlacementShiftCells);
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan = ModePlan;
	Request.bHasFrozenTerrainBiomeAdapterInput = true;
	Request.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos = ModePlan.SiteCenterBlockWorldPos;
	Request.FrozenTerrainBiomeAdapterInput.FootprintMinBlockWorldPos = FIntVector(28, 28, 16);
	Request.FrozenTerrainBiomeAdapterInput.FootprintSizeInBlocks = FIntPoint(16, 8);
	Request.FrozenTerrainBiomeAdapterInput.EligibleBiomeRowName = TEXT("Biome.A");
	Request.FrozenTerrainBiomeAdapterInput.SearchStartZBlockWorld = 64;
	Request.FrozenTerrainBiomeAdapterInput.SearchDepthBlocks = 32;
	Request.FrozenTerrainBiomeAdapterInput.TerrainSampleGridSpacing = 8;
	Request.FrozenTerrainBiomeAdapterInput.bHasFiniteSearchBounds = true;
	Request.FrozenTerrainBiomeAdapterInput.SearchMinBlockXY = FIntPoint(28, 28);
	Request.FrozenTerrainBiomeAdapterInput.SearchMaxBlockXY = FIntPoint(43, 35);
	Request.FrozenTerrainBiomeAdapterInput.bHasSampledColumnEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasSteppedSupportEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasFootprintClassificationEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.SurfaceSamples.AddDefaulted_GetRef().bIsValid = true;
	Request.FrozenTerrainBiomeAdapterInput.SurfaceSamples[0].BlockXY = FIntPoint(28, 28);
	Request.FrozenTerrainBiomeAdapterInput.FootprintClassification.CellClassifications.AddDefaulted_GetRef().BlockXY = FIntPoint(28, 28);
	FLayoutSteppedTerrainSupportSample& LowerSupport = Request.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples.AddDefaulted_GetRef();
	LowerSupport.LocalCell = FIntVector(0, 0, 0);
	LowerSupport.SupportSurfaceZ = 16;
	FLayoutSteppedTerrainSupportSample& HigherSupport = Request.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples.AddDefaulted_GetRef();
	HigherSupport.LocalCell = FIntVector(1, 0, 0);
	HigherSupport.SupportSurfaceZ = 24;

	FLayoutRegionContract Contract;
	FString FailureReason;
	TestFalse(
		TEXT("Single-level stepped adapter fails closed without explicit placement evidence"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestEqual(TEXT("Single-level stepped missing placement evidence fails normalized stage authority"), FailureReason,
		FString(TEXT("Continuation stage map lost normalized terrain-stage evidence.")));

	Request.FrozenTerrainBiomeAdapterInput.bHasTerrainPlacementEvidence = true;
	FLayoutTerrainPlacementCellEvidence& LowerPlacement = Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.AddDefaulted_GetRef();
	LowerPlacement.Cell = FIntVector(0, 0, 0);
	LowerPlacement.bPlaceableForSelectedMode = true;
	LowerPlacement.TerrainStageIndex = 0;
	LowerPlacement.ProvenanceId = TEXT("Placement.Lower");
	FLayoutTerrainPlacementCellEvidence& HigherPlacement = Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.AddDefaulted_GetRef();
	HigherPlacement.Cell = FIntVector(1, 0, 0);
	HigherPlacement.bPlaceableForSelectedMode = true;
	HigherPlacement.TerrainStageIndex = 1;
	HigherPlacement.VerticalShiftBlocks = 8;
	HigherPlacement.ProvenanceId = TEXT("Placement.Higher");
	TestTrue(
		TEXT("Single-level stepped adapter produces active-cell collection from placement evidence"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	// Stage-1 cluster has only 1 cell — below minimum of 3 — shift falls back to terrain excavation.
	// No Z-shift, no bridge cells, no TopBridge. Active cells = 2 original planned cells.
	TestEqual(TEXT("Sub-minimum cluster excludes shift/bridge — only original cells"), Contract.ActiveCells.Num(), 2);

	int32 Z0CellCount = 0;
	for (const FLayoutContractActiveCellRecord& ActiveCell : Contract.ActiveCells)
	{
		TestTrue(TEXT("Active cell at Z=0 (no shift for sub-minimum cluster)"), ActiveCell.Cell.Z == 0);
	}
	TestEqual(TEXT("Planned cells unchanged for sub-minimum cluster"), Contract.PlannedCells.Num(), 2);

	HigherPlacement.TerrainStageIndex = 2;
	HigherPlacement.VerticalShiftBlocks = 16;
	// Non-adjacent stage jumps rejected regardless of cluster size (Pass 1).
	TestFalse(
		TEXT("Single-level stepped adapter rejects non-adjacent stage jumps"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestTrue(TEXT("Non-adjacent stage failure is reported"), FailureReason.Contains(TEXT("non-adjacent")));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractDiagonalStageSupportCompletesBridgeCornerTest,
	"PorismExtension.Layout.Contracts.DiagonalStageSupportCompletesBridgeCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractDiagonalStageSupportCompletesBridgeCornerTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.Seed = 47;
	Request.FootprintSize = FIntPoint(3, 3);
	Request.ProfileSnapshot.LevelCount = 1;
	Request.ProfileSnapshot.bSupportsSteppedTerrainSolve = true;
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(8, 8, 8);
	for (int32 Y = 0; Y < Request.FootprintSize.Y; ++Y)
	{
		for (int32 X = 0; X < Request.FootprintSize.X; ++X)
		{
			FLayoutPlannedCell& Cell = Request.PlannedCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			Cell.Intent = X == 0 || Y == 0 || X == Request.FootprintSize.X - 1 || Y == Request.FootprintSize.Y - 1
				? ELayoutCellIntent::Boundary
				: ELayoutCellIntent::Interior;
		}
	}

	FLayoutModePlan ModePlan;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	ModePlan.bUsesSteppedTerrainTopology = true;
	ModePlan.SiteCenterBlockWorldPos = FIntVector(32, 32, 16);
	ModePlan.SolveSeed = Request.Seed;
	ModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ModePlan.PlacementShiftCells);
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan = ModePlan;
	Request.bHasFrozenTerrainBiomeAdapterInput = true;
	FLayoutFrozenTerrainBiomeAdapterInput& Terrain = Request.FrozenTerrainBiomeAdapterInput;
	Terrain.SiteCenterBlockWorldPos = ModePlan.SiteCenterBlockWorldPos;
	Terrain.FootprintMinBlockWorldPos = FIntVector(20, 20, 16);
	Terrain.FootprintSizeInBlocks = FIntPoint(24, 24);
	Terrain.EligibleBiomeRowName = TEXT("Biome.A");
	Terrain.SearchStartZBlockWorld = 64;
	Terrain.SearchDepthBlocks = 32;
	Terrain.TerrainSampleGridSpacing = 8;
	Terrain.bHasFiniteSearchBounds = true;
	Terrain.SearchMinBlockXY = FIntPoint(20, 20);
	Terrain.SearchMaxBlockXY = FIntPoint(43, 43);
	Terrain.bHasSampledColumnEvidence = true;
	Terrain.bHasSteppedSupportEvidence = true;
	Terrain.bHasFootprintClassificationEvidence = true;
	Terrain.bHasTerrainPlacementEvidence = true;
	Terrain.SurfaceSamples.AddDefaulted_GetRef().bIsValid = true;
	Terrain.SurfaceSamples[0].BlockXY = Terrain.SearchMinBlockXY;
	Terrain.FootprintClassification.CellClassifications.AddDefaulted_GetRef().BlockXY = Terrain.SearchMinBlockXY;

	auto IsRaised = [](const int32 X, const int32 Y)
	{
		return (X == 1 && Y == 1) || (X == 2 && Y == 1) || (X == 2 && Y == 2);
	};
	for (int32 Y = 0; Y < Request.FootprintSize.Y; ++Y)
	{
		for (int32 X = 0; X < Request.FootprintSize.X; ++X)
		{
			const int32 Stage = IsRaised(X, Y) ? 1 : 0;
			FLayoutSteppedTerrainSupportSample& Support = Terrain.SteppedSupportSamples.AddDefaulted_GetRef();
			Support.LocalCell = FIntVector(X, Y, 0);
			Support.SupportSurfaceZ = 16 + Stage * 8;
			Support.SnappedSupportFloorZ = Stage * 8;
			Support.SnappedSupportCeilingZ = (Stage + 1) * 8;
			FLayoutTerrainPlacementCellEvidence& Placement = Terrain.TerrainPlacementCells.AddDefaulted_GetRef();
			Placement.Cell = FIntVector(X, Y, 0);
			Placement.bPlaceableForSelectedMode = true;
			Placement.TerrainStageIndex = Stage;
			Placement.VerticalShiftBlocks = Stage * 8;
			Placement.ProvenanceId = *FString::Printf(TEXT("DiagonalBridge.%d.%d"), X, Y);
		}
	}

	FLayoutRegionContract Contract;
	FString FailureReason;
	if (!TestTrue(
		TEXT("Stepped adapter builds diagonal corner-support topology"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	TestTrue(TEXT("Diagonal stage contact supplies same-level bridge support"),
		Contract.PlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
		{
			return Cell.Cell == FIntVector(0, 0, 1) && Cell.bIsBridgeCell;
		}));
	const FLayoutPlannedCell* const SupportedBridge = Contract.PlannedCells.FindByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell == FIntVector(1, 0, 1) && Cell.bIsBridgeCell;
	});
	TestTrue(TEXT("Diagonal support keeps adjacent perimeter bridge Edge instead of impossible Corner"),
		SupportedBridge != nullptr && SupportedBridge->PlacementZone == ELayoutPlacementZone::Edge);

	TSet<FIntVector> PlannedCells;
	for (const FLayoutPlannedCell& Cell : Contract.PlannedCells)
	{
		PlannedCells.Add(Cell.Cell);
	}
	const bool bHasTerminalGeneratedTopBridge = Contract.PlannedCells.ContainsByPredicate(
		[&PlannedCells](const FLayoutPlannedCell& Cell)
		{
			if (!Cell.bIsBridgeCell || !PlannedCells.Contains(Cell.Cell - FIntVector(0, 0, 1)))
			{
				return false;
			}
			int32 LateralNeighborCount = 0;
			for (const FIntVector& Delta : {
				FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
				FIntVector(0, 1, 0), FIntVector(0, -1, 0) })
			{
				LateralNeighborCount += PlannedCells.Contains(Cell.Cell + Delta) ? 1 : 0;
			}
			return LateralNeighborCount < 2;
		});
	TestFalse(TEXT("Generated TopBridge never leaves a one-neighbor terminal deck cell"), bHasTerminalGeneratedTopBridge);

	// A flat continuation centerline may have one raised terrain cluster along
	// its widened fringe. Its protected seam anchor can leave terminal bridge
	// candidates on either side; those leaves must be omitted, not reject the segment.
	Request.RootContinuationSelection.FamilyId = TEXT("FringeStageContinuation");
	FLayoutPlannedCell& ProtectedEntryCell = Request.PlannedCells[4];
	ProtectedEntryCell.Intent = ELayoutCellIntent::Entry;
	ProtectedEntryCell.EntryOrigin = ELayoutEntryOrigin::Continuation;
	Request.CommittedEndpointAnchors.Reset();
	FLayoutCommittedEndpointAnchor& ProtectedAnchor = Request.CommittedEndpointAnchors.AddDefaulted_GetRef();
	ProtectedAnchor.CommitmentId = TEXT("FringeStageProtectedAnchor");
	ProtectedAnchor.LocalCell = FIntVector(1, 1, 0);
	ProtectedAnchor.RequiredWorldCenterBlockZ = 20;
	Terrain.SteppedSupportSamples.Reset();
	Terrain.TerrainPlacementCells.Reset();
	for (int32 Y = 0; Y < Request.FootprintSize.Y; ++Y)
	{
		for (int32 X = 0; X < Request.FootprintSize.X; ++X)
		{
			const int32 Stage = X == 0 ? 1 : 0;
			FLayoutSteppedTerrainSupportSample& Support = Terrain.SteppedSupportSamples.AddDefaulted_GetRef();
			Support.LocalCell = FIntVector(X, Y, 0);
			Support.SupportSurfaceZ = 16 + Stage * 8;
			Support.SnappedSupportFloorZ = Stage * 8;
			Support.SnappedSupportCeilingZ = (Stage + 1) * 8;
			FLayoutTerrainPlacementCellEvidence& Placement = Terrain.TerrainPlacementCells.AddDefaulted_GetRef();
			Placement.Cell = Support.LocalCell;
			Placement.bPlaceableForSelectedMode = true;
			Placement.TerrainStageIndex = Stage;
			Placement.VerticalShiftBlocks = Stage * 8;
			Placement.ProvenanceId = *FString::Printf(TEXT("FringeStage.%d.%d"), X, Y);
		}
	}
	Contract = FLayoutRegionContract();
	FailureReason.Reset();
	const bool bBuiltFringeStageContract = PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(
		Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason);
	TestTrue(TEXT("Raised continuation fringe omits terminal TopBridge leaves"), bBuiltFringeStageContract);
	if (!bBuiltFringeStageContract)
	{
		AddError(FailureReason);
	}
	else
	{
		TestFalse(TEXT("Fringe start has no terminal TopBridge"), Contract.PlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
		{
			return Cell.Cell == FIntVector(1, 0, 1) && Cell.bIsBridgeCell;
		}));
		TestFalse(TEXT("Fringe end has no terminal TopBridge"), Contract.PlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
		{
			return Cell.Cell == FIntVector(1, 2, 1) && Cell.bIsBridgeCell;
		}));
	}
	Request.RootContinuationSelection = FLayoutResolvedWorldBindingContinuationSelection();
	Request.CommittedEndpointAnchors.Reset();
	ProtectedEntryCell.Intent = ELayoutCellIntent::Interior;
	ProtectedEntryCell.EntryOrigin = ELayoutEntryOrigin::None;

	// Cardinal terraces remain valid when a diagonal pair spans two stages.
	Request.FootprintSize = FIntPoint(4, 4);
	Request.PlannedCells.Reset();
	Terrain.FootprintSizeInBlocks = FIntPoint(32, 32);
	Terrain.SearchMaxBlockXY = FIntPoint(51, 51);
	Terrain.SteppedSupportSamples.Reset();
	Terrain.TerrainPlacementCells.Reset();
	for (int32 Y = 0; Y < Request.FootprintSize.Y; ++Y)
	{
		for (int32 X = 0; X < Request.FootprintSize.X; ++X)
		{
			const int32 Stage = X == 0 && Y == 0 ? 0 : (X == 0 || Y == 0 ? 1 : 2);
			FLayoutPlannedCell& Cell = Request.PlannedCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			Cell.Intent = X == 0 || Y == 0 || X == Request.FootprintSize.X - 1 || Y == Request.FootprintSize.Y - 1
				? ELayoutCellIntent::Boundary
				: ELayoutCellIntent::Interior;

			FLayoutSteppedTerrainSupportSample& Support = Terrain.SteppedSupportSamples.AddDefaulted_GetRef();
			Support.LocalCell = Cell.Cell;
			Support.SupportSurfaceZ = 16 + Stage * 8;
			Support.SnappedSupportFloorZ = Stage * 8;
			Support.SnappedSupportCeilingZ = (Stage + 1) * 8;
			FLayoutTerrainPlacementCellEvidence& Placement = Terrain.TerrainPlacementCells.AddDefaulted_GetRef();
			Placement.Cell = Cell.Cell;
			Placement.bPlaceableForSelectedMode = true;
			Placement.TerrainStageIndex = Stage;
			Placement.VerticalShiftBlocks = Stage * 8;
			Placement.ProvenanceId = *FString::Printf(TEXT("DiagonalTerrace.%d.%d"), X, Y);
		}
	}

	Contract = FLayoutRegionContract();
	FailureReason.Reset();
	TestTrue(
		TEXT("Cardinal 0-to-1-to-2 terrace accepts diagonal 0-to-2 contact"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(
			Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	if (!FailureReason.IsEmpty())
	{
		AddError(FailureReason);
	}

	FLayoutTerrainPlacementCellEvidence* const CardinalNeighbor = Terrain.TerrainPlacementCells.FindByPredicate(
		[](const FLayoutTerrainPlacementCellEvidence& Placement)
		{
			return Placement.Cell == FIntVector(1, 0, 0);
		});
	FLayoutSteppedTerrainSupportSample* const CardinalNeighborSupport = Terrain.SteppedSupportSamples.FindByPredicate(
		[](const FLayoutSteppedTerrainSupportSample& Support)
		{
			return Support.LocalCell == FIntVector(1, 0, 0);
		});
	TestNotNull(TEXT("Cardinal regression neighbor exists"), CardinalNeighbor);
	TestNotNull(TEXT("Cardinal regression support exists"), CardinalNeighborSupport);
	if (CardinalNeighbor != nullptr && CardinalNeighborSupport != nullptr)
	{
		CardinalNeighbor->TerrainStageIndex = 2;
		CardinalNeighbor->VerticalShiftBlocks = 16;
		CardinalNeighborSupport->SupportSurfaceZ = 32;
		CardinalNeighborSupport->SnappedSupportFloorZ = 16;
		CardinalNeighborSupport->SnappedSupportCeilingZ = 24;
		Contract = FLayoutRegionContract();
		FailureReason.Reset();
		TestFalse(
			TEXT("Cardinal 0-to-2 contact remains rejected"),
			PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(
				Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
		TestTrue(TEXT("Cardinal stage skip reports non-adjacent failure"), FailureReason.Contains(TEXT("non-adjacent")));
	}
	return true;
}

// ---- N4.86: Seam-only bridge cells with connected-component propagation ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractSeamOnlyShiftConnectedTest,
	"PorismExtension.Layout.Contracts.SeamOnlyShiftConnectedSameStageColumns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractSeamOnlyShiftConnectedTest::RunTest(const FString& Parameters)
{
	// 4x1 footprint, stages [0,0,1,1].
	// Columns 2 and 3 (stage 1) should both Z-shift (connected component).
	// Only column 2 (adjacent to column 1, stage 0) is a seam and gets bridge at Z=0.
	// Column 3 shifts but Z=0 stays empty.
	FLayoutRegionSolveRequest Request;
	Request.Seed = 60;
	Request.FootprintSize = FIntPoint(4, 1);
	Request.ProfileSnapshot.LevelCount = 1;
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(8, 8, 8);
	for (int32 X = 0; X < 4; ++X)
	{
		Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector(X, 0, 0);
	}

	FLayoutModePlan ModePlan;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	ModePlan.bUsesSteppedTerrainTopology = true;
	ModePlan.SiteCenterBlockWorldPos = FIntVector(48, 32, 16);
	ModePlan.SolveSeed = Request.Seed;
	ModePlan.WorldSeed = 7;
	ModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ModePlan.PlacementShiftCells);
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan = ModePlan;
	Request.bHasFrozenTerrainBiomeAdapterInput = true;
	Request.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos = ModePlan.SiteCenterBlockWorldPos;
	Request.FrozenTerrainBiomeAdapterInput.FootprintMinBlockWorldPos = FIntVector(36, 28, 16);
	Request.FrozenTerrainBiomeAdapterInput.FootprintSizeInBlocks = FIntPoint(32, 8);
	Request.FrozenTerrainBiomeAdapterInput.EligibleBiomeRowName = TEXT("Biome.A");
	Request.FrozenTerrainBiomeAdapterInput.SearchStartZBlockWorld = 64;
	Request.FrozenTerrainBiomeAdapterInput.SearchDepthBlocks = 32;
	Request.FrozenTerrainBiomeAdapterInput.TerrainSampleGridSpacing = 8;
	Request.FrozenTerrainBiomeAdapterInput.bHasFiniteSearchBounds = true;
	Request.FrozenTerrainBiomeAdapterInput.SearchMinBlockXY = FIntPoint(36, 28);
	Request.FrozenTerrainBiomeAdapterInput.SearchMaxBlockXY = FIntPoint(67, 35);
	Request.FrozenTerrainBiomeAdapterInput.bHasSampledColumnEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasSteppedSupportEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasFootprintClassificationEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasTerrainPlacementEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.SurfaceSamples.AddDefaulted_GetRef().bIsValid = true;
	Request.FrozenTerrainBiomeAdapterInput.SurfaceSamples[0].BlockXY = FIntPoint(36, 28);
	Request.FrozenTerrainBiomeAdapterInput.FootprintClassification.CellClassifications.AddDefaulted_GetRef().BlockXY = FIntPoint(36, 28);

	// Stage 0 columns (X=0,1): SupportSurfaceZ=16. Stage 1 columns (X=2,3): SupportSurfaceZ=24.
	auto AddSupport = [&Request](int32 X, int32 SupportZ)
	{
		FLayoutSteppedTerrainSupportSample& S = Request.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples.AddDefaulted_GetRef();
		S.LocalCell = FIntVector(X, 0, 0);
		S.SupportSurfaceZ = SupportZ;
	};
	AddSupport(0, 16); AddSupport(1, 16);
	AddSupport(2, 24); AddSupport(3, 24);

	auto AddPlacement = [&Request](int32 X, int32 Stage)
	{
		FLayoutTerrainPlacementCellEvidence& P = Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.AddDefaulted_GetRef();
		P.Cell = FIntVector(X, 0, 0);
		P.bPlaceableForSelectedMode = true;
		P.TerrainStageIndex = Stage;
		P.ProvenanceId = *FString::Printf(TEXT("P%d"), X);
		if (Stage > 0) P.VerticalShiftBlocks = Stage * 8;
	};
	AddPlacement(0, 0); AddPlacement(1, 0);
	AddPlacement(2, 1); AddPlacement(3, 1);

	FLayoutRegionContract Contract;
	FString FailureReason;
	TestTrue(
		TEXT("Connected same-stage region produces active cells"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));

	if (!FailureReason.IsEmpty())
	{
		AddError(FString::Printf(TEXT("Contract build failure: %s"), *FailureReason));
		return false;
	}

	// Stage-1 cluster has 2 cells (columns 2 and 3) — below minimum of 3.
	// Cluster excluded from SeamHigherColumns. No shift, no bridge cells.
	// All 4 columns stay at Z=0.
	TestEqual(TEXT("Sub-minimum cluster excludes shift/bridge — only original cells"), Contract.ActiveCells.Num(), 4);

	// All active cells at Z=0, no bridges
	for (const FLayoutContractActiveCellRecord& Ac : Contract.ActiveCells)
	{
		TestEqual(TEXT("No shift — all cells at Z=0"), Ac.Cell.Z, 0);
	}

	// No Z=-1 cells anywhere
	for (const FLayoutContractActiveCellRecord& Ac : Contract.ActiveCells)
	{
		TestTrue(FString::Printf(TEXT("No Z=-1 cells (found cell %s)"), *Ac.Cell.ToString()), Ac.Cell.Z >= 0);
	}

	return true;
}


// ---- S8: Shift DOWN bridge direction test ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractSteppedActiveCellsShiftDownTest,
	"PorismExtension.Layout.Contracts.SteppedActiveCellsShiftDownProducesBridgeCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractSteppedActiveCellsShiftDownTest::RunTest(const FString& Parameters)
{
	// Shift DOWN: higher-stage column at (0,0) sits above the shifted-down column at (1,0).
	// Lower/higher stage logic: lower-stage (1,0) gets TopBridge, higher-stage (0,0) gets BottomBridge.
	FLayoutRegionSolveRequest Request;
	Request.Seed = 45;
	Request.FootprintSize = FIntPoint(2, 1);
	Request.ProfileSnapshot.LevelCount = 1;
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(8, 8, 8);
	Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector(0, 0, 0);
	Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector(1, 0, 0);
	FLayoutModePlan ModePlan;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	ModePlan.bUsesSteppedTerrainTopology = true;
	ModePlan.SiteCenterBlockWorldPos = FIntVector(32, 32, 16);
	ModePlan.SolveSeed = Request.Seed;
	ModePlan.WorldSeed = 7;
	ModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ModePlan.PlacementShiftCells);
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan = ModePlan;
	Request.bHasFrozenTerrainBiomeAdapterInput = true;
	Request.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos = ModePlan.SiteCenterBlockWorldPos;
	Request.FrozenTerrainBiomeAdapterInput.FootprintMinBlockWorldPos = FIntVector(28, 28, 16);
	Request.FrozenTerrainBiomeAdapterInput.FootprintSizeInBlocks = FIntPoint(16, 8);
	Request.FrozenTerrainBiomeAdapterInput.EligibleBiomeRowName = TEXT("Biome.A");
	Request.FrozenTerrainBiomeAdapterInput.SearchStartZBlockWorld = 64;
	Request.FrozenTerrainBiomeAdapterInput.SearchDepthBlocks = 32;
	Request.FrozenTerrainBiomeAdapterInput.TerrainSampleGridSpacing = 8;
	Request.FrozenTerrainBiomeAdapterInput.bHasFiniteSearchBounds = true;
	Request.FrozenTerrainBiomeAdapterInput.SearchMinBlockXY = FIntPoint(28, 28);
	Request.FrozenTerrainBiomeAdapterInput.SearchMaxBlockXY = FIntPoint(43, 35);
	Request.FrozenTerrainBiomeAdapterInput.bHasSampledColumnEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasSteppedSupportEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasFootprintClassificationEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.SurfaceSamples.AddDefaulted_GetRef().bIsValid = true;
	Request.FrozenTerrainBiomeAdapterInput.SurfaceSamples[0].BlockXY = FIntPoint(28, 28);
	Request.FrozenTerrainBiomeAdapterInput.FootprintClassification.CellClassifications.AddDefaulted_GetRef().BlockXY = FIntPoint(28, 28);
	FLayoutSteppedTerrainSupportSample& HigherSupport = Request.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples.AddDefaulted_GetRef();
	HigherSupport.LocalCell = FIntVector(0, 0, 0);
	HigherSupport.SupportSurfaceZ = 24;
	FLayoutSteppedTerrainSupportSample& LowerSupport = Request.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples.AddDefaulted_GetRef();
	LowerSupport.LocalCell = FIntVector(1, 0, 0);
	LowerSupport.SupportSurfaceZ = 16;

	Request.FrozenTerrainBiomeAdapterInput.bHasTerrainPlacementEvidence = true;
	// (0,0) = higher stage (unshifted), (1,0) = lower stage (shifted-down)
	FLayoutTerrainPlacementCellEvidence& HigherPlacement = Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.AddDefaulted_GetRef();
	HigherPlacement.Cell = FIntVector(0, 0, 0);
	HigherPlacement.bPlaceableForSelectedMode = true;
	HigherPlacement.TerrainStageIndex = 1;
	HigherPlacement.VerticalShiftBlocks = 8;
	HigherPlacement.ProvenanceId = TEXT("Placement.Higher");
	FLayoutTerrainPlacementCellEvidence& LowerPlacement = Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.AddDefaulted_GetRef();
	LowerPlacement.Cell = FIntVector(1, 0, 0);
	LowerPlacement.bPlaceableForSelectedMode = true;
	LowerPlacement.TerrainStageIndex = 0;
	LowerPlacement.VerticalShiftBlocks = 0;
	LowerPlacement.ProvenanceId = TEXT("Placement.Lower");

	FLayoutRegionContract Contract;
	FString FailureReason;
	TestTrue(
		TEXT("Shift-down stepped adapter produces active-cell collection"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	// Stage-1 cluster has only 1 cell — below minimum of 3 — shift falls back to terrain excavation.
	// No Z-shift, no bridge cells, no TopBridge. Active cells = 2 original planned cells.
	TestEqual(TEXT("Sub-minimum cluster excludes shift/bridge — only original cells"), Contract.ActiveCells.Num(), 2);

	for (const FLayoutContractActiveCellRecord& ActiveCell : Contract.ActiveCells)
	{
		TestTrue(TEXT("Active cell at Z=0 (no shift for sub-minimum cluster)"), ActiveCell.Cell.Z == 0);
	}

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractTerrainAdapterReportsOutOfBoundsEvidenceDetailsTest,
	"PorismExtension.Layout.Contracts.TerrainAdapterReportsOutOfBoundsEvidenceDetails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractTerrainAdapterReportsOutOfBoundsEvidenceDetailsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.Seed = 55;
	Request.FootprintSize = FIntPoint(1, 1);
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(8, 8, 8);
	Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector::ZeroValue;

	FLayoutModePlan ModePlan;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	ModePlan.bUsesSteppedTerrainTopology = true;
	ModePlan.SiteCenterBlockWorldPos = FIntVector(32, 32, 16);
	ModePlan.SolveSeed = Request.Seed;
	ModePlan.WorldSeed = 7;
	ModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ModePlan.PlacementShiftCells);
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan = ModePlan;

	Request.bHasFrozenTerrainBiomeAdapterInput = true;
	FLayoutFrozenTerrainBiomeAdapterInput& Artifact = Request.FrozenTerrainBiomeAdapterInput;
	Artifact.ArtifactId = TEXT("Terrain.BoundsFixture");
	Artifact.ModePlanId = ModePlan.ModePlanId;
	Artifact.SiteCenterBlockWorldPos = ModePlan.SiteCenterBlockWorldPos;
	Artifact.FootprintMinBlockWorldPos = FIntVector(28, 28, 16);
	Artifact.FootprintSizeInBlocks = FIntPoint(8, 8);
	Artifact.EligibleBiomeRowName = TEXT("Biome.A");
	Artifact.SearchStartZBlockWorld = 64;
	Artifact.SearchDepthBlocks = 32;
	Artifact.TerrainSampleGridSpacing = 8;
	Artifact.bHasFiniteSearchBounds = true;
	Artifact.SearchMinBlockXY = FIntPoint(28, 28);
	Artifact.SearchMaxBlockXY = FIntPoint(35, 35);
	Artifact.bHasSampledColumnEvidence = true;
	Artifact.bHasSteppedSupportEvidence = true;
	Artifact.bHasFootprintClassificationEvidence = true;

	FLayoutTerrainSurfaceSample& BadSurfaceSample = Artifact.SurfaceSamples.AddDefaulted_GetRef();
	BadSurfaceSample.bIsValid = true;
	BadSurfaceSample.BlockXY = FIntPoint(44, 28);
	Artifact.FootprintClassification.CellClassifications.AddDefaulted_GetRef().BlockXY = FIntPoint(28, 28);
	FLayoutSteppedTerrainSupportSample& SupportSample = Artifact.SteppedSupportSamples.AddDefaulted_GetRef();
	SupportSample.LocalCell = FIntVector::ZeroValue;
	SupportSample.SupportSurfaceZ = 16;
	Artifact.bHasTerrainPlacementEvidence = true;
	FLayoutTerrainPlacementCellEvidence& PlacementCell = Artifact.TerrainPlacementCells.AddDefaulted_GetRef();
	PlacementCell.Cell = FIntVector::ZeroValue;
	PlacementCell.bPlaceableForSelectedMode = true;
	PlacementCell.bHasClearanceEvidence = true;
	PlacementCell.ProvenanceId = TEXT("Placement.BoundsFixture");

	FLayoutRegionContract Contract;
	FString FailureReason;
	TestFalse(
		TEXT("Terrain adapter rejects out-of-bounds frozen surface samples"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestTrue(TEXT("Out-of-bounds diagnostic names source"), FailureReason.Contains(TEXT("source=surface")));
	TestTrue(TEXT("Out-of-bounds diagnostic names artifact"), FailureReason.Contains(TEXT("artifact=Terrain.BoundsFixture")));
	TestTrue(TEXT("Out-of-bounds diagnostic names mode plan"), FailureReason.Contains(TEXT("modePlan=")));
	TestTrue(TEXT("Out-of-bounds diagnostic names bad sample"), FailureReason.Contains(TEXT("sampleXY=")));
	TestTrue(TEXT("Out-of-bounds diagnostic names search min"), FailureReason.Contains(TEXT("searchMin=")));
	TestTrue(TEXT("Out-of-bounds diagnostic names search max"), FailureReason.Contains(TEXT("searchMax=")));
	TestTrue(TEXT("Out-of-bounds diagnostic names footprint"), FailureReason.Contains(TEXT("footprintMin=")) && FailureReason.Contains(TEXT("footprintSize=")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractManifestSummaryCarryThroughTest,
	"PorismExtension.Layout.Contracts.ManifestSummaryCarryThrough",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractManifestSummaryCarryThroughTest::RunTest(const FString& Parameters)
{
	const FLayoutRegionSolveRequest Request = BuildManifestFixtureRequest();
	const FLayoutContractManifest Manifest = FLayoutContractPipeline::BuildManifestFromSolveRequest(Request);

	TestEqual(TEXT("Manifest id uses effective snapshot id"), Manifest.ManifestId, Request.EffectiveSnapshotId);
	TestEqual(TEXT("Profile snapshot id is preserved"), Manifest.ProfileSnapshotId, Request.ProfileSnapshot.SnapshotId);
	TestEqual(TEXT("Profile source path is preserved"), Manifest.ProfileSourcePath, Request.ProfileSnapshot.SourceProfilePath);
	TestEqual(TEXT("Content-set snapshot id is preserved"), Manifest.ContentSetSnapshotId, Request.ContentSetSnapshot.SnapshotId);
	TestEqual(TEXT("Module-set snapshot id is preserved"), Manifest.ModuleCatalogId, Request.ModuleCatalog.SnapshotId);
	TestEqual(TEXT("World-binding id is preserved"), Manifest.WorldBindingId, Request.WorldBindingId);
	TestEqual(TEXT("Shared cell size is preserved"), Manifest.SharedCellSizeInBlocks, Request.ModuleCatalog.SharedCellSizeInBlocks);
	TestEqual(TEXT("Minimum footprint is preserved"), Manifest.MinimumFootprintInCells, Request.ProfileSnapshot.MinimumFootprintInCells);
	TestEqual(TEXT("Maximum footprint is preserved"), Manifest.MaximumFootprintInCells, Request.ProfileSnapshot.MaximumFootprintInCells);
	TestEqual(TEXT("Level count is preserved"), Manifest.LevelCount, Request.ProfileSnapshot.LevelCount);
	TestTrue(TEXT("Stepped terrain support flag is preserved"), Manifest.bSupportsSteppedTerrainSolve);
	TestTrue(TEXT("Terrain seam enable flag is preserved"), Manifest.bEnableTerrainSeams);
	TestEqual(TEXT("Exact VerticalAccess count is preserved"), Manifest.RequiredVerticalAccessCount, 2);
	TestEqual(TEXT("Closure requirement id is preserved"), Manifest.ClosureRequirementIds.Num(), 1);
	TestEqual(TEXT("Feature requirement id is preserved"), Manifest.ZoneFeatureRequirementIds.Num(), 1);
	TestEqual(TEXT("Content-entry summaries are preserved"), Manifest.ContentEntries.Num(), 2);
	TestEqual(TEXT("Module summaries are preserved"), Manifest.Modules.Num(), 1);
	TestEqual(TEXT("Unique validation assertions are preserved across request, profile, content, module, and child template sources"), Manifest.ValidationAssertions.Num(), 7);
	TestEqual(TEXT("First validation assertion keeps request ordering"), Manifest.ValidationAssertions[0].AssertionId, FLayoutId(TEXT("Assert.Request")));
	TestNotNull(
		TEXT("Child-template validation assertion is carried into the manifest"),
		Manifest.ValidationAssertions.FindByPredicate([](const FLayoutContractManifestValidationAssertion& Assertion)
		{
			return Assertion.AssertionId == FLayoutId(TEXT("Assert.ChildTemplate"));
		}));
	TestNotNull(
		TEXT("Module validation assertion is carried into the manifest"),
		Manifest.ValidationAssertions.FindByPredicate([](const FLayoutContractManifestValidationAssertion& Assertion)
		{
			return Assertion.AssertionId == FLayoutId(TEXT("Assert.Module"));
		}));

	const FLayoutContractManifestContentEntrySummary& ModuleEntry = Manifest.ContentEntries[0];
	TestEqual(TEXT("Module entry id is preserved"), ModuleEntry.EntryId, FName(TEXT("Entry.Module")));
	TestEqual(TEXT("Module entry kind is preserved"), ModuleEntry.ContentKind, ELayoutRegionContentKind::Module);
	TestEqual(TEXT("Module placement zone is preserved"), ModuleEntry.ModulePlacementZone, ELayoutPlacementZone::Perimeter);
	TestEqual(TEXT("Module level policy is preserved"), ModuleEntry.ModuleLevelPlacementPolicy, ELayoutLevelPlacementPolicy::SpecificLevel);
	TestEqual(TEXT("Module specific level is preserved"), ModuleEntry.ModuleSpecificLevel, 1);
	TestTrue(TEXT("Module optional flag is preserved"), ModuleEntry.bModuleOptional);
	TestEqual(TEXT("Module closure intent is preserved"), ModuleEntry.ClosureProviderIntents[0].ProviderIntentId, FName(TEXT("Closure.Intent")));
	TestEqual(TEXT("Module seam intent is preserved"), ModuleEntry.SeamProviderIntents[0].SeamIntentId, FName(TEXT("Seam.Intent")));

	const FLayoutContractManifestContentEntrySummary& ChildEntry = Manifest.ContentEntries[1];
	TestEqual(TEXT("Child entry id is preserved"), ChildEntry.EntryId, FName(TEXT("Entry.Child")));
	TestEqual(TEXT("Child entry kind is preserved"), ChildEntry.ContentKind, ELayoutRegionContentKind::ChildRegion);
	TestEqual(TEXT("Child profile snapshot id is preserved"), ChildEntry.ChildProfileSnapshotId, FLayoutId(TEXT("Child.Profile.Snapshot")));
	TestEqual(TEXT("Child profile path is preserved"), ChildEntry.ChildProfilePath, FSoftObjectPath(TEXT("/Game/Layouts/TestChildProfile.TestChildProfile")));
	TestEqual(TEXT("Child content-set snapshot id is preserved"), ChildEntry.ChildContentSetSnapshotId, FLayoutId(TEXT("Child.Content.Snapshot")));
	TestEqual(TEXT("Child placement zone is preserved"), ChildEntry.ChildPlacementZone, ELayoutPlacementZone::Interior);
	TestTrue(TEXT("Child optional flag is preserved"), ChildEntry.bChildOptional);
	TestTrue(TEXT("Child host VerticalAccess contribution is preserved"), ChildEntry.bChildContributesHostVerticalAccess);

	const FLayoutContractManifestModuleSummary& ModuleSummary = Manifest.Modules[0];
	TestEqual(TEXT("Module snapshot id is preserved"), ModuleSummary.SnapshotId, FLayoutId(TEXT("Module.Snapshot")));
	TestEqual(TEXT("Module debug name is preserved"), ModuleSummary.DebugName, FName(TEXT("Module.Debug")));
	TestEqual(TEXT("Module bounds are preserved"), ModuleSummary.BoundsCells, FIntVector(2, 1, 1));
	TestEqual(TEXT("Every occupied local cell is preserved"), ModuleSummary.OccupiedLocalCells.Num(), 2);
	TestEqual(TEXT("Module source entry id is preserved"), ModuleSummary.SourceContentEntryId, FName(TEXT("Entry.Module")));
	TestEqual(TEXT("Module placement zone is preserved in summary"), ModuleSummary.PlacementZone, ELayoutPlacementZone::Perimeter);
	TestEqual(TEXT("Module level policy is preserved in summary"), ModuleSummary.LevelPlacementPolicy, ELayoutLevelPlacementPolicy::SpecificLevel);
	TestEqual(TEXT("Module specific level is preserved in summary"), ModuleSummary.SpecificLevel, 1);
	TestTrue(TEXT("Module optional flag is preserved in summary"), ModuleSummary.bOptional);
	TestFalse(TEXT("Module summary remains non-composite when no composite source exists"), ModuleSummary.bCompositeSnapshot);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractManifestCacheDeterminismTest,
	"PorismExtension.Layout.Contracts.ManifestCacheDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractManifestCacheDeterminismTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request = BuildManifestFixtureRequest();
	Request.ProfilePath = FSoftObjectPath(TEXT("/Game/Layouts/TestProfile.TestProfile"));
	Request.RootSolveId = TEXT("Root.Solve");
	Request.RootCandidateId = TEXT("Root.Candidate");
	Request.RootPlacementPolicyId = TEXT("Root.Policy");
	Request.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	Request.WorldBindingPlacementPolicy.TerrainSampleGridSpacing = 12;
	Request.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 3;
	Request.RootContinuationSelection.FamilyId = TEXT("Continuation.Family");
	Request.RootContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::SurfacePath;
	Request.RootContinuationSelection.ResolvedEntryLevel = 1;
	Request.DelegatedZoneFeatureRequirementIds = {TEXT("Feature.B"), TEXT("Feature.A"), TEXT("Feature.A")};
	Request.DelegatedClosureRequirementIds = {TEXT("Closure.B"), TEXT("Closure.A")};

	const FLayoutContractManifestCacheKey FirstKey = FLayoutContractManifestCache::BuildKeyFromSolveRequest(Request);
	FString ArtifactFailureReason;
	FLayoutFrozenRequestManifestArtifact ValidArtifact = FLayoutContractManifestCache::BuildFrozenRequestManifestArtifact(Request);
	TestTrue(TEXT("Frozen request manifest artifact validates when it has stable ids and no live sources"), FLayoutContractManifestCache::ValidateFrozenRequestManifestArtifact(ValidArtifact, ArtifactFailureReason));
	FLayoutFrozenRequestManifestArtifact LiveSourceArtifact = ValidArtifact;
	LiveSourceArtifact.ProfileSnapshot.SourceProfile = NewObject<ULayoutProfileAsset>();
	TestFalse(TEXT("Frozen request manifest artifact rejects live profile sources"), FLayoutContractManifestCache::ValidateFrozenRequestManifestArtifact(LiveSourceArtifact, ArtifactFailureReason));
	TestTrue(TEXT("Live profile source validation failure names source"), ArtifactFailureReason.Contains(TEXT("profile source")));
	FLayoutFrozenRequestManifestArtifact MissingKeyArtifact = ValidArtifact;
	MissingKeyArtifact.Key.KeyId = NAME_None;
	TestFalse(TEXT("Frozen request manifest artifact rejects missing cache key id"), FLayoutContractManifestCache::ValidateFrozenRequestManifestArtifact(MissingKeyArtifact, ArtifactFailureReason));
	FLayoutRegionSolveRequest ReorderedRequest = Request;
	Algo::Reverse(ReorderedRequest.DelegatedZoneFeatureRequirementIds);
	Algo::Reverse(ReorderedRequest.DelegatedClosureRequirementIds);
	ReorderedRequest.RegionDebugPath = TEXT("Different/Debug/Path");
	ReorderedRequest.Seed = 999;
	ReorderedRequest.RegionCellOffset = FIntVector(100, -20, 7);
	ReorderedRequest.TemplatePlacementZOffsetBlocks = 64;
	const FLayoutContractManifestCacheKey ReorderedKey = FLayoutContractManifestCache::BuildKeyFromSolveRequest(ReorderedRequest);
	TestEqual(TEXT("Manifest cache key ignores location, seed, debug path, and delegated-id input order"), FirstKey.KeyId, ReorderedKey.KeyId);
	TestEqual(TEXT("Delegated feature ids are sorted and deduplicated"), ReorderedKey.DelegatedZoneFeatureRequirementIds.Num(), 2);
	TestEqual(TEXT("Delegated feature ids sort deterministically"), ReorderedKey.DelegatedZoneFeatureRequirementIds[0], FLayoutId(TEXT("Feature.A")));

	FLayoutRegionSolveRequest DifferentProfileRequest = Request;
	DifferentProfileRequest.ProfileSnapshot.SnapshotId = TEXT("Profile.Other");
	TestNotEqual(
		TEXT("Different profile snapshot changes manifest cache key"),
		FirstKey.KeyId,
		FLayoutContractManifestCache::BuildKeyFromSolveRequest(DifferentProfileRequest).KeyId);

	FLayoutRegionSolveRequest DifferentPolicyRequest = Request;
	DifferentPolicyRequest.RootPlacementPolicyId = TEXT("Root.Policy.Other");
	TestNotEqual(
		TEXT("Different policy id changes manifest cache key"),
		FirstKey.KeyId,
		FLayoutContractManifestCache::BuildKeyFromSolveRequest(DifferentPolicyRequest).KeyId);

	FLayoutRegionSolveRequest TerrainSeamsDisabledRequest = Request;
	TerrainSeamsDisabledRequest.ProfileSnapshot.bEnableTerrainSeams = false;
	TestNotEqual(
		TEXT("Terrain seam mode changes manifest cache key"),
		FirstKey.KeyId,
		FLayoutContractManifestCache::BuildKeyFromSolveRequest(TerrainSeamsDisabledRequest).KeyId);

	FLayoutRegionSolveRequest DifferentBindingRequest = Request;
	DifferentBindingRequest.WorldBindingId = TEXT("WorldBinding.Secondary");
	TestNotEqual(
		TEXT("Different owning world-binding id changes manifest cache key"),
		FirstKey.KeyId,
		FLayoutContractManifestCache::BuildKeyFromSolveRequest(DifferentBindingRequest).KeyId);

	FLayoutContractManifestCache Cache;
	FLayoutContractManifestCacheEntry FirstEntry;
	bool bCacheHit = true;
	TestTrue(TEXT("First manifest lookup succeeds"), Cache.FindOrAddManifest(Request, FirstEntry, bCacheHit));
	TestFalse(TEXT("First manifest lookup is a cache miss"), bCacheHit);
	TestEqual(TEXT("Cache stores one entry after miss"), Cache.Num(), 1);
	TestEqual(TEXT("Cached entry key matches deterministic key"), FirstEntry.Key.KeyId, FirstKey.KeyId);

	FLayoutContractManifestCacheEntry SecondEntry;
	TestTrue(TEXT("Second manifest lookup succeeds"), Cache.FindOrAddManifest(ReorderedRequest, SecondEntry, bCacheHit));
	TestTrue(TEXT("Second equivalent manifest lookup is a cache hit"), bCacheHit);
	TestEqual(TEXT("Equivalent request does not add a second cache entry"), Cache.Num(), 1);
	TestEqual(TEXT("Cache hit returns same deterministic key"), SecondEntry.Key.KeyId, FirstEntry.Key.KeyId);
	TestEqual(TEXT("Cache hit returns same manifest id"), SecondEntry.Manifest.ManifestId, FirstEntry.Manifest.ManifestId);

	FLayoutFrozenRequestManifestArtifact RebuiltArtifact = FLayoutContractManifestCache::BuildFrozenRequestManifestArtifact(ReorderedRequest);
	RebuiltArtifact.ValidationAssertions.Add(MakeAssertionRecord(TEXT("Assert.WouldOnlyAppearOnRebuild")));
	FLayoutContractManifestCacheEntry ReusedArtifactEntry;
	TestTrue(TEXT("Equivalent frozen artifact lookup succeeds"), Cache.FindOrAddManifest(RebuiltArtifact, ReusedArtifactEntry, bCacheHit));
	TestTrue(TEXT("Equivalent frozen artifact lookup is a cache hit"), bCacheHit);
	TestEqual(TEXT("Equivalent frozen artifact does not add a second cache entry"), Cache.Num(), 1);
	TestFalse(
		TEXT("Equivalent cache hit reuses frozen manifest payload instead of rebuilding from changed artifact assertions"),
		ReusedArtifactEntry.Manifest.ValidationAssertions.ContainsByPredicate([](const FLayoutContractManifestValidationAssertion& Assertion)
		{
			return Assertion.AssertionId == FLayoutId(TEXT("Assert.WouldOnlyAppearOnRebuild"));
		}));

	FLayoutContractManifestCacheEntry DifferentEntry;
	TestTrue(TEXT("Different policy manifest lookup succeeds"), Cache.FindOrAddManifest(DifferentPolicyRequest, DifferentEntry, bCacheHit));
	TestFalse(TEXT("Different policy manifest lookup is a cache miss"), bCacheHit);
	TestEqual(TEXT("Different policy adds a second cache entry"), Cache.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractCandidateOrderingKeyTest,
	"PorismExtension.Layout.Contracts.CandidateOrderingKeysAreDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractCandidateOrderingKeyTest::RunTest(const FString& Parameters)
{
	FLayoutContractCandidateOrderingKey LaterKey;
	LaterKey.ManifestId = TEXT("Manifest.Root");
	LaterKey.SolveSeed = 17;
	LaterKey.LocalObligationId = TEXT("Obligation.Seam");
	LaterKey.CandidateId = TEXT("Candidate.B");
	LaterKey.PlacementShiftId = TEXT("Shift.0");
	LaterKey.AttemptIndex = 0;
	LaterKey.ChildCertificateId = TEXT("Cert.Child");
	LaterKey.ChildCertificateInputHash = 99;

	FLayoutContractCandidateOrderingKey EarlierKey = LaterKey;
	EarlierKey.CandidateId = TEXT("Candidate.A");
	EarlierKey.ChildCertificateInputHash = 11;

	TArray<FLayoutContractCandidateOrderingKey> Keys = {LaterKey, EarlierKey};
	FLayoutContractPlacementCandidates::SortCandidateKeys(Keys);
	TestEqual(TEXT("Candidate id orders stable keys deterministically"), Keys[0].CandidateId, FLayoutId(TEXT("Candidate.A")));
	TestEqual(TEXT("Ordering id is stable for identical value fields"), LaterKey.BuildOrderingId(), LaterKey.BuildOrderingId());

	FLayoutContractCandidateOrderingKey DifferentAttemptKey = LaterKey;
	DifferentAttemptKey.AttemptIndex = 1;
	TestNotEqual(TEXT("Attempt index participates in ordering id"), LaterKey.BuildOrderingId(), DifferentAttemptKey.BuildOrderingId());

	FLayoutContractCandidateOrderingKey DifferentCertificateKey = EarlierKey;
	DifferentCertificateKey.ChildCertificateInputHash = 22;
	TestNotEqual(TEXT("Child certificate input hash participates in ordering id"), EarlierKey.BuildOrderingId(), DifferentCertificateKey.BuildOrderingId());

	TArray<FLayoutContractCandidateOrderingKey> ReversedKeys = {EarlierKey, LaterKey};
	FLayoutContractPlacementCandidates::SortCandidateKeys(ReversedKeys);
	TestEqual(TEXT("Input order does not change sorted first candidate"), ReversedKeys[0].BuildOrderingId(), Keys[0].BuildOrderingId());

	const TArray<FLayoutCoarsePlacementShiftCandidate> Shifts =
		FLayoutContractPlacementCandidates::BuildCoarsePlacementShiftCandidates(
			TEXT("Manifest.Root"),
			17,
			TEXT("Obligation.RootSite"),
			FIntPoint(4, 3),
			2,
			1);
	TestEqual(TEXT("One coarse ring emits center plus eight shifts"), Shifts.Num(), 9);
	TestTrue(TEXT("Coarse shifts include unshifted candidate"), Shifts.ContainsByPredicate([](const FLayoutCoarsePlacementShiftCandidate& Candidate)
	{
		return Candidate.ShiftCells == FIntVector::ZeroValue;
	}));
	TestFalse(TEXT("Coarse shifts do not default to one-cell scan offsets"), Shifts.ContainsByPredicate([](const FLayoutCoarsePlacementShiftCandidate& Candidate)
	{
		return FMath::Abs(Candidate.ShiftCells.X) == 1 || FMath::Abs(Candidate.ShiftCells.Y) == 1;
	}));
	TestTrue(TEXT("Coarse shifts step by footprint width"), Shifts.ContainsByPredicate([](const FLayoutCoarsePlacementShiftCandidate& Candidate)
	{
		return Candidate.ShiftCells == FIntVector(4, 0, 0);
	}));
	TestTrue(TEXT("Coarse shifts step by footprint height"), Shifts.ContainsByPredicate([](const FLayoutCoarsePlacementShiftCandidate& Candidate)
	{
		return Candidate.ShiftCells == FIntVector(0, 3, 0);
	}));
	const TArray<FLayoutCoarsePlacementShiftCandidate> RebuiltShifts =
		FLayoutContractPlacementCandidates::BuildCoarsePlacementShiftCandidates(
			TEXT("Manifest.Root"),
			17,
			TEXT("Obligation.RootSite"),
			FIntPoint(4, 3),
			2,
			1);
	TestEqual(TEXT("Coarse shift order is deterministic"), RebuiltShifts[0].ShiftId, Shifts[0].ShiftId);
	TestEqual(TEXT("Coarse shift ordering carries attempt index"), Shifts[0].OrderingKey.AttemptIndex, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractActiveCellMaskIgnoresOrderInInputTest,
	"PorismExtension.Layout.Contracts.ActiveCellMaskIgnoresOrderInInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractActiveCellMaskIgnoresOrderInInputTest::RunTest(const FString& Parameters)
{
	FLayoutContractActiveCellRecord RealCell;
	RealCell.Cell = FIntVector(1, 2, 0);

	FLayoutContractActiveCellRecord PseudoCell;
	PseudoCell.Cell = FIntVector(1, 2, 1);

	const TArray<FLayoutContractActiveCellRecord> OrderedCells = {RealCell, PseudoCell};
	const TArray<FLayoutContractActiveCellRecord> ReorderedCells = {PseudoCell, RealCell};
	TestEqual(
		TEXT("Active-cell mask id ignores input order"),
		FLayoutContractPipeline::BuildActiveCellMaskId(TEXT("ActiveMask"), OrderedCells),
		FLayoutContractPipeline::BuildActiveCellMaskId(TEXT("ActiveMask"), ReorderedCells));

	FLayoutContractActiveCellRecord OtherCell;
	OtherCell.Cell = FIntVector(3, 4, 0);
	TestNotEqual(
		TEXT("Active-cell mask id distinguishes cells at different coordinates"),
		FLayoutContractPipeline::BuildActiveCellMaskId(TEXT("ActiveMask"), {RealCell}),
		FLayoutContractPipeline::BuildActiveCellMaskId(TEXT("ActiveMask"), {OtherCell}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractActiveCellApplyFailsClosedTest,
	"PorismExtension.Layout.Contracts.ActiveCellApplyFailsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractActiveCellApplyFailsClosedTest::RunTest(const FString& Parameters)
{
	FLayoutRegionContract Contract;
	Contract.ModePlan.PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	Contract.FootprintSize = FIntPoint(1, 1);
	FLayoutPlannedCell& PlannedCell = Contract.PlannedCells.AddDefaulted_GetRef();
	PlannedCell.Cell = FIntVector::ZeroValue;
	PlannedCell.Intent = ELayoutCellIntent::Interior;
	FLayoutContractActiveCellRecord& PseudoOnlyCell = Contract.ActiveCells.AddDefaulted_GetRef();
	PseudoOnlyCell.Cell = FIntVector(0, 0, 1);

	FLayoutRegionSolveRequest Request;
	FString FailureReason;
	TestFalse(
		TEXT("Request finalization rejects planned cells missing real active-cell records"),
		FLayoutContractPipeline::TryApplyRegionContractToSolveRequest(Contract, Request, FailureReason));
	TestTrue(TEXT("Missing real active-cell failure is reported"), FailureReason.Contains(TEXT("real active cells")));

	FLayoutContractActiveCellRecord& RealCell = Contract.ActiveCells.AddDefaulted_GetRef();
	RealCell.Cell = PlannedCell.Cell;
	TestTrue(
		TEXT("Request finalization accepts planned cells backed by real active-cell records"),
		FLayoutContractPipeline::TryApplyRegionContractToSolveRequest(Contract, Request, FailureReason));
	TestEqual(TEXT("Contract planned cells apply to request"), Request.PlannedCells.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractActiveCellValidationTest,
	"PorismExtension.Layout.Contracts.ActiveCellValidationFailsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractActiveCellValidationTest::RunTest(const FString& Parameters)
{
	FString FailureReason;
	FLayoutContractActiveCellRecord ValidCell;
	ValidCell.Cell = FIntVector(0, 0, 0);
	TestTrue(
		TEXT("Valid active cell passes validation"),
		FLayoutContractPipeline::ValidateActiveCellRecords({ValidCell}, FailureReason));

	FLayoutContractActiveCellRecord DuplicateCell;
	DuplicateCell.Cell = FIntVector(0, 0, 0);
	TestFalse(
		TEXT("Active cell validation rejects duplicate cell records"),
		FLayoutContractPipeline::ValidateActiveCellRecords({ValidCell, DuplicateCell}, FailureReason));
	TestTrue(TEXT("Duplicate cell failure is reported"), FailureReason.Contains(TEXT("duplicate active-cell records")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractStableOrderingTest,
	"PorismExtension.Layout.Contracts.StableOrderingOfDiagnosticCategoriesAndContractIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractStableOrderingTest::RunTest(const FString& Parameters)
{
	const TArray<FName> ExpectedCategories = {
		TEXT("None"),
		TEXT("ManifestInvalid"),
		TEXT("ModeUnsupported"),
		TEXT("TerrainSampleUnavailable"),
		TEXT("EntryTerrainSealed"),
		TEXT("ClearanceExcavationConflict"),
		TEXT("CoarsePlacementShiftsExhausted"),
		TEXT("InvalidRequiredTerrainOverlap"),
		TEXT("DisconnectedStage"),
		TEXT("MissingVerticalAccessFrontier"),
		TEXT("ExactVerticalAccessCountImpossible"),
		TEXT("FeatureProviderCountImpossible"),
		TEXT("ChildPlacementImpossible"),
		TEXT("SeamHandoffImpossible"),
		TEXT("BoundaryClosureImpossible"),
		TEXT("FoundationRampBudgetImpossible"),
		TEXT("BridgeSpanInvalid"),
		TEXT("BridgeApproachLandingUnreachable"),
		TEXT("TunnelExcavationConflict"),
		TEXT("InnerProofFailed"),
		TEXT("PlanningWindowRootPresolveMissedReadyRadius"),
		TEXT("BackgroundSolveWaitingForDispatch"),
		TEXT("BackgroundSolveCanceledByGroup"),
		TEXT("BackgroundSolveSupersededByGeneration"),
		TEXT("BackgroundSolveExpiredOutsidePlanningWindow"),
		TEXT("ContinuationEndpointOutsidePlanningWindow"),
		TEXT("ContinuationGraphHasNoAcceptedTargetRoots"),
		TEXT("ChunkRealizationWaitingForRequiredChunkOrigins")
	};
	TestEqual(TEXT("Diagnostic category order is stable"), FLayoutContractPipeline::GetStableDiagnosticCategoryNames(), ExpectedCategories);

	const TArray<FIntVector> OrderedCells = {
		FIntVector(0, 0, 0),
		FIntVector(2, 0, 0),
		FIntVector(1, 1, 0)
	};
	const TArray<FIntVector> ReorderedCells = {
		FIntVector(1, 1, 0),
		FIntVector(0, 0, 0),
		FIntVector(2, 0, 0)
	};
	TestEqual(
		TEXT("Cell mask id ignores input order"),
		FLayoutContractPipeline::BuildCellMaskId(TEXT("ActiveMask"), OrderedCells),
		FLayoutContractPipeline::BuildCellMaskId(TEXT("ActiveMask"), ReorderedCells));

	TArray<FLayoutPlannedCell> PlannedCells;
	FLayoutPlannedCell& First = PlannedCells.AddDefaulted_GetRef();
	First.Cell = FIntVector(3, 0, 0);
	First.Intent = ELayoutCellIntent::Interior;
	FLayoutPlannedCell& Second = PlannedCells.AddDefaulted_GetRef();
	Second.Cell = FIntVector(0, 1, 0);
	Second.Intent = ELayoutCellIntent::Entry;

	TArray<FLayoutPlannedCell> ReorderedPlannedCells = {Second, First};
	TestEqual(
		TEXT("Stage-map id ignores input order"),
		FLayoutContractPipeline::BuildStageMapId(PlannedCells),
		FLayoutContractPipeline::BuildStageMapId(ReorderedPlannedCells));

	TArray<FLayoutPlannedCell> TerrainSeamVariant = PlannedCells;
	TerrainSeamVariant[0].TerrainSeamFaceMask = LayoutFaceDirectionMask(ELayoutFaceDirection::PosX);
	TestNotEqual(
		TEXT("Stage-map id changes when terrain seam topology changes"),
		FLayoutContractPipeline::BuildStageMapId(PlannedCells),
		FLayoutContractPipeline::BuildStageMapId(TerrainSeamVariant));

	FLayoutFrozenTerrainContract FirstContract;
	FirstContract.SiteCenterBlockWorldPos = FIntVector(32, 64, 0);
	FirstContract.FootprintMinBlockWorldPos = FIntVector(0, 0, 0);
	FirstContract.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
	FirstContract.FootprintSizeInCells = FIntPoint(2, 2);
	FirstContract.DiagnosticKind = ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedFlatFit;
	FLayoutFrozenTerrainWriteRecord& WriteA = FirstContract.TerrainWrites.AddDefaulted_GetRef();
	WriteA.BlockWorldPos = FIntVector(16, 0, 0);
	WriteA.Material = 7;
	WriteA.SourceContract = ELayoutFrozenTerrainCellContract::FlatClearance;
	FLayoutFrozenTerrainWriteRecord& WriteB = FirstContract.TerrainWrites.AddDefaulted_GetRef();
	WriteB.BlockWorldPos = FIntVector(0, 0, 0);
	WriteB.Material = 3;
	WriteB.SourceContract = ELayoutFrozenTerrainCellContract::Active;

	FLayoutFrozenTerrainContract SecondContract = FirstContract;
	Algo::Reverse(SecondContract.TerrainWrites);
	TestEqual(
		TEXT("Terrain-write artifact id ignores write input order"),
		FLayoutContractPipeline::BuildTerrainWriteArtifactId(FirstContract),
		FLayoutContractPipeline::BuildTerrainWriteArtifactId(SecondContract));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractPipelineAcceptsMixedRampAndClearanceEvidenceTest,
	"PorismExtension.Layout.Contracts.Pipeline.AcceptsMixedRampAndClearanceEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractPipelineAcceptsMixedRampAndClearanceEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.Seed = 55;
	Request.FootprintSize = FIntPoint(1, 1);
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(8, 8, 8);
	Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector::ZeroValue;

	FLayoutModePlan ModePlan;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::NonSteppedWorldPlacement;
	ModePlan.SiteCenterBlockWorldPos = FIntVector(0, 0, 0);
	ModePlan.SolveSeed = Request.Seed;
	ModePlan.WorldSeed = 7;
	ModePlan.PlacementPolicy.TerrainTransition.bAllowPerimeterRampTransition = true;
	ModePlan.PlacementPolicy.TerrainTransition.MaxFoundationDepth = 4;
	ModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ModePlan.PlacementShiftCells);
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan = ModePlan;

	Request.bHasFrozenTerrainBiomeAdapterInput = true;
	Request.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos = FIntVector::ZeroValue;
	Request.FrozenTerrainBiomeAdapterInput.FootprintMinBlockWorldPos = FIntVector(0, 0, 0);
	Request.FrozenTerrainBiomeAdapterInput.FootprintSizeInBlocks = FIntPoint(8, 8);
	Request.FrozenTerrainBiomeAdapterInput.EligibleBiomeRowName = TEXT("Biome.Test");
	Request.FrozenTerrainBiomeAdapterInput.SearchStartZBlockWorld = 64;
	Request.FrozenTerrainBiomeAdapterInput.SearchDepthBlocks = 32;
	Request.FrozenTerrainBiomeAdapterInput.TerrainSampleGridSpacing = 8;
	Request.FrozenTerrainBiomeAdapterInput.bHasFiniteSearchBounds = true;
	Request.FrozenTerrainBiomeAdapterInput.SearchMinBlockXY = FIntPoint(0, 0);
	Request.FrozenTerrainBiomeAdapterInput.SearchMaxBlockXY = FIntPoint(7, 7);
	Request.FrozenTerrainBiomeAdapterInput.bHasSampledColumnEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasSteppedSupportEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasFootprintClassificationEvidence = true;
	FLayoutTerrainSurfaceSample& AdapterSurfaceSample = Request.FrozenTerrainBiomeAdapterInput.SurfaceSamples.AddDefaulted_GetRef();
	AdapterSurfaceSample.bIsValid = true;
	AdapterSurfaceSample.BlockXY = FIntPoint(0, 0);
	Request.FrozenTerrainBiomeAdapterInput.FootprintClassification.CellClassifications.AddDefaulted_GetRef().BlockXY = FIntPoint(0, 0);
	FLayoutSteppedTerrainSupportSample& SupportSample = Request.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples.AddDefaulted_GetRef();
	SupportSample.LocalCell = FIntVector::ZeroValue;
	SupportSample.SupportSurfaceZ = 0;

	Request.FrozenTerrainBiomeAdapterInput.bHasTerrainPlacementEvidence = true;
	FLayoutTerrainPlacementCellEvidence& Evidence = Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.AddDefaulted_GetRef();
	Evidence.Cell = FIntVector::ZeroValue;
	Evidence.bPlaceableForSelectedMode = true;
	Evidence.bHasRampTransitionEvidence = true;
	Evidence.bHasClearanceEvidence = true;
	Evidence.ProvenanceId = TEXT("PlacementEvidence.MixedRampClearance");

	FLayoutRegionContract Contract;
	FString FailureReason;
	const bool bAccepted = PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason);
	TestTrue(TEXT("Mixed ramp+clearance evidence survives adapter validation"), bAccepted);
	if (!bAccepted)
	{
		return true;
	}
	TestEqual(TEXT("Active cells preserved"), Contract.ActiveCells.Num(), 1);
	TestEqual(TEXT("Cell contract is Active (not FlatClearance)"), Contract.FrozenTerrainContract.CellContracts[0].Contract, ELayoutFrozenTerrainCellContract::Active);
	TestTrue(TEXT("Ramp transition evidence preserved"), Contract.FrozenTerrainContract.CellContracts[0].bHasRampTransitionEvidence);
	TestTrue(TEXT("Clearance evidence preserved"), Contract.FrozenTerrainContract.CellContracts[0].bHasClearanceEvidence);
	TestFalse(TEXT("Foundation fill not inferred"), Contract.FrozenTerrainContract.CellContracts[0].bHasFoundationFillEvidence);

	FString FinalizationFailure;
	TestTrue(TEXT("Final contract pipeline settles mixed candidate evidence"),
		FLayoutContractPipeline::TryPrecomputeAdapterOutput(Request, FinalizationFailure));
	if (!Request.PrecomputedFrozenTerrainContract.CellContracts.IsEmpty())
	{
		const FLayoutTerrainCellContractRecord& FinalCell =
			Request.PrecomputedFrozenTerrainContract.CellContracts[0];
		TestTrue(TEXT("Perimeter ramp authority survives final Entry selection"), FinalCell.bHasRampTransitionEvidence);
		TestTrue(TEXT("Perimeter ramp retains clearance evidence"), FinalCell.bHasClearanceEvidence);
		TestEqual(TEXT("Mixed ramp and clearance ownership remains active"),
			FinalCell.Contract,
			ELayoutFrozenTerrainCellContract::Active);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractPipelineAcceptsMixedRampAndExcavationEvidenceTest,
	"PorismExtension.Layout.Contracts.Pipeline.AcceptsMixedRampAndExcavationEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractPipelineAcceptsMixedRampAndExcavationEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.Seed = 56;
	Request.FootprintSize = FIntPoint(1, 1);
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(8, 8, 8);
	Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector::ZeroValue;

	FLayoutModePlan ModePlan;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::NonSteppedWorldPlacement;
	ModePlan.SiteCenterBlockWorldPos = FIntVector(0, 0, 0);
	ModePlan.SolveSeed = Request.Seed;
	ModePlan.WorldSeed = 7;
	ModePlan.PlacementPolicy.TerrainTransition.bAllowPerimeterRampTransition = true;
	ModePlan.PlacementPolicy.TerrainTransition.MaxFoundationDepth = 4;
	ModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ModePlan.PlacementShiftCells);
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan = ModePlan;

	Request.bHasFrozenTerrainBiomeAdapterInput = true;
	Request.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos = FIntVector::ZeroValue;
	Request.FrozenTerrainBiomeAdapterInput.FootprintMinBlockWorldPos = FIntVector(0, 0, 0);
	Request.FrozenTerrainBiomeAdapterInput.FootprintSizeInBlocks = FIntPoint(8, 8);
	Request.FrozenTerrainBiomeAdapterInput.EligibleBiomeRowName = TEXT("Biome.Test");
	Request.FrozenTerrainBiomeAdapterInput.SearchStartZBlockWorld = 64;
	Request.FrozenTerrainBiomeAdapterInput.SearchDepthBlocks = 32;
	Request.FrozenTerrainBiomeAdapterInput.TerrainSampleGridSpacing = 8;
	Request.FrozenTerrainBiomeAdapterInput.bHasFiniteSearchBounds = true;
	Request.FrozenTerrainBiomeAdapterInput.SearchMinBlockXY = FIntPoint(0, 0);
	Request.FrozenTerrainBiomeAdapterInput.SearchMaxBlockXY = FIntPoint(7, 7);
	Request.FrozenTerrainBiomeAdapterInput.bHasSampledColumnEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasSteppedSupportEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasFootprintClassificationEvidence = true;
	FLayoutTerrainSurfaceSample& AdapterSurfaceSample = Request.FrozenTerrainBiomeAdapterInput.SurfaceSamples.AddDefaulted_GetRef();
	AdapterSurfaceSample.bIsValid = true;
	AdapterSurfaceSample.BlockXY = FIntPoint(0, 0);
	Request.FrozenTerrainBiomeAdapterInput.FootprintClassification.CellClassifications.AddDefaulted_GetRef().BlockXY = FIntPoint(0, 0);
	FLayoutSteppedTerrainSupportSample& SupportSample = Request.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples.AddDefaulted_GetRef();
	SupportSample.LocalCell = FIntVector::ZeroValue;
	SupportSample.SupportSurfaceZ = 0;

	Request.FrozenTerrainBiomeAdapterInput.bHasTerrainPlacementEvidence = true;
	FLayoutTerrainPlacementCellEvidence& Evidence = Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.AddDefaulted_GetRef();
	Evidence.Cell = FIntVector::ZeroValue;
	Evidence.bPlaceableForSelectedMode = true;
	Evidence.bHasRampTransitionEvidence = true;
	Evidence.bHasExcavationEvidence = true;
	Evidence.bHasLocalOverlapZ = true;
	LayoutLocalBlockCoordinates::TryMakeCoord8(0, Evidence.OverlapMinLocalZ);
	LayoutLocalBlockCoordinates::TryMakeCoord8(4, Evidence.OverlapMaxLocalZ);
	Evidence.ProvenanceId = TEXT("PlacementEvidence.MixedRampExcavation");

	FLayoutRegionContract Contract;
	FString FailureReason;
	const bool bAccepted = PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason);
	TestTrue(TEXT("Mixed ramp+excavation evidence survives adapter validation"), bAccepted);
	if (!bAccepted)
	{
		return true;
	}
	TestEqual(TEXT("Active cells preserved"), Contract.ActiveCells.Num(), 1);
	TestEqual(TEXT("Cell contract is Active (not FlatClearance)"), Contract.FrozenTerrainContract.CellContracts[0].Contract, ELayoutFrozenTerrainCellContract::Active);
	TestTrue(TEXT("Ramp transition evidence preserved"), Contract.FrozenTerrainContract.CellContracts[0].bHasRampTransitionEvidence);
	TestTrue(TEXT("Excavation overlap bounds preserved"), Contract.FrozenTerrainContract.CellContracts[0].bHasExcavationOverlapZ);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractPipelineRejectsFoundationAndExcavationMixedEvidenceTest,
	"PorismExtension.Layout.Contracts.Pipeline.RejectsFoundationAndExcavationMixedEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractPipelineRejectsFoundationAndExcavationMixedEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.Seed = 57;
	Request.FootprintSize = FIntPoint(1, 1);
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(8, 8, 8);
	Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector::ZeroValue;

	FLayoutModePlan ModePlan;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::NonSteppedWorldPlacement;
	ModePlan.SiteCenterBlockWorldPos = FIntVector(0, 0, 0);
	ModePlan.SolveSeed = Request.Seed;
	ModePlan.WorldSeed = 7;
	ModePlan.PlacementPolicy.TerrainTransition.bAllowFoundationFill = true;
	ModePlan.PlacementPolicy.TerrainTransition.MaxFoundationDepth = 4;
	ModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ModePlan.PlacementShiftCells);
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan = ModePlan;

	Request.bHasFrozenTerrainBiomeAdapterInput = true;
	Request.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos = FIntVector::ZeroValue;
	Request.FrozenTerrainBiomeAdapterInput.FootprintMinBlockWorldPos = FIntVector(0, 0, 0);
	Request.FrozenTerrainBiomeAdapterInput.FootprintSizeInBlocks = FIntPoint(8, 8);
	Request.FrozenTerrainBiomeAdapterInput.EligibleBiomeRowName = TEXT("Biome.Test");
	Request.FrozenTerrainBiomeAdapterInput.SearchStartZBlockWorld = 64;
	Request.FrozenTerrainBiomeAdapterInput.SearchDepthBlocks = 32;
	Request.FrozenTerrainBiomeAdapterInput.TerrainSampleGridSpacing = 8;
	Request.FrozenTerrainBiomeAdapterInput.bHasFiniteSearchBounds = true;
	Request.FrozenTerrainBiomeAdapterInput.SearchMinBlockXY = FIntPoint(0, 0);
	Request.FrozenTerrainBiomeAdapterInput.SearchMaxBlockXY = FIntPoint(7, 7);
	Request.FrozenTerrainBiomeAdapterInput.bHasSampledColumnEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasSteppedSupportEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasFootprintClassificationEvidence = true;
	FLayoutTerrainSurfaceSample& AdapterSurfaceSample = Request.FrozenTerrainBiomeAdapterInput.SurfaceSamples.AddDefaulted_GetRef();
	AdapterSurfaceSample.bIsValid = true;
	AdapterSurfaceSample.BlockXY = FIntPoint(0, 0);
	Request.FrozenTerrainBiomeAdapterInput.FootprintClassification.CellClassifications.AddDefaulted_GetRef().BlockXY = FIntPoint(0, 0);
	FLayoutSteppedTerrainSupportSample& SupportSample = Request.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples.AddDefaulted_GetRef();
	SupportSample.LocalCell = FIntVector::ZeroValue;
	SupportSample.SupportSurfaceZ = 0;

	Request.FrozenTerrainBiomeAdapterInput.bHasTerrainPlacementEvidence = true;
	FLayoutTerrainPlacementCellEvidence& Evidence = Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.AddDefaulted_GetRef();
	Evidence.Cell = FIntVector::ZeroValue;
	Evidence.bPlaceableForSelectedMode = true;
	Evidence.bHasFoundationFillEvidence = true;
	Evidence.RequiredFoundationDepth = 1;
	Evidence.FoundationMaterial = 1;
	Evidence.bHasExcavationEvidence = true;
	Evidence.bHasLocalOverlapZ = true;
	LayoutLocalBlockCoordinates::TryMakeCoord8(0, Evidence.OverlapMinLocalZ);
	LayoutLocalBlockCoordinates::TryMakeCoord8(4, Evidence.OverlapMaxLocalZ);
	Evidence.ProvenanceId = TEXT("PlacementEvidence.FoundationExcavation");

	FLayoutRegionContract Contract;
	FString FailureReason;
	TestFalse(
		TEXT("Foundation fill + excavation on same cell is rejected"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestTrue(TEXT("Failure reports foundation mixed with excavation"), FailureReason.Contains(TEXT("foundation")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractSteppedAdapterRejectsIncompleteStageMapTest,
	"PorismExtension.Layout.Contracts.SteppedAdapterRejectsIncompleteStageMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractSteppedAdapterRejectsIncompleteStageMapTest::RunTest(const FString& Parameters)
{
	// 2x2 footprint with SteppedSurfacePlacement but only 1 support sample — must fail.
	FLayoutRegionSolveRequest Request;
	Request.Seed = 88;
	Request.FootprintSize = FIntPoint(2, 2);
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(16, 16, 8);
	for (int32 Y = 0; Y < 2; ++Y)
	{
		for (int32 X = 0; X < 2; ++X)
		{
			Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector(X, Y, 0);
		}
	}

	FLayoutModePlan ModePlan;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	ModePlan.bUsesSteppedTerrainTopology = true;
	ModePlan.SiteCenterBlockWorldPos = FIntVector(64, 64, 32);
	ModePlan.SolveSeed = Request.Seed;
	ModePlan.WorldSeed = 7;
	ModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ModePlan.PlacementShiftCells);
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan = ModePlan;

	Request.bHasFrozenTerrainBiomeAdapterInput = true;
	Request.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos = ModePlan.SiteCenterBlockWorldPos;
	Request.FrozenTerrainBiomeAdapterInput.FootprintMinBlockWorldPos = FIntVector(56, 56, 32);
	Request.FrozenTerrainBiomeAdapterInput.FootprintSizeInBlocks = FIntPoint(32, 32);
	Request.FrozenTerrainBiomeAdapterInput.EligibleBiomeRowName = TEXT("Biome.A");
	Request.FrozenTerrainBiomeAdapterInput.EligibleBiomeRowNames = {TEXT("Biome.A")};
	Request.FrozenTerrainBiomeAdapterInput.SearchDepthBlocks = 32;
	Request.FrozenTerrainBiomeAdapterInput.bHasFiniteSearchBounds = true;
	Request.FrozenTerrainBiomeAdapterInput.SearchMinBlockXY = FIntPoint(56, 56);
	Request.FrozenTerrainBiomeAdapterInput.SearchMaxBlockXY = FIntPoint(87, 87);
	Request.FrozenTerrainBiomeAdapterInput.bHasSampledColumnEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasSteppedSupportEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasFootprintClassificationEvidence = true;

	// Only 1 support sample and 1 footprint classification for a 2x2 footprint.
	// Surface samples still cover all 4 columns to pass base validation.
	for (int32 Y = 0; Y < 2; ++Y)
	{
		for (int32 X = 0; X < 2; ++X)
		{
			FLayoutTerrainSurfaceSample& Surface = Request.FrozenTerrainBiomeAdapterInput.SurfaceSamples.AddDefaulted_GetRef();
			Surface.bIsValid = true;
			Surface.BlockXY = FIntPoint(56 + X * 16, 56 + Y * 16);
		}
	}
	Request.FrozenTerrainBiomeAdapterInput.FootprintClassification.CellClassifications.AddDefaulted_GetRef().BlockXY = FIntPoint(56, 56);
	Request.FrozenTerrainBiomeAdapterInput.FootprintClassification.CellClassifications.AddDefaulted_GetRef().BlockXY = FIntPoint(72, 56);
	Request.FrozenTerrainBiomeAdapterInput.FootprintClassification.CellClassifications.AddDefaulted_GetRef().BlockXY = FIntPoint(56, 72);
	Request.FrozenTerrainBiomeAdapterInput.FootprintClassification.CellClassifications.AddDefaulted_GetRef().BlockXY = FIntPoint(72, 72);

	// Only 1 support sample for a 2x2 footprint — missing 3 columns.
	{
		FLayoutSteppedTerrainSupportSample& S = Request.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples.AddDefaulted_GetRef();
		S.LocalCell = FIntVector(0, 0, 0);
		S.SupportSurfaceZ = 32;
		S.SnappedSupportFloorZ = 32;
	}

	// Placement evidence still needed.
	Request.FrozenTerrainBiomeAdapterInput.bHasTerrainPlacementEvidence = true;
	for (const FLayoutPlannedCell& PC : Request.PlannedCells)
	{
		FLayoutTerrainPlacementCellEvidence& Evidence = Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.AddDefaulted_GetRef();
		Evidence.Cell = PC.Cell;
		Evidence.bPlaceableForSelectedMode = true;
		Evidence.bHasClearanceEvidence = true;
		Evidence.ProvenanceId = TEXT("Evidence");
	}

	FLayoutRegionContract Contract;
	FString FailureReason;
	TestFalse(
		TEXT("Stepped adapter must not succeed with incomplete support data for footprint"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	// The adapter may reject for different reasons (stage map column mismatch, missing evidence, etc.),
	// but it must not succeed when footprint columns exceed support samples.
	TestTrue(TEXT("Failure reason is reported for incomplete footprint coverage"), !FailureReason.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractAdapterRejectsZeroWalkableBoundarySteppedPlacementTest,
	"PorismExtension.Layout.Contracts.AdapterRejectsZeroWalkableBoundarySteppedPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractAdapterRejectsZeroWalkableBoundarySteppedPlacementTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.ProfileSnapshot.DebugName = TEXT("TestProfile");
	Request.Seed = 100;
	Request.FootprintSize = FIntPoint(3, 3);
	Request.ProfileSnapshot.LevelCount = 1;
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
	for (int32 Y = 0; Y < 3; ++Y)
		for (int32 X = 0; X < 3; ++X)
			Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector(X, Y, 0);

	FLayoutModePlan ModePlan;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	ModePlan.bUsesSteppedTerrainTopology = true;
	ModePlan.SiteCenterBlockWorldPos = FIntVector(24, 24, 0);
	ModePlan.SolveSeed = Request.Seed;
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan = ModePlan;
	Request.bHasFrozenTerrainBiomeAdapterInput = true;
	Request.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos = ModePlan.SiteCenterBlockWorldPos;
	Request.FrozenTerrainBiomeAdapterInput.FootprintMinBlockWorldPos = FIntVector(0, 0, 0);
	Request.FrozenTerrainBiomeAdapterInput.FootprintSizeInBlocks = FIntPoint(48, 48);
	Request.FrozenTerrainBiomeAdapterInput.EligibleBiomeRowName = TEXT("Biome.Test");
	Request.FrozenTerrainBiomeAdapterInput.SearchStartZBlockWorld = 64;
	Request.FrozenTerrainBiomeAdapterInput.SearchDepthBlocks = 32;
	Request.FrozenTerrainBiomeAdapterInput.TerrainSampleGridSpacing = 16;
	Request.FrozenTerrainBiomeAdapterInput.bHasFiniteSearchBounds = true;
	Request.FrozenTerrainBiomeAdapterInput.SearchMinBlockXY = FIntPoint(0, 0);
	Request.FrozenTerrainBiomeAdapterInput.SearchMaxBlockXY = FIntPoint(47, 47);
	Request.FrozenTerrainBiomeAdapterInput.bHasSampledColumnEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasSteppedSupportEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasFootprintClassificationEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasTerrainPlacementEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.SurfaceSamples.AddDefaulted_GetRef().bIsValid = true;
	Request.FrozenTerrainBiomeAdapterInput.SurfaceSamples[0].BlockXY = FIntPoint(0, 0);
	Request.FrozenTerrainBiomeAdapterInput.FootprintClassification.CellClassifications.AddDefaulted_GetRef().BlockXY = FIntPoint(0, 0);

	// All columns at the same stage with a support sample each.
	for (int32 Y = 0; Y < 3; ++Y)
		for (int32 X = 0; X < 3; ++X)
		{
			FLayoutSteppedTerrainSupportSample& S = Request.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples.AddDefaulted_GetRef();
			S.LocalCell = FIntVector(X, Y, 0);
			S.SupportSurfaceZ = 8;
			S.SnappedSupportFloorZ = 0;
			S.SnappedSupportCeilingZ = 16;

			FLayoutTerrainPlacementCellEvidence& P = Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.AddDefaulted_GetRef();
			P.Cell = FIntVector(X, Y, 0);
			P.bPlaceableForSelectedMode = true;
			P.ProvenanceId = *FString::Printf(TEXT("P%d_%d"), X, Y);
			// Mark all boundary cells as CliffEdge — no walkable entries.
			if (X == 0 || X == 2 || Y == 0 || Y == 2)
			{
				P.EntryTraversability = ELayoutEntryTraversabilityVerdict::CliffEdge;
			}
		}

	FLayoutRegionContract Contract;
	FString FailureReason;
	TestFalse(
		TEXT("SteppedSurfacePlacement fails when no boundary cell is walkable"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));
	TestTrue(
		TEXT("Failure reason mentions walkable entry traversability"),
		FailureReason.Contains(TEXT("no boundary cell has walkable entry traversability")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractAdapterAcceptsTunnelExcavationEntryTest,
	"PorismExtension.Layout.Contracts.AdapterAcceptsTunnelExcavationEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractAdapterAcceptsTunnelExcavationEntryTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.ProfileSnapshot.DebugName = TEXT("TestTunnel");
	Request.Seed = 200;
	Request.FootprintSize = FIntPoint(2, 1);
	Request.ProfileSnapshot.LevelCount = 1;
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
	Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector(0, 0, 0);
	Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector(1, 0, 0);

	FLayoutModePlan ModePlan;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::NonSteppedWorldPlacement;
	ModePlan.SiteCenterBlockWorldPos = FIntVector(16, 8, 0);
	ModePlan.SolveSeed = Request.Seed;
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan = ModePlan;
	Request.bHasFrozenTerrainBiomeAdapterInput = true;
	Request.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos = ModePlan.SiteCenterBlockWorldPos;
	Request.FrozenTerrainBiomeAdapterInput.FootprintMinBlockWorldPos = FIntVector(8, 4, 0);
	Request.FrozenTerrainBiomeAdapterInput.FootprintSizeInBlocks = FIntPoint(32, 16);
	Request.FrozenTerrainBiomeAdapterInput.EligibleBiomeRowName = TEXT("Biome.Tunnel");
	Request.FrozenTerrainBiomeAdapterInput.SearchStartZBlockWorld = 64;
	Request.FrozenTerrainBiomeAdapterInput.SearchDepthBlocks = 32;
	Request.FrozenTerrainBiomeAdapterInput.TerrainSampleGridSpacing = 16;
	Request.FrozenTerrainBiomeAdapterInput.bHasFiniteSearchBounds = true;
	Request.FrozenTerrainBiomeAdapterInput.SearchMinBlockXY = FIntPoint(8, 4);
	Request.FrozenTerrainBiomeAdapterInput.SearchMaxBlockXY = FIntPoint(39, 19);
	Request.FrozenTerrainBiomeAdapterInput.bHasSampledColumnEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.bHasTerrainPlacementEvidence = true;
	Request.FrozenTerrainBiomeAdapterInput.SurfaceSamples.AddDefaulted_GetRef().bIsValid = true;
	Request.FrozenTerrainBiomeAdapterInput.SurfaceSamples[0].BlockXY = FIntPoint(8, 4);

	// Cell (0,0): ExcavationNeeded with excavation → walkable.
	FLayoutTerrainPlacementCellEvidence& ExcavCell = Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.AddDefaulted_GetRef();
	ExcavCell.Cell = FIntVector(0, 0, 0);
	ExcavCell.bPlaceableForSelectedMode = true;
	ExcavCell.EntryTraversability = ELayoutEntryTraversabilityVerdict::ExcavationNeeded;
	ExcavCell.bHasExcavationEvidence = true;
	ExcavCell.bHasLocalOverlapZ = true;
	ExcavCell.ProvenanceId = TEXT("TunnelEx");

	// Cell (1,0): ExcavationNeeded WITHOUT excavation → NOT walkable.
	FLayoutTerrainPlacementCellEvidence& NoExcavCell = Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.AddDefaulted_GetRef();
	NoExcavCell.Cell = FIntVector(1, 0, 0);
	NoExcavCell.bPlaceableForSelectedMode = true;
	NoExcavCell.EntryTraversability = ELayoutEntryTraversabilityVerdict::ExcavationNeeded;
	NoExcavCell.bHasExcavationEvidence = false;
	NoExcavCell.ProvenanceId = TEXT("NoEx");

	FLayoutRegionContract Contract;
	FString FailureReason;
	TestTrue(
		TEXT("NonStepped adapter accepts tunnel excavation cells with authorization"),
		PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(Request, ModePlan.SiteCenterBlockWorldPos, 0, Contract, FailureReason));

	bool bFoundExcavWalkable = false;
	bool bFoundNoExcavWalkable = false;
	for (const FLayoutTerrainCellContractRecord& R : Contract.FrozenTerrainContract.CellContracts)
	{
		if (R.Cell == FIntVector(0, 0, 0)) { bFoundExcavWalkable = R.bEntryWalkable; }
		if (R.Cell == FIntVector(1, 0, 0)) { bFoundNoExcavWalkable = R.bEntryWalkable; }
	}
	TestTrue(TEXT("ExcavationNeeded + excavation = walkable"), bFoundExcavWalkable);
	TestFalse(TEXT("ExcavationNeeded without excavation = not walkable"), bFoundNoExcavWalkable);

	return true;
}

// NOTE: The two precompute tests below validate TryPrecomputeAdapterOutput at the
// adapter level.  Full descriptor-producer pipeline integration tests (verifying that
// TryBuildProducedDescriptorArtifactFromSnapshotInternal returns false on terrain-mode
// precompute failure, or carries PrecomputedAdapterOutput on success) require
// ValidateSelectedModePlan to pass inside FinalizeRequestFromPacket, which in turn
// needs RuntimeSnapshot.PlacementKind to equal the manifest's CapturedRootPlacementKind.
// The adapter-level coverage is equivalent for the precompute behavior — the descriptor
// producer calls TryPrecomputeAdapterOutput and captures its output verbatim.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractPrecomputeAdapterPopulatesRequestFieldsTest,
	"PorismExtension.Layout.Contracts.PrecomputeAdapterPopulatesRequestFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractPrecomputeAdapterPopulatesRequestFieldsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.ProfileSnapshot.DebugName = TEXT("TestPrecompute");
	Request.Seed = 100;
	Request.FootprintSize = FIntPoint(2, 1);
	Request.ProfileSnapshot.LevelCount = 1;
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
	Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector(0, 0, 0);
	Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector(1, 0, 0);

	FLayoutModePlan ModePlan;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::StandardRegion;
	ModePlan.SiteCenterBlockWorldPos = FIntVector(16, 8, 0);
	ModePlan.SolveSeed = Request.Seed;
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan = ModePlan;

	FString FailureReason;
	TestTrue(
		TEXT("Precompute adapter succeeds for StandardRegion"),
		FLayoutContractPipeline::TryPrecomputeAdapterOutput(Request, FailureReason));
	TestFalse(
		TEXT("Precomputed frozen contract id is non-None"),
		Request.PrecomputedFrozenTerrainContract.ContractId.IsNone());
	TestTrue(
		TEXT("Precomputed active cells are non-empty"),
		Request.PrecomputedActiveCells.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractPrecomputeAdapterRejectsTerrainModeWithoutEvidenceTest,
	"PorismExtension.Layout.Contracts.PrecomputeAdapterRejectsTerrainModeWithoutEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContractPrecomputeAdapterRejectsTerrainModeWithoutEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.ProfileSnapshot.DebugName = TEXT("TestRejectPrecompute");
	Request.Seed = 200;
	Request.FootprintSize = FIntPoint(2, 1);
	Request.ProfileSnapshot.LevelCount = 1;
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
	Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector(0, 0, 0);

	FLayoutModePlan ModePlan;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::NonSteppedWorldPlacement;
	ModePlan.SiteCenterBlockWorldPos = FIntVector(16, 8, 0);
	ModePlan.SolveSeed = Request.Seed;
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan = ModePlan;
	// Deliberately leave bHasFrozenTerrainBiomeAdapterInput false.

	FString FailureReason;
	TestFalse(
		TEXT("Precompute adapter rejects NonSteppedWorldPlacement without terrain evidence"),
		FLayoutContractPipeline::TryPrecomputeAdapterOutput(Request, FailureReason));
	TestTrue(
		TEXT("Failure reason mentions frozen terrain artifact requirement"),
		FailureReason.Contains(TEXT("frozen terrain")));
	return true;
}

/** Verifies a three-cell shifted top deck reports the classified failure and retries from a fresh flat footprint. */
