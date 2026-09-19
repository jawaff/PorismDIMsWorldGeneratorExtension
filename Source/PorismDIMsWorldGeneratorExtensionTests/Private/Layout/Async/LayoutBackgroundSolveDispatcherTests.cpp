#include "Layout/Types/LayoutId.h"
#include "Layout/Async/LayoutSolveExecution.h"
// Copyright 2026 Spotted Loaf Studio

#include "Layout/Async/LayoutBackgroundSolveDispatcher.h"
#include "Layout/Async/LayoutBackgroundSolveSnapshot.h"
#include "Layout/Async/LayoutBackgroundSolveTypes.h"
#include "Layout/Async/LayoutActiveBiomeNoiseSnapshot.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutBackgroundLifecycleSequencer.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutParentResumeArtifact.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutWorkerSolvePacket.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "Engine/DataTable.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Contracts/LayoutContractModeSelection.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"
#include "Layout/Solver/LayoutWorkerSolveRequestFinalizer.h"

#include "HAL/PlatformProcess.h"
#include "UObject/SoftObjectPath.h"
#include "Misc/AutomationTest.h"
#include "Tasks/Task.h"

namespace
{
	bool PumpDispatcherUntil(
		FLayoutBackgroundSolveDispatcher& Dispatcher,
		TFunctionRef<bool()> Predicate,
		const int32 MaxPumpCount = 200)
	{
		for (int32 PumpIndex = 0; PumpIndex < MaxPumpCount; ++PumpIndex)
		{
			Dispatcher.Tick();
			if (Predicate())
			{
				return true;
			}
		}
		return Predicate();
	}

	FLayoutBackgroundSolveSubmission MakeCounterJob(
		const TCHAR* const DebugName,
		const uint64 GroupId,
		const ELayoutBackgroundSolveJobTier Tier,
		const int32 Priority,
		TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> WorkCounter,
		TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> PublishCounter)
	{
		FLayoutBackgroundSolveSubmission Submission;
		Submission.DebugName = DebugName;
		Submission.LayoutGroupId = GroupId;
		Submission.Tier = Tier;
		Submission.Priority = Priority;
		Submission.Work = [WorkCounter](const FLayoutSolveCancellationToken& CancellationToken, FString& OutFailureReason)
		{
			if (CancellationToken.IsCancellationRequested())
			{
				OutFailureReason = TEXT("Canceled before work.");
				return false;
			}
			WorkCounter->Increment();
			return !CancellationToken.IsCancellationRequested();
		};
		Submission.PublishOnGameThread = [PublishCounter](const FLayoutBackgroundSolveCompletion& Completion)
		{
			if (Completion.bWorkSucceeded)
			{
				PublishCounter->Increment();
			}
		};
		return Submission;
	}

	FLayoutBackgroundSolveSubmission MakeOrderedJob(
		const TCHAR* const DebugName,
		const uint64 GroupId,
		const ELayoutBackgroundSolveJobTier Tier,
		const int32 Priority,
		TSharedRef<TArray<FString>, ESPMode::ThreadSafe> PublishOrder,
		TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> PublishCounter,
		TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> OptionalReleaseGate = nullptr)
	{
		FLayoutBackgroundSolveSubmission Submission;
		Submission.DebugName = DebugName;
		Submission.LayoutGroupId = GroupId;
		Submission.Tier = Tier;
		Submission.Priority = Priority;
		Submission.Work = [OptionalReleaseGate](const FLayoutSolveCancellationToken& CancellationToken, FString& OutFailureReason)
		{
			while (OptionalReleaseGate.IsValid() && !static_cast<bool>(*OptionalReleaseGate) && !CancellationToken.IsCancellationRequested())
			{
			}
			if (CancellationToken.IsCancellationRequested())
			{
				OutFailureReason = TEXT("Canceled before ordered work.");
				return false;
			}
			return true;
		};
		Submission.PublishOnGameThread = [PublishOrder, PublishCounter, Name = FString(DebugName)](const FLayoutBackgroundSolveCompletion& Completion)
		{
			if (Completion.bWorkSucceeded)
			{
				PublishOrder->Add(Name);
				PublishCounter->Increment();
			}
		};
		return Submission;
	}

	constexpr const TCHAR* ActiveBiomeConstantPositiveFastNoise = TEXT("AAAAAIA/");

	UWorldGenDef* CreateActiveBiomeSnapshotWorldGenDef(UObject* const Outer)
	{
		UWorldGenDef* const WorldGenDef = NewObject<UWorldGenDef>(Outer);
		WorldGenDef->BaseBlockSize = 100;
		WorldGenDef->NoiseScale = FVector::OneVector;
		WorldGenDef->NoiseCoordinateOffset = FIntVector::ZeroValue;
		return WorldGenDef;
	}

	FLayoutNoiseCoordinateSettings MakeActiveBiomeSnapshotCoordinateSettings(const UWorldGenDef* const WorldGenDef)
	{
		FLayoutNoiseCoordinateSettings Settings;
		Settings.BaseBlockSize = WorldGenDef != nullptr ? WorldGenDef->BaseBlockSize : 100;
		Settings.NoiseScale = WorldGenDef != nullptr ? WorldGenDef->NoiseScale : FVector::OneVector;
		Settings.NoiseCoordinateOffset = WorldGenDef != nullptr ? WorldGenDef->NoiseCoordinateOffset : FIntVector::ZeroValue;
		return Settings;
	}

	bool CompareActiveBiomeSamples(
		FAutomationTestBase& Test,
		const TCHAR* const Context,
		const FLayoutActiveBiomeSample& SamplerSample,
		const FLayoutActiveBiomeSample& SnapshotSample)
	{
		bool bMatches = true;
		bMatches &= Test.TestEqual(FString::Printf(TEXT("%s valid state matches"), Context), SnapshotSample.bIsValid, SamplerSample.bIsValid);
		bMatches &= Test.TestEqual(FString::Printf(TEXT("%s positive-domain state matches"), Context), SnapshotSample.bAnyPositiveDomain, SamplerSample.bAnyPositiveDomain);
		bMatches &= Test.TestEqual(FString::Printf(TEXT("%s row name matches"), Context), SnapshotSample.WinningRow.RowName, SamplerSample.WinningRow.RowName);
		bMatches &= Test.TestEqual(FString::Printf(TEXT("%s terrain solid state matches"), Context), SnapshotSample.bTerrainSolid, SamplerSample.bTerrainSolid);
		bMatches &= Test.TestEqual(FString::Printf(TEXT("%s Gen selector matches"), Context), SnapshotSample.bSelectedGenA, SamplerSample.bSelectedGenA);
		bMatches &= Test.TestEqual(FString::Printf(TEXT("%s domain value matches"), Context), SnapshotSample.WinningDomainValue, SamplerSample.WinningDomainValue);
		bMatches &= Test.TestEqual(FString::Printf(TEXT("%s terrain value matches"), Context), SnapshotSample.TerrainValue, SamplerSample.TerrainValue);
		return bMatches;
	}

	bool SampleAndCompareActiveBiomeSnapshot(
		FAutomationTestBase& Test,
		const UWorldGenDef* const WorldGenDef,
		const TSharedRef<const FLayoutActiveBiomeNoiseSnapshot, ESPMode::ThreadSafe>& Snapshot,
		const int32 Seed,
		const FIntVector SamplePosition,
		const TCHAR* const Context)
	{
		FLayoutActiveBiomeSampler Sampler;
		if (!Test.TestTrue(FString::Printf(TEXT("%s sampler initializes"), Context), Sampler.Initialize(GetTransientPackage(), WorldGenDef, Seed)))
		{
			return false;
		}

		const FLayoutNoiseCoordinateSettings CoordinateSettings = MakeActiveBiomeSnapshotCoordinateSettings(WorldGenDef);
		FLayoutActiveBiomeSample SamplerSample;
		FLayoutActiveBiomeSample SnapshotSample;
		bool bMatches = true;
		bMatches &= Test.TestTrue(FString::Printf(TEXT("%s sampler samples"), Context), Sampler.SampleAtBlockPosition(SamplePosition, CoordinateSettings, SamplerSample));
		bMatches &= Test.TestTrue(FString::Printf(TEXT("%s snapshot samples"), Context), Snapshot->SampleAtBlockPosition(SamplePosition, CoordinateSettings, SnapshotSample));
		bMatches &= CompareActiveBiomeSamples(Test, Context, SamplerSample, SnapshotSample);
		return bMatches;
	}

	TSharedRef<const FLayoutActiveBiomeNoiseSnapshot, ESPMode::ThreadSafe> CaptureActiveBiomeSnapshotForTest(
		FAutomationTestBase& Test,
		const UWorldGenDef* const WorldGenDef,
		const int32 Seed,
		const TCHAR* const Context)
	{
		FString FailureReason;
		TSharedRef<const FLayoutActiveBiomeNoiseSnapshot, ESPMode::ThreadSafe> Snapshot =
			FLayoutActiveBiomeNoiseSnapshot::CaptureFromWorldDefinition(GetTransientPackage(), WorldGenDef, Seed, FailureReason);
		Test.TestTrue(FString::Printf(TEXT("%s snapshot initializes"), Context), Snapshot->IsInitialized());
		Test.TestTrue(FString::Printf(TEXT("%s snapshot capture has no failure"), Context), FailureReason.IsEmpty());
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutParentResumeArtifactStableOrderTest,
	"PorismExtension.Layout.Async.ParentResumeArtifactStableChildOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutParentResumeArtifactStableOrderTest::RunTest(const FString& Parameters)
{
	FLayoutParentResumeArtifact Artifact;
	Artifact.ParentContractId = TEXT("Parent.Contract");
	Artifact.ProofContract.ParentRequest.RegionDebugPath = TEXT("Parent.Region");
	Artifact.ParentRegionDebugPath = TEXT("Parent.Region");
	Artifact.ParentRequest.RegionDebugPath = TEXT("Parent.Region");
	Artifact.YieldReason = TEXT("Waiting for certified child proof jobs.");

	FLayoutBackgroundSolveHandle FirstHandle;
	FirstHandle.GenerationId = 1;
	FirstHandle.LayoutGroupId = 10;
	FirstHandle.JobId = 100;
	FirstHandle.Tier = ELayoutBackgroundSolveJobTier::InProgressLayoutGroup;
	FLayoutBackgroundSolveHandle SecondHandle = FirstHandle;
	SecondHandle.JobId = 101;

	FLayoutRegionSolveRequest FirstChildRequest;
	FirstChildRequest.RegionDebugPath = TEXT("Child.A");
	FirstChildRequest.SourceContentEntryId = TEXT("Entry.A");
	FLayoutRegionSolveRequest SecondChildRequest;
	SecondChildRequest.RegionDebugPath = TEXT("Child.B");
	SecondChildRequest.SourceContentEntryId = TEXT("Entry.B");
	FLayoutChildSolveHandoff FirstChildHandoff;
	FirstChildHandoff.ParentRegionDebugPath = TEXT("Parent.Region");
	FirstChildHandoff.ChildRegionDebugPath = TEXT("Child.A");
	FirstChildHandoff.StableChildKey = TEXT("Child.A");
	FirstChildHandoff.ContentMetadata.SourceContentEntryId = TEXT("Entry.A");
	FirstChildHandoff.ContentMetadata.ChildProfilePath = FSoftObjectPath(TEXT("/Game/Test/ChildA.ChildA"));
	FirstChildHandoff.bRequiresCertifiedWitnessBundle = true;
	FirstChildHandoff.CertifiedWitnessBundle.SourceContentEntryId = TEXT("Entry.A");
	FirstChildHandoff.CertifiedWitnessBundle.StableChildId = TEXT("Child.A");
	FirstChildHandoff.CertifiedWitnessBundle.BranchId = TEXT("Branch.Parent");
	FirstChildHandoff.RefreshProofCertificate(TEXT("ChildCert.A"));
	FLayoutChildSolveHandoff SecondChildHandoff = FirstChildHandoff;
	SecondChildHandoff.ChildRegionDebugPath = TEXT("Child.B");
	SecondChildHandoff.StableChildKey = TEXT("Child.B");
	SecondChildHandoff.ContentMetadata.SourceContentEntryId = TEXT("Entry.B");
	SecondChildHandoff.ContentMetadata.ChildProfilePath = FSoftObjectPath(TEXT("/Game/Test/ChildB.ChildB"));
	SecondChildHandoff.CertifiedWitnessBundle.SourceContentEntryId = TEXT("Entry.B");
	SecondChildHandoff.CertifiedWitnessBundle.StableChildId = TEXT("Child.B");
	SecondChildHandoff.RefreshProofCertificate(TEXT("ChildCert.B"));
	FLayoutContractCandidateOrderingKey FirstOrderingKey;
	FirstOrderingKey.ManifestId = Artifact.ParentContractId;
	FirstOrderingKey.SolveSeed = 1;
	FirstOrderingKey.LocalObligationId = TEXT("Entry.A");
	FirstOrderingKey.CandidateId = TEXT("Child.A");
	FirstOrderingKey.ChildCertificateId = FirstChildHandoff.ProofCertificate.CertificateId;
	FirstOrderingKey.ChildCertificateInputHash = FirstChildHandoff.ProofCertificate.InputHash;
	FLayoutContractCandidateOrderingKey SecondOrderingKey = FirstOrderingKey;
	SecondOrderingKey.LocalObligationId = TEXT("Entry.B");
	SecondOrderingKey.CandidateId = TEXT("Child.B");
	SecondOrderingKey.ChildCertificateId = SecondChildHandoff.ProofCertificate.CertificateId;
	SecondOrderingKey.ChildCertificateInputHash = SecondChildHandoff.ProofCertificate.InputHash;
	Artifact.AddOrderedChildJob(TEXT("Child.A"), FirstChildRequest, FirstHandle, FirstChildHandoff, FirstOrderingKey);
	Artifact.AddOrderedChildJob(TEXT("Child.B"), SecondChildRequest, SecondHandle, SecondChildHandoff, SecondOrderingKey);

	FString FailureReason;
	TestTrue(TEXT("Parent resume artifact accepts matching stable child ids and handles"), Artifact.ValidateStableChildOrder(FailureReason));
	FLayoutParentResumeArtifact MissingActiveCellArtifact = Artifact;
	MissingActiveCellArtifact.bRequiresMergedActiveCellProvenance = true;
	TestFalse(TEXT("Parent resume artifact rejects missing merged active-cell provenance"), MissingActiveCellArtifact.ValidateStableChildOrder(FailureReason));
	TestTrue(TEXT("Missing merged active-cell provenance reports active-cell failure"), FailureReason.Contains(TEXT("active-cell")));
	FLayoutParentResumeArtifact MergedActiveCellArtifact = MissingActiveCellArtifact;
	FLayoutContractActiveCellRecord RealActiveCell;
	RealActiveCell.Cell = FIntVector::ZeroValue;
	MergedActiveCellArtifact.MergedActiveCells.Add(RealActiveCell);
	TestTrue(TEXT("Parent resume artifact accepts valid merged active-cell provenance"), MergedActiveCellArtifact.ValidateStableChildOrder(FailureReason));
	TestEqual(TEXT("First child id keeps certified order"), Artifact.OrderedChildRegionDebugPaths[0], FString(TEXT("Child.A")));
	TestEqual(TEXT("Second child handle keeps certified order"), Artifact.OrderedChildJobHandles[1], SecondHandle);

	FLayoutRegionSolveResult SecondChildResult;
	SecondChildResult.RegionDebugPath = TEXT("Child.B");
	FLayoutRegionSolveResult FirstChildResult;
	FirstChildResult.RegionDebugPath = TEXT("Child.A");
	TestFalse(TEXT("Parent resume artifact is not complete before child results arrive"), Artifact.HasAllOrderedChildResults());
	Artifact.RecordCompletedChildResult(SecondChildResult);
	TestFalse(TEXT("Parent resume artifact waits for every ordered child result"), Artifact.HasAllOrderedChildResults());
	Artifact.RecordCompletedChildResult(FirstChildResult);
	TestTrue(TEXT("Parent resume artifact is complete after every ordered child result arrives"), Artifact.HasAllOrderedChildResults());
	TArray<FLayoutRegionSolveResult> OrderedChildResults;
	TestTrue(TEXT("Parent resume artifact builds child results in certified order"), Artifact.BuildOrderedChildResults(OrderedChildResults, FailureReason));
	TestEqual(TEXT("First ordered child result follows artifact order"), OrderedChildResults[0].RegionDebugPath, FString(TEXT("Child.A")));
	TestEqual(TEXT("Second ordered child result follows artifact order"), OrderedChildResults[1].RegionDebugPath, FString(TEXT("Child.B")));

	FLayoutParentResumeArtifact MissingResultArtifact = Artifact;
	MissingResultArtifact.CompletedChildResultsByRegion.Remove(TEXT("Child.A"));
	TestFalse(TEXT("Parent resume artifact rejects missing child proof result"), MissingResultArtifact.BuildOrderedChildResults(OrderedChildResults, FailureReason));
	TestTrue(TEXT("Missing child proof result reports missing result failure"), FailureReason.Contains(TEXT("missing")));

	FLayoutParentResumeArtifact MissingHandleArtifact = Artifact;
	MissingHandleArtifact.OrderedChildJobHandles.Pop();
	TestFalse(TEXT("Parent resume artifact rejects mismatched child id/handle counts"), MissingHandleArtifact.ValidateStableChildOrder(FailureReason));
	TestTrue(TEXT("Mismatched child id/handle count reports count failure"), FailureReason.Contains(TEXT("counts")));

	FLayoutParentResumeArtifact MismatchedRequestArtifact = Artifact;
	MismatchedRequestArtifact.OrderedChildRequests[0].RegionDebugPath = TEXT("Child.Other");
	TestFalse(TEXT("Parent resume artifact rejects mismatched child request identity"), MismatchedRequestArtifact.ValidateStableChildOrder(FailureReason));
	TestTrue(TEXT("Mismatched child request reports request failure"), FailureReason.Contains(TEXT("request")));

	FLayoutParentResumeArtifact InvalidHandleArtifact = Artifact;
	InvalidHandleArtifact.OrderedChildJobHandles[0].Reset();
	TestFalse(TEXT("Parent resume artifact rejects invalid child handles"), InvalidHandleArtifact.ValidateStableChildOrder(FailureReason));
	TestTrue(TEXT("Invalid child handle reports handle failure"), FailureReason.Contains(TEXT("handle")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildProofSubmissionPreservesStructuredHandoffFailureTest,
	"PorismExtension.Layout.Async.Dispatcher.ChildProofPreservesStructuredHandoffFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildProofSubmissionPreservesStructuredHandoffFailureTest::RunTest(const FString& Parameters)
{
	FLayoutBackgroundSolveSettings Settings;
	Settings.MaxConcurrentBackgroundLayoutSolves = 1;
	FLayoutBackgroundSolveDispatcher Dispatcher(Settings);
	Dispatcher.SetExecution(MakeUnique<FSynchronousLayoutSolveExecution>());

	FLayoutChildSolveHandoff InvalidHandoff;
	InvalidHandoff.ParentRegionDebugPath = TEXT("Root");
	InvalidHandoff.ChildRegionDebugPath = TEXT("Root/Room");
	InvalidHandoff.CertifiedWitnessBundle.BranchId = TEXT("Branch.Room");
	FLayoutRegionSolveRequest ChildRequest;
	ChildRequest.RegionDebugPath = TEXT("Root/Room");
	ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	ChildRequest.SourceContentEntryId = TEXT("Room");

	TSharedRef<bool, ESPMode::ThreadSafe> bPublished =
		MakeShared<bool, ESPMode::ThreadSafe>(false);
	TSharedRef<FLayoutBackgroundSolveCompletion, ESPMode::ThreadSafe> PublishedCompletion =
		MakeShared<FLayoutBackgroundSolveCompletion, ESPMode::ThreadSafe>();
	TSharedRef<FLayoutRegionSolveResult, ESPMode::ThreadSafe> PublishedResult =
		MakeShared<FLayoutRegionSolveResult, ESPMode::ThreadSafe>();
	Dispatcher.Submit(FLayoutBackgroundLifecycleSequencer::BuildChildProofSubmission(
		TEXT("InvalidCertifiedChildProof"),
		71,
		0,
		InvalidHandoff,
		ChildRequest,
		[bPublished, PublishedCompletion, PublishedResult](
			const FLayoutBackgroundSolveCompletion& Completion,
			const FLayoutRegionSolveResult& Result)
		{
			*bPublished = true;
			*PublishedCompletion = Completion;
			*PublishedResult = Result;
		}));

	TestTrue(TEXT("Rejected child handoff publishes through dispatcher completion"), *bPublished);
	TestFalse(TEXT("Rejected child handoff reports failed worker execution"),
		PublishedCompletion->bWorkSucceeded);
	const FLayoutRegionalFailureRecord& Failure =
		PublishedResult->SolveResult.RegionalFailure;
	TestTrue(TEXT("Async child completion retains structured failure"), Failure.IsSet());
	TestEqual(TEXT("Async child completion retains child source"),
		Failure.SourceContentEntryId,
		FName(TEXT("Room")));
	TestEqual(TEXT("Async child completion retains child path"),
		Failure.RegionDebugPath,
		FString(TEXT("Root/Room")));
	TestEqual(TEXT("Async child completion identifies handoff validation"),
		Failure.Phase,
		FName(TEXT("ChildHandoffValidation")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBackgroundSolveAutoConcurrencyTest,
	"PorismExtension.Layout.Async.Dispatcher.AutoConcurrencyResolvesToSafeCoreClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBackgroundSolveAutoConcurrencyTest::RunTest(const FString& Parameters)
{
	FLayoutBackgroundSolveSettings Settings;
	Settings.MaxConcurrentBackgroundLayoutSolves = 0;
	const int32 LogicalCoreCount = FMath::Max(1, FPlatformMisc::NumberOfCoresIncludingHyperthreads());
	TestEqual(
		TEXT("Auto concurrency uses Clamp(LogicalCores - 2, 1, 4)"),
		Settings.ResolveMaxConcurrentBackgroundLayoutSolves(),
		FMath::Clamp(LogicalCoreCount - 2, 1, 4));

	Settings.MaxConcurrentBackgroundLayoutSolves = 3;
	TestEqual(TEXT("Explicit concurrency is preserved"), Settings.ResolveMaxConcurrentBackgroundLayoutSolves(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBackgroundSolveDurableBacklogTest,
	"PorismExtension.Layout.Async.Dispatcher.DurableBacklogDoesNotDropWaitingJobs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBackgroundSolveDurableBacklogTest::RunTest(const FString& Parameters)
{
	FLayoutBackgroundSolveSettings Settings;
	Settings.MaxConcurrentBackgroundLayoutSolves = 1;
	FLayoutBackgroundSolveDispatcher Dispatcher(Settings);
	Dispatcher.SetExecution(MakeUnique<FSynchronousLayoutSolveExecution>());
	Dispatcher.SetDisableAutoPumpForTesting(true);

	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> WorkCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> PublishCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	Dispatcher.Submit(MakeCounterJob(TEXT("JobA"), 1, ELayoutBackgroundSolveJobTier::NearRoot, 0, WorkCounter, PublishCounter));
	Dispatcher.Submit(MakeCounterJob(TEXT("JobB"), 1, ELayoutBackgroundSolveJobTier::NearRoot, 0, WorkCounter, PublishCounter));
	Dispatcher.Submit(MakeCounterJob(TEXT("JobC"), 1, ELayoutBackgroundSolveJobTier::NearRoot, 0, WorkCounter, PublishCounter));

	const FLayoutBackgroundSolveDiagnosticsSnapshot InitialSnapshot = Dispatcher.GetDiagnosticsSnapshot();
	TestTrue(TEXT("At least one job waits in durable table while cap is saturated"), InitialSnapshot.WaitingForDispatch > 0 || InitialSnapshot.Running > 0);

	const bool bCompleted = PumpDispatcherUntil(Dispatcher, [&PublishCounter]()
	{
		return PublishCounter->GetValue() == 3;
	});
	TestTrue(TEXT("All durable backlog jobs publish"), bCompleted);
	TestEqual(TEXT("All worker bodies ran"), WorkCounter->GetValue(), 3);
	TestEqual(TEXT("All completions published"), PublishCounter->GetValue(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBackgroundSolveLifecyclePrewarmRejectTest,
	"PorismExtension.Layout.Async.Dispatcher.ManifestPrewarmRejectsMissingFrozenManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBackgroundSolveLifecyclePrewarmRejectTest::RunTest(const FString& Parameters)
{
	FLayoutBackgroundSolveSettings Settings;
	Settings.MaxConcurrentBackgroundLayoutSolves = 1;
	FLayoutBackgroundSolveDispatcher Dispatcher(Settings);
	Dispatcher.SetExecution(MakeUnique<FSynchronousLayoutSolveExecution>());

	TSharedRef<bool, ESPMode::ThreadSafe> bPublished = MakeShared<bool, ESPMode::ThreadSafe>(false);
	TSharedRef<bool, ESPMode::ThreadSafe> bSawTerminalRejection = MakeShared<bool, ESPMode::ThreadSafe>(false);
	TSharedRef<bool, ESPMode::ThreadSafe> bSawLifecycleStage = MakeShared<bool, ESPMode::ThreadSafe>(false);

	FLayoutManifestPrewarmInput PrewarmInput;
	PrewarmInput.PrewarmId = TEXT("MissingManifestPrewarm");
	PrewarmInput.Kind = ELayoutManifestPrewarmKind::Root;
	PrewarmInput.bHasFrozenRequestManifest = false;

	FLayoutBackgroundSolveSubmission Submission = FLayoutBackgroundLifecycleSequencer::BuildManifestPrewarmSubmission(
		TEXT("MissingManifestPrewarm"),
		77,
		5,
		PrewarmInput,
		[bPublished, bSawTerminalRejection, bSawLifecycleStage](
			const FLayoutBackgroundSolveCompletion& Completion,
			const FLayoutManifestPrewarmResult& PrewarmResult)
		{
			*bPublished = true;
			*bSawTerminalRejection = PrewarmResult.bTerminalRejection && !PrewarmResult.bHasFrozenRequestManifest;
			*bSawLifecycleStage = Completion.LifecycleStage == ELayoutBackgroundSolveLifecycleStage::ManifestPrewarm;
		});

	Dispatcher.Submit(MoveTemp(Submission));
	const bool bCompleted = PumpDispatcherUntil(Dispatcher, [&bPublished]()
	{
		return *bPublished;
	});

	TestTrue(TEXT("Fail-closed manifest prewarm publishes completion"), bCompleted);
	TestTrue(TEXT("Missing manifest reports terminal rejection"), *bSawTerminalRejection);
	TestTrue(TEXT("Completion preserves manifest-prewarm lifecycle stage"), *bSawLifecycleStage);
	const FLayoutBackgroundSolveDiagnosticsSnapshot Snapshot = Dispatcher.GetDiagnosticsSnapshot();
	TestEqual(TEXT("Rejected prewarm is published as rejected"), Snapshot.PublishedRejected, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBackgroundSolveLifecycleTerminalFailureTest,
	"PorismExtension.Layout.Async.Dispatcher.ManifestPrewarmTerminalFailureCompletesOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies a rejected prewarm terminates one lifecycle request without silently dropping its caller completion. */
bool FLayoutBackgroundSolveLifecycleTerminalFailureTest::RunTest(const FString& Parameters)
{
	FLayoutBackgroundSolveSettings Settings;
	Settings.MaxConcurrentBackgroundLayoutSolves = 1;
	FLayoutBackgroundSolveDispatcher Dispatcher(Settings);
	Dispatcher.SetExecution(MakeUnique<FSynchronousLayoutSolveExecution>());

	FLayoutManifestPrewarmInput PrewarmInput;
	PrewarmInput.PrewarmId = TEXT("TerminalFailureMissingManifest");
	PrewarmInput.Kind = ELayoutManifestPrewarmKind::Root;
	PrewarmInput.bHasFrozenRequestManifest = false;

	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> FactoryCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> TerminalCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	TSharedRef<FString, ESPMode::ThreadSafe> TerminalReason = MakeShared<FString, ESPMode::ThreadSafe>();
	FLayoutFrozenSolveSubmissionFactory Factory = [FactoryCounter](
		const FLayoutBackgroundSolveCompletion&,
		const FLayoutBackgroundAdmissibilityPreflightResult&,
		FLayoutBackgroundSolveSubmission&,
		FString&)
	{
		FactoryCounter->Increment();
		return false;
	};

	for (const TCHAR* Event : {TEXT("begin"), TEXT("end succeeded=0"), TEXT("work"), TEXT("publication succeeded=0")})
		AddExpectedMessagePlain(FString(TEXT("[LayoutSolveDiag] request=terminal-prewarm phase=prewarm event=")) + Event, ELogVerbosity::Display);
	Dispatcher.Submit(FLayoutBackgroundLifecycleSequencer::BuildManifestPrewarmThenPreflightThenSolveSubmission(
		&Dispatcher,
		TEXT("TerminalFailurePrewarm"),
		TEXT("TerminalFailurePreflight"),
		118,
		0,
		0,
		PrewarmInput,
		MoveTemp(Factory),
		FLayoutManifestPrewarmStageComplete(),
		FLayoutAdmissibilityPreflightStageComplete(),
		[TerminalCounter, TerminalReason](const FLayoutBackgroundSolveCompletion& Completion, const FString& FailureReason)
		{
			if (Completion.LifecycleStage == ELayoutBackgroundSolveLifecycleStage::ManifestPrewarm)
			{
				TerminalCounter->Increment();
				*TerminalReason = FailureReason;
			}
		}, nullptr, TEXT("request=terminal-prewarm")));

	TestTrue(TEXT("Terminal prewarm failure publishes"), PumpDispatcherUntil(Dispatcher, [TerminalCounter]()
	{
		return TerminalCounter->GetValue() == 1;
	}));
	TestEqual(TEXT("Terminal failure invokes caller once"), TerminalCounter->GetValue(), 1);
	TestEqual(TEXT("Rejected prewarm never builds solve factory"), FactoryCounter->GetValue(), 0);
	TestTrue(TEXT("Terminal failure preserves prewarm reason"), TerminalReason->Contains(TEXT("frozen request manifest")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBackgroundSolveLifecyclePrewarmToPreflightTest,
	"PorismExtension.Layout.Async.Dispatcher.FrozenManifestPrewarmSequencesToPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBackgroundSolveLifecyclePrewarmToPreflightTest::RunTest(const FString& Parameters)
{
	FLayoutBackgroundSolveSettings Settings;
	Settings.MaxConcurrentBackgroundLayoutSolves = 1;
	FLayoutBackgroundSolveDispatcher Dispatcher(Settings);
	Dispatcher.SetExecution(MakeUnique<FSynchronousLayoutSolveExecution>());

	FLayoutWorkerSolveRequestManifest Manifest;
	Manifest.bHasSelectedModePlan = true;
	Manifest.SelectedModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::StandardRegion;
	Manifest.bHasRootPlacementSubmission = true;
	Manifest.RootPlacementShiftId = TEXT("Unshifted");
	Manifest.RootSiteCenterBlockWorldPos = FIntVector(16, 32, 48);
	Manifest.RootReservationKey = FIntPoint(1, 2);
	Manifest.CapturedSeed = 1234;

	FLayoutManifestPrewarmInput PrewarmInput;
	PrewarmInput.PrewarmId = TEXT("FrozenManifestPrewarm");
	PrewarmInput.Kind = ELayoutManifestPrewarmKind::Root;
	PrewarmInput.bHasFrozenRequestManifest = true;
	PrewarmInput.FrozenRequestManifest = Manifest;
	// Real manifest prewarm consumes descriptor-produced adapter output rather
	// than synthesizing topology from a bare manifest.
	PrewarmInput.bHasPreSubmitSnapshot = true;
	PrewarmInput.PreSubmitSnapshot.WorkerSolvePacket.bHasPrecomputedAdapterOutput = true;
	PrewarmInput.PreSubmitSnapshot.WorkerSolvePacket.PrecomputedAdapterOutput.bSucceeded = true;

	TSharedRef<bool, ESPMode::ThreadSafe> bPrewarmPublished = MakeShared<bool, ESPMode::ThreadSafe>(false);
	TSharedRef<bool, ESPMode::ThreadSafe> bPrewarmAccepted = MakeShared<bool, ESPMode::ThreadSafe>(false);
	TSharedRef<bool, ESPMode::ThreadSafe> bPreflightPublished = MakeShared<bool, ESPMode::ThreadSafe>(false);
	TSharedRef<bool, ESPMode::ThreadSafe> bPreflightAccepted = MakeShared<bool, ESPMode::ThreadSafe>(false);

	Dispatcher.Submit(FLayoutBackgroundLifecycleSequencer::BuildManifestPrewarmThenPreflightSubmission(
		&Dispatcher,
		TEXT("FrozenManifestPrewarm"),
		TEXT("FrozenManifestPreflight"),
		88,
		10,
		9,
		PrewarmInput,
		[bPrewarmPublished, bPrewarmAccepted](
			const FLayoutBackgroundSolveCompletion& Completion,
			const FLayoutManifestPrewarmResult& PrewarmResult)
		{
			*bPrewarmPublished = true;
			*bPrewarmAccepted = Completion.bWorkSucceeded
				&& Completion.LifecycleStage == ELayoutBackgroundSolveLifecycleStage::ManifestPrewarm
				&& PrewarmResult.bHasFrozenRequestManifest;
		},
		[bPreflightPublished, bPreflightAccepted](
			const FLayoutBackgroundSolveCompletion& Completion,
			const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult)
		{
			*bPreflightPublished = true;
			*bPreflightAccepted = Completion.bWorkSucceeded
				&& Completion.LifecycleStage == ELayoutBackgroundSolveLifecycleStage::AdmissibilityPreflight
				&& PreflightResult.bAdmissible;
		}));

	TestTrue(TEXT("Frozen manifest prewarm publishes"), PumpDispatcherUntil(Dispatcher, [&bPrewarmPublished]()
	{
		return *bPrewarmPublished;
	}));
	TestTrue(TEXT("Frozen manifest prewarm accepted"), *bPrewarmAccepted);

	TestTrue(TEXT("Cheap preflight publishes"), PumpDispatcherUntil(Dispatcher, [&bPreflightPublished]()
	{
		return *bPreflightPublished;
	}));
	TestTrue(TEXT("Cheap preflight accepts already-frozen standard root manifest"), *bPreflightAccepted);
	const FLayoutBackgroundSolveDiagnosticsSnapshot Snapshot = Dispatcher.GetDiagnosticsSnapshot();
	TestEqual(TEXT("Prewarm and preflight both publish accepted"), Snapshot.PublishedAccepted, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBackgroundSolveLifecyclePreflightToSolveTest,
	"PorismExtension.Layout.Async.Dispatcher.AdmissiblePreflightSubmitsFrozenSolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBackgroundSolveLifecyclePreflightToSolveTest::RunTest(const FString& Parameters)
{
	FLayoutBackgroundSolveSettings Settings;
	Settings.MaxConcurrentBackgroundLayoutSolves = 1;
	FLayoutBackgroundSolveDispatcher Dispatcher(Settings);
	Dispatcher.SetExecution(MakeUnique<FSynchronousLayoutSolveExecution>());

	FLayoutWorkerSolveRequestManifest Manifest;
	Manifest.bHasSelectedModePlan = true;
	Manifest.SelectedModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::StandardRegion;
	Manifest.bHasRootPlacementSubmission = true;
	Manifest.RootPlacementShiftId = TEXT("Unshifted");
	Manifest.RootSiteCenterBlockWorldPos = FIntVector(64, 64, 32);
	Manifest.RootReservationKey = FIntPoint(4, 4);
	Manifest.CapturedSeed = 5678;

	FLayoutBackgroundAdmissibilityPreflightInput PreflightInput;
	PreflightInput.Kind = ELayoutBackgroundAdmissibilityPreflightKind::Root;
	PreflightInput.RequestManifest = Manifest;

	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> SolveWorkCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> SolvePublishCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	FLayoutBackgroundSolveSubmission FrozenSolveSubmission = MakeCounterJob(
		TEXT("AlreadyFrozenSolve"),
		99,
		ELayoutBackgroundSolveJobTier::NearRoot,
		1,
		SolveWorkCounter,
		SolvePublishCounter);

	TSharedRef<bool, ESPMode::ThreadSafe> bPreflightPublished = MakeShared<bool, ESPMode::ThreadSafe>(false);
	TSharedRef<bool, ESPMode::ThreadSafe> bPreflightAccepted = MakeShared<bool, ESPMode::ThreadSafe>(false);
	Dispatcher.Submit(FLayoutBackgroundLifecycleSequencer::BuildAdmissibilityPreflightThenSolveSubmission(
		&Dispatcher,
		TEXT("FrozenManifestPreflight"),
		99,
		5,
		PreflightInput,
		MoveTemp(FrozenSolveSubmission),
		[bPreflightPublished, bPreflightAccepted](
			const FLayoutBackgroundSolveCompletion& Completion,
			const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult)
		{
			*bPreflightPublished = true;
			*bPreflightAccepted = Completion.bWorkSucceeded
				&& Completion.LifecycleStage == ELayoutBackgroundSolveLifecycleStage::AdmissibilityPreflight
				&& PreflightResult.bAdmissible;
		}));

	TestTrue(TEXT("Preflight publishes"), PumpDispatcherUntil(Dispatcher, [&bPreflightPublished]()
	{
		return *bPreflightPublished;
	}));
	TestTrue(TEXT("Preflight accepts frozen root manifest"), *bPreflightAccepted);
	TestTrue(TEXT("Frozen solve publishes after admissible preflight"), PumpDispatcherUntil(Dispatcher, [&SolvePublishCounter]()
	{
		return SolvePublishCounter->GetValue() == 1;
	}));
	TestEqual(TEXT("Solve work ran once"), SolveWorkCounter->GetValue(), 1);
	TestEqual(TEXT("Solve publish ran once"), SolvePublishCounter->GetValue(), 1);
	const FLayoutBackgroundSolveDiagnosticsSnapshot Snapshot = Dispatcher.GetDiagnosticsSnapshot();
	TestEqual(TEXT("Preflight and solve both publish accepted"), Snapshot.PublishedAccepted, 2);

	// Publication can grow the dispatcher table before its original record is settled.
	// Force multiple reallocations without depending on worker timing or changing solve fixtures.
	FLayoutBackgroundSolveDispatcher GrowingDispatcher(Settings);
	GrowingDispatcher.SetExecution(MakeUnique<FSynchronousLayoutSolveExecution>());
	GrowingDispatcher.SetDisableAutoPumpForTesting(true);
	const auto GrowthWork = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	const auto GrowthPublish = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	FLayoutBackgroundSolveSubmission Trigger = MakeCounterJob(
		TEXT("PublicationGrowth"), 100, ELayoutBackgroundSolveJobTier::NearRoot, 1, GrowthWork, GrowthPublish);
	Trigger.PublishOnGameThread = [&GrowingDispatcher, GrowthWork, GrowthPublish](const FLayoutBackgroundSolveCompletion&)
	{
		for (int32 Index = 0; Index < 64; ++Index)
		{
			GrowingDispatcher.Submit(MakeCounterJob(TEXT("ReentrantPublicationJob"), 101,
				ELayoutBackgroundSolveJobTier::NearRoot, 1, GrowthWork, GrowthPublish));
		}
		// Exercise the callback's own captures after the table has moved as well.
		GrowthPublish->Increment();
	};
	GrowingDispatcher.Submit(MoveTemp(Trigger));
	TestTrue(TEXT("All jobs publish after reentrant table growth"), PumpDispatcherUntil(GrowingDispatcher, [&]()
	{
		return GrowthPublish->GetValue() == 65;
	}));
	TestEqual(TEXT("Every reentrant job runs exactly once"), GrowthWork->GetValue(), 65);
	const auto GrowthSnapshot = GrowingDispatcher.GetDiagnosticsSnapshot();
	TestEqual(TEXT("Original and added records all settle accepted"), GrowthSnapshot.PublishedAccepted, 65);
	TestEqual(TEXT("No original record remains stuck publishing"), GrowthSnapshot.CompletedAwaitingPublish, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBackgroundSolveLifecycleDeferredSolveFactoryTest,
	"PorismExtension.Layout.Async.Dispatcher.AdmissiblePreflightBuildsDeferredFrozenSolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBackgroundSolveLifecycleDeferredSolveFactoryTest::RunTest(const FString& Parameters)
{
	FLayoutBackgroundSolveSettings Settings;
	Settings.MaxConcurrentBackgroundLayoutSolves = 1;
	FLayoutBackgroundSolveDispatcher Dispatcher(Settings);
	Dispatcher.SetExecution(MakeUnique<FSynchronousLayoutSolveExecution>());

	FLayoutWorkerSolveRequestManifest Manifest;
	Manifest.bHasSelectedModePlan = true;
	Manifest.SelectedModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::StandardRegion;
	Manifest.bHasRootPlacementSubmission = true;
	Manifest.RootPlacementShiftId = TEXT("Unshifted");
	Manifest.RootSiteCenterBlockWorldPos = FIntVector(96, 64, 32);
	Manifest.RootReservationKey = FIntPoint(6, 4);
	Manifest.CapturedSeed = 6789;

	FLayoutBackgroundAdmissibilityPreflightInput PreflightInput;
	PreflightInput.Kind = ELayoutBackgroundAdmissibilityPreflightKind::Root;
	PreflightInput.RequestManifest = Manifest;

	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> FactoryCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> SolveWorkCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> SolvePublishCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	FLayoutFrozenSolveSubmissionFactory Factory = [FactoryCounter, SolveWorkCounter, SolvePublishCounter](
		const FLayoutBackgroundSolveCompletion& PreflightCompletion,
		const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult,
		FLayoutBackgroundSolveSubmission& OutSubmission,
		FString& OutFailureReason)
	{
		if (PreflightCompletion.LifecycleStage != ELayoutBackgroundSolveLifecycleStage::AdmissibilityPreflight
			|| !PreflightCompletion.bWorkSucceeded
			|| !PreflightResult.bAdmissible)
		{
			OutFailureReason = TEXT("Factory requires successful admissibility preflight.");
			return false;
		}
		FactoryCounter->Increment();
		OutSubmission = MakeCounterJob(
			TEXT("DeferredFrozenSolve"),
			101,
			ELayoutBackgroundSolveJobTier::NearRoot,
			1,
			SolveWorkCounter,
			SolvePublishCounter);
		return true;
	};

	Dispatcher.Submit(FLayoutBackgroundLifecycleSequencer::BuildAdmissibilityPreflightThenSolveSubmission(
		&Dispatcher,
		TEXT("DeferredPreflight"),
		101,
		5,
		PreflightInput,
		MoveTemp(Factory)));

	TestTrue(TEXT("Deferred solve publishes after admissible preflight"), PumpDispatcherUntil(Dispatcher, [&SolvePublishCounter]()
	{
		return SolvePublishCounter->GetValue() == 1;
	}));
	TestEqual(TEXT("Factory ran once after preflight"), FactoryCounter->GetValue(), 1);
	TestEqual(TEXT("Deferred solve work ran once"), SolveWorkCounter->GetValue(), 1);
	TestEqual(TEXT("Deferred solve publish ran once"), SolvePublishCounter->GetValue(), 1);
	const FLayoutBackgroundSolveDiagnosticsSnapshot Snapshot = Dispatcher.GetDiagnosticsSnapshot();
	TestEqual(TEXT("Preflight and deferred solve both publish accepted"), Snapshot.PublishedAccepted, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBackgroundSolveRetiresTerminalJobsTest,
	"PorismExtension.Layout.Async.Dispatcher.RetiresTerminalJobsAndHoldsExpiredWorkerSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBackgroundSolveRetiresTerminalJobsTest::RunTest(const FString& Parameters)
{
	FLayoutBackgroundSolveSettings Settings;
	Settings.MaxConcurrentBackgroundLayoutSolves = 1;
	FLayoutBackgroundSolveDispatcher Dispatcher(Settings);
	Dispatcher.SetExecution(MakeUnique<FSynchronousLayoutSolveExecution>());
	Dispatcher.SetDisableAutoPumpForTesting(true);
	const auto WorkCount = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	const auto PublishCount = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	for (int32 Index = 0; Index < 1024; ++Index)
	{
		Dispatcher.Submit(MakeCounterJob(TEXT("Traversal"), Index, ELayoutBackgroundSolveJobTier::NearRoot,
			0, WorkCount, PublishCount));
		Dispatcher.Tick();
		if (!TestEqual(TEXT("Completed traversal jobs leave no retained history"),
			Dispatcher.GetDiagnosticsSnapshot().RetainedJobRecords, 0))
		{
			return false;
		}
	}
	TestEqual(TEXT("Cumulative diagnostics survive record retirement"),
		Dispatcher.GetDiagnosticsSnapshot().PublishedAccepted, 1024);

	const FLayoutBackgroundSolveHandle Running = Dispatcher.Submit(MakeCounterJob(
		TEXT("ExpiringWorker"), 2048, ELayoutBackgroundSolveJobTier::NearRoot, 0, WorkCount, PublishCount));
	TSharedPtr<int32, ESPMode::ThreadSafe> Payload = MakeShared<int32, ESPMode::ThreadSafe>(1);
	const TWeakPtr<int32, ESPMode::ThreadSafe> WeakPayload = Payload;
	FLayoutBackgroundSolveSubmission Waiting;
	Waiting.Work = [Payload](const FLayoutSolveCancellationToken&, FString&) { return *Payload == 1; };
	Waiting.PublishOnGameThread = [Payload](const FLayoutBackgroundSolveCompletion&) { check(*Payload == 1); };
	const FLayoutBackgroundSolveHandle WaitingHandle = Dispatcher.Submit(MoveTemp(Waiting));
	Payload.Reset();
	TestTrue(TEXT("Queued callbacks own their payload"), WeakPayload.IsValid());
	Dispatcher.Cancel(WaitingHandle);
	TestFalse(TEXT("Canceling queued work releases both work and publish captures"), WeakPayload.IsValid());
	TestTrue(TEXT("Running group remains pending"), Dispatcher.HasPendingGroup(Running.LayoutGroupId));
	Dispatcher.Expire(Running);
	TestFalse(TEXT("Expired worker is no longer pending publication"), Dispatcher.HasPendingGroup(Running.LayoutGroupId));
	TestTrue(TEXT("Expired worker still owns retained captures"), Dispatcher.HasRetainedGroup(Running.LayoutGroupId));
	TestTrue(TEXT("Expired worker remains protected from capacity eviction"), Dispatcher.HasRunningGroup(Running.LayoutGroupId));
	Dispatcher.Submit(MakeCounterJob(TEXT("NextWorker"), 2049, ELayoutBackgroundSolveJobTier::NearRoot,
		0, WorkCount, PublishCount));
	const FLayoutBackgroundSolveDiagnosticsSnapshot ExpiredSnapshot = Dispatcher.GetDiagnosticsSnapshot();
	TestEqual(TEXT("Expired executing worker still occupies its slot"), ExpiredSnapshot.InFlightWorkers, 1);
	TestEqual(TEXT("New work waits until the expired worker actually completes"), ExpiredSnapshot.WaitingForDispatch, 1);
	Dispatcher.Tick();
	Dispatcher.Tick();
	const FLayoutBackgroundSolveDiagnosticsSnapshot SettledSnapshot = Dispatcher.GetDiagnosticsSnapshot();
	TestEqual(TEXT("No retained records after cancellation and late completion settle"), SettledSnapshot.RetainedJobRecords, 0);
	TestFalse(TEXT("Retirement releases group ownership"), Dispatcher.HasRetainedGroup(Running.LayoutGroupId));
	TestFalse(TEXT("Retirement releases running ownership"), Dispatcher.HasRunningGroup(Running.LayoutGroupId));
	TestEqual(TEXT("Retired cancellation stays visible in diagnostics"), SettledSnapshot.Canceled, 1);
	TestEqual(TEXT("Retired expiry stays visible in diagnostics"), SettledSnapshot.Expired, 1);
	TestEqual(TEXT("Expired worker completion was discarded"), SettledSnapshot.DiscardedLateCompletions, 1);
	TestEqual(TEXT("Only the new worker ran after the traversal jobs"), WorkCount->GetValue(), 1025);
	TestEqual(TEXT("Only the new worker published after the traversal jobs"), PublishCount->GetValue(), 1025);
	TestTrue(TEXT("HUD distinguishes cumulative stages from placements"), SettledSnapshot.ToDebugString().Contains(TEXT("not placements")));

	FLayoutBackgroundSolveSubmission FirstStage = MakeCounterJob(TEXT("FirstStage"), 4096,
		ELayoutBackgroundSolveJobTier::Continuation, 0, WorkCount, PublishCount);
	FirstStage.PublishOnGameThread = [&Dispatcher, WorkCount, PublishCount](const FLayoutBackgroundSolveCompletion&)
	{
		Dispatcher.Submit(MakeCounterJob(TEXT("Successor"), 4096,
			ELayoutBackgroundSolveJobTier::Continuation, 0, WorkCount, PublishCount));
	};
	Dispatcher.Submit(MoveTemp(FirstStage));
	Dispatcher.Tick();
	TestTrue(TEXT("Retired first stage does not hide pending successor"), Dispatcher.HasPendingGroup(4096));
	Dispatcher.Tick();
	TestFalse(TEXT("Completed lifecycle is no longer pending"), Dispatcher.HasPendingGroup(4096));

	const int32 PublishedBeforeReset = PublishCount->GetValue();
	const auto OldWorld = Dispatcher.Submit(MakeCounterJob(TEXT("OldWorld"), 8192,
		ELayoutBackgroundSolveJobTier::NearRoot, 0, WorkCount, PublishCount));
	Dispatcher.Submit(MakeCounterJob(TEXT("OldWorldQueued"), 8193,
		ELayoutBackgroundSolveJobTier::NearRoot, 0, WorkCount, PublishCount));
	Dispatcher.CancelAll();
	TestEqual(TEXT("World reset retains canceled worker against concurrency"), Dispatcher.GetDiagnosticsSnapshot().InFlightWorkers, 1);
	TestFalse(TEXT("Old-world group cannot publish"), Dispatcher.HasPendingGroup(8192));
	const auto NewWorld = Dispatcher.Submit(MakeCounterJob(TEXT("NewWorld"), 8192,
		ELayoutBackgroundSolveJobTier::NearRoot, 0, WorkCount, PublishCount));
	TestTrue(TEXT("Reused group has a new job identity"), NewWorld.JobId > OldWorld.JobId);
	Dispatcher.Tick();
	Dispatcher.Tick();
	TestEqual(TEXT("Only new-world completion publishes after reset"), PublishCount->GetValue(), PublishedBeforeReset + 1);
	TestEqual(TEXT("World reset releases retired work"), Dispatcher.GetDiagnosticsSnapshot().RetainedJobRecords, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBackgroundSolveGroupCancellationTest,
	"PorismExtension.Layout.Async.Dispatcher.GroupCancellationCancelsWaitingAndDiscardsLateWork",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBackgroundSolveGroupCancellationTest::RunTest(const FString& Parameters)
{
	FLayoutBackgroundSolveSettings Settings;
	Settings.MaxConcurrentBackgroundLayoutSolves = 1;
	FLayoutBackgroundSolveDispatcher Dispatcher(Settings);
	Dispatcher.SetExecution(MakeUnique<FSynchronousLayoutSolveExecution>());
	Dispatcher.SetDisableAutoPumpForTesting(true);

	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> WorkCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> PublishCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	Dispatcher.Submit(MakeCounterJob(TEXT("CancelableA"), 42, ELayoutBackgroundSolveJobTier::NearRoot, 0, WorkCounter, PublishCounter));
	Dispatcher.Submit(MakeCounterJob(TEXT("CancelableB"), 42, ELayoutBackgroundSolveJobTier::NearRoot, 0, WorkCounter, PublishCounter));
	Dispatcher.CancelGroup(42);

	PumpDispatcherUntil(Dispatcher, [&Dispatcher]()
	{
		const FLayoutBackgroundSolveDiagnosticsSnapshot Snapshot = Dispatcher.GetDiagnosticsSnapshot();
		return Snapshot.Canceled >= 1;
	});

	const FLayoutBackgroundSolveDiagnosticsSnapshot Snapshot = Dispatcher.GetDiagnosticsSnapshot();
	TestTrue(TEXT("Group cancellation records canceled jobs"), Snapshot.Canceled >= 1);
	TestEqual(TEXT("Canceled group does not publish accepted completions"), PublishCounter->GetValue(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBackgroundSolveExpiryTest,
	"PorismExtension.Layout.Async.Dispatcher.ExpiredJobsDoNotDispatchOrPublish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBackgroundSolveExpiryTest::RunTest(const FString& Parameters)
{
	FLayoutBackgroundSolveSettings Settings;
	Settings.MaxConcurrentBackgroundLayoutSolves = 1;
	FLayoutBackgroundSolveDispatcher Dispatcher(Settings);
	Dispatcher.SetExecution(MakeUnique<FSynchronousLayoutSolveExecution>());
	Dispatcher.SetDisableAutoPumpForTesting(true);

	TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> ReleaseGate = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
	TSharedRef<TArray<FString>, ESPMode::ThreadSafe> PublishOrder = MakeShared<TArray<FString>, ESPMode::ThreadSafe>();
	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> PublishCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	const FLayoutBackgroundSolveHandle RunningHandle = Dispatcher.Submit(MakeOrderedJob(TEXT("Running"), 1, ELayoutBackgroundSolveJobTier::NearRoot, 0, PublishOrder, PublishCounter, ReleaseGate));
	const FLayoutBackgroundSolveHandle WaitingHandle = Dispatcher.Submit(MakeOrderedJob(TEXT("Waiting"), 2, ELayoutBackgroundSolveJobTier::NearRoot, 0, PublishOrder, PublishCounter));
	Dispatcher.Expire(WaitingHandle);
	Dispatcher.Expire(RunningHandle);
	*ReleaseGate = true;

	PumpDispatcherUntil(Dispatcher, [&Dispatcher]()
	{
		const FLayoutBackgroundSolveDiagnosticsSnapshot Snapshot = Dispatcher.GetDiagnosticsSnapshot();
		return Snapshot.Expired >= 2 && Snapshot.DiscardedLateCompletions >= 1;
	}, 800);

	const FLayoutBackgroundSolveDiagnosticsSnapshot Snapshot = Dispatcher.GetDiagnosticsSnapshot();
	TestEqual(TEXT("Waiting and running jobs stay expired"), Snapshot.Expired, 2);
	TestTrue(TEXT("Running expired completion is discarded as late"), Snapshot.DiscardedLateCompletions >= 1);
	TestEqual(TEXT("Expired jobs do not publish"), PublishCounter->GetValue(), 0);
	TestEqual(TEXT("Expired jobs never enter publish order"), PublishOrder->Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBackgroundSolveStablePriorityOrderTest,
	"PorismExtension.Layout.Async.Dispatcher.StablePriorityOrderPrefersProductionTiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBackgroundSolveStablePriorityOrderTest::RunTest(const FString& Parameters)
{
	FLayoutBackgroundSolveSettings Settings;
	Settings.MaxConcurrentBackgroundLayoutSolves = 1;
	FLayoutBackgroundSolveDispatcher Dispatcher(Settings);
	Dispatcher.SetExecution(MakeUnique<FSynchronousLayoutSolveExecution>());
	Dispatcher.SetDisableAutoPumpForTesting(true);

	TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> ReleaseGate = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
	TSharedRef<TArray<FString>, ESPMode::ThreadSafe> PublishOrder = MakeShared<TArray<FString>, ESPMode::ThreadSafe>();
	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> PublishCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	Dispatcher.Submit(MakeOrderedJob(TEXT("Blocker"), 1, ELayoutBackgroundSolveJobTier::InProgressLayoutGroup, 0, PublishOrder, PublishCounter, ReleaseGate));
	Dispatcher.Submit(MakeOrderedJob(TEXT("Warmup"), 2, ELayoutBackgroundSolveJobTier::Warmup, 100, PublishOrder, PublishCounter));
	Dispatcher.Submit(MakeOrderedJob(TEXT("NearRoot"), 3, ELayoutBackgroundSolveJobTier::NearRoot, 0, PublishOrder, PublishCounter));
	Dispatcher.Submit(MakeOrderedJob(TEXT("Continuation"), 4, ELayoutBackgroundSolveJobTier::Continuation, 0, PublishOrder, PublishCounter));
	Dispatcher.Submit(MakeOrderedJob(TEXT("ParentResume"), 5, ELayoutBackgroundSolveJobTier::ParentResume, 0, PublishOrder, PublishCounter));
	Dispatcher.Submit(MakeOrderedJob(TEXT("InProgress"), 6, ELayoutBackgroundSolveJobTier::InProgressLayoutGroup, -10, PublishOrder, PublishCounter));
	*ReleaseGate = true;

	const bool bCompleted = PumpDispatcherUntil(Dispatcher, [&PublishCounter]()
	{
		return PublishCounter->GetValue() == 6;
	}, 800);
	TestTrue(TEXT("All priority-order jobs publish"), bCompleted);
	const TArray<FString> ExpectedOrder = {
		TEXT("Blocker"),
		TEXT("InProgress"),
		TEXT("ParentResume"),
		TEXT("Continuation"),
		TEXT("NearRoot"),
		TEXT("Warmup")
	};
	TestEqual(TEXT("Dispatcher publishes by required production tier order"), *PublishOrder, ExpectedOrder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBackgroundSolveMovingCenterPriorityTest,
	"PorismExtension.Layout.Async.Dispatcher.RefreshesWaitingGroupPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBackgroundSolveMovingCenterPriorityTest::RunTest(const FString& Parameters)
{
	FLayoutBackgroundSolveSettings Settings;
	Settings.MaxConcurrentBackgroundLayoutSolves = 1;
	FLayoutBackgroundSolveDispatcher Dispatcher(Settings);
	Dispatcher.SetExecution(MakeUnique<FSynchronousLayoutSolveExecution>());
	Dispatcher.SetDisableAutoPumpForTesting(true);
	const auto Order = MakeShared<TArray<FString>, ESPMode::ThreadSafe>();
	const auto Count = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	// Submit dispatches immediately even when auto-pumping is disabled. Hold the slot
	// until Tick drains this completion so both target groups are genuinely queued.
	Dispatcher.Submit(MakeOrderedJob(TEXT("Blocker"), 99, ELayoutBackgroundSolveJobTier::NearRoot, 0, Order, Count));
	const FLayoutBackgroundSolveHandle FirstStage = Dispatcher.Submit(MakeOrderedJob(TEXT("OldNear"), 1, ELayoutBackgroundSolveJobTier::NearRoot, 0, Order, Count));
	Dispatcher.Submit(MakeOrderedJob(TEXT("NewNear"), 2, ELayoutBackgroundSolveJobTier::NearRoot, -100, Order, Count));
	Dispatcher.UpdateWaitingGroupPriorities({{1, -100}, {2, 0}});
	TestTrue(TEXT("Reprioritized jobs publish"), PumpDispatcherUntil(Dispatcher, [&Count]() { return Count->GetValue() == 3; }, 800));
	TestEqual(TEXT("Moved center changes queued dispatch order"), *Order, TArray<FString>{TEXT("Blocker"), TEXT("NewNear"), TEXT("OldNear")});
	Dispatcher.Submit(MakeOrderedJob(TEXT("Successor"), FirstStage.LayoutGroupId, ELayoutBackgroundSolveJobTier::NearRoot, 0, Order, Count));
	Dispatcher.CancelGroup(FirstStage.LayoutGroupId);
	Dispatcher.Tick();
	TestEqual(TEXT("Group cancellation stops successor after original stage retires"), Count->GetValue(), 3);
	TestEqual(TEXT("Canceled successor releases its record"), Dispatcher.GetDiagnosticsSnapshot().RetainedJobRecords, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBackgroundSolvePrerequisiteWaitReleasesWorkerSlotTest,
	"PorismExtension.Layout.Async.Dispatcher.ParentPrerequisiteWaitDoesNotOccupyWorkerSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBackgroundSolvePrerequisiteWaitReleasesWorkerSlotTest::RunTest(const FString& Parameters)
{
	FLayoutBackgroundSolveSettings Settings;
	Settings.MaxConcurrentBackgroundLayoutSolves = 1;
	FLayoutBackgroundSolveDispatcher Dispatcher(Settings);
	Dispatcher.SetExecution(MakeUnique<FSynchronousLayoutSolveExecution>());
	Dispatcher.SetDisableAutoPumpForTesting(true);

	TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> ReleaseGate = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
	TSharedRef<TArray<FString>, ESPMode::ThreadSafe> PublishOrder = MakeShared<TArray<FString>, ESPMode::ThreadSafe>();
	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> PublishCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	Dispatcher.Submit(MakeOrderedJob(TEXT("Blocker"), 1, ELayoutBackgroundSolveJobTier::InProgressLayoutGroup, 0, PublishOrder, PublishCounter, ReleaseGate));
	const FLayoutBackgroundSolveHandle WaitingHandle = Dispatcher.Submit(MakeOrderedJob(TEXT("Prereq"), 2, ELayoutBackgroundSolveJobTier::ParentResume, 0, PublishOrder, PublishCounter));
	Dispatcher.MarkWaitingForPrerequisites(WaitingHandle);
	Dispatcher.Submit(MakeOrderedJob(TEXT("Runnable"), 3, ELayoutBackgroundSolveJobTier::NearRoot, 0, PublishOrder, PublishCounter));
	*ReleaseGate = true;

	const bool bRunnableCompleted = PumpDispatcherUntil(Dispatcher, [&PublishCounter]()
	{
		return PublishCounter->GetValue() == 2;
	}, 800);
	TestTrue(TEXT("Runnable job publishes while prerequisite job waits"), bRunnableCompleted);
	TestEqual(TEXT("Prerequisite-waiting job did not occupy the freed worker slot"), (*PublishOrder)[1], FString(TEXT("Runnable")));
	FLayoutBackgroundSolveDiagnosticsSnapshot WaitingSnapshot = Dispatcher.GetDiagnosticsSnapshot();
	TestEqual(TEXT("Prerequisite job remains parked after runnable publishes"), WaitingSnapshot.WaitingForPrerequisites, 1);

	Dispatcher.MarkPrerequisitesSatisfied(WaitingHandle);
	const bool bPrereqCompleted = PumpDispatcherUntil(Dispatcher, [&PublishCounter]()
	{
		return PublishCounter->GetValue() == 3;
	}, 800);
	TestTrue(TEXT("Prerequisite job publishes after prerequisites are satisfied"), bPrereqCompleted);
	TestEqual(TEXT("Prerequisite job publishes last"), (*PublishOrder)[2], FString(TEXT("Prereq")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorkerSolvePacketRootCaptureTest,
	"PorismExtension.Layout.Async.WorkerPacket.RootCapturesPointerFreePlanningExplicitAndFallbackMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorkerSolvePacketContinuationAndChildCaptureTest,
	"PorismExtension.Layout.Async.WorkerPacket.ContinuationAndChildHandoffCapturePointerFreeMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildSolveHandoffCertificateGuardTest,
	"PorismExtension.Layout.Async.WorkerPacket.ChildHandoffRejectsStaleCertificateHash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorkerSolvePacketRootCaptureTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* const Profile = NewObject<ULayoutProfileAsset>();
	ULayoutRegionContentSetAsset* const ContentSet = NewObject<ULayoutRegionContentSetAsset>();
	
	FLayoutWorldBindingRuntimeView RuntimeView;
	RuntimeView.PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	RuntimeView.BindingId = TEXT("Binding.WorkerPacket.Root");
	RuntimeView.CandidateId = TEXT("Candidate.WorkerPacket.Root");
	RuntimeView.LayoutProfile = Profile;
	RuntimeView.ContentSet = ContentSet;
	RuntimeView.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
	RuntimeView.TemplatePlacementZOffsetBlocks = 9;

	FPlannedLayoutSiteRecord PendingRecord;
	FLayoutPlannedSiteReservationSourceSelection ReservationSelection;
	ReservationSelection.ReservationKey = FIntPoint(11, 17);
	ReservationSelection.SiteCenterBlockWorldPos = FIntVector(32, 48, 64);
	PendingRecord.SetPlannedSiteReservationSourceSelection(ReservationSelection);
	FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata;
	LifecycleMetadata.StableRecordKey = TEXT("Planning.WorkerPacket.Root");
	PendingRecord.SetPlannedSiteLifecycleMetadata(LifecycleMetadata);
	FLayoutSiteSolveSourceSelection SolveSourceSelection;
	SolveSourceSelection.SolveSeed = 2719;
	PendingRecord.SetSiteSolveSourceSelection(SolveSourceSelection);
	FLayoutWorldBindingSiteFrontendSelection FrontendSelection;
	FrontendSelection.BiomeRowName = TEXT("WorkerPacketBiome");
	PendingRecord.SetWorldBindingFrontendSelection(FrontendSelection);

	FLayoutRegionSolveRequest Request;
	Request.Seed = 2719;
	Request.TemplatePlacementZOffsetBlocks = 9;
	Request.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	Request.WorldBindingId = RuntimeView.BindingId;
	Request.ProfileSnapshot.SourceProfile = Profile;
	Request.ContentSetSnapshot.SourceContentSet = ContentSet;

	FLayoutWorkerSolvePacket PlanningPacket = FLayoutWorkerSolvePacket::CapturePlanningRoot(
		TEXT("PlanningRoot WorkerPacket"),
		RuntimeView,
		PendingRecord,
		2);
	PlanningPacket.RequestManifest = FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(Request);
	PlanningPacket.bHasRequestManifest = true;
	FLayoutWorkerSolvePacket ExplicitPacket = FLayoutWorkerSolvePacket::CaptureExplicitPreviewRoot(
		TEXT("ExplicitRoot WorkerPacket"),
		RuntimeView,
		FIntVector(80, 96, 112),
		7331,
		1,
		TEXT("ExplicitBiome"));
	Request.Seed = 7331;
	ExplicitPacket.RequestManifest = FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(Request);
	ExplicitPacket.bHasRequestManifest = true;
	FLayoutWorkerSolvePacket FallbackPacket = FLayoutWorkerSolvePacket::CaptureObservedFallbackRoot(
		TEXT("ObservedFallback WorkerPacket"),
		RuntimeView,
		FIntVector(144, 160, 176),
		8128,
		3,
		TEXT("FallbackBiome"));
	Request.Seed = 8128;
	FallbackPacket.RequestManifest = FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(Request);
	FallbackPacket.bHasRequestManifest = true;

	FString FailureReason;
	TestTrue(TEXT("Planning worker packet validates pointer-free"), PlanningPacket.ValidateNoLiveObjectCarriers(FailureReason));
	TestTrue(TEXT("Explicit worker packet validates pointer-free"), ExplicitPacket.ValidateNoLiveObjectCarriers(FailureReason));
	TestTrue(TEXT("Observed fallback worker packet validates pointer-free"), FallbackPacket.ValidateNoLiveObjectCarriers(FailureReason));
	TestEqual(TEXT("Planning worker packet tags planning-root kind"), PlanningPacket.Kind, ELayoutWorkerSolvePacketKind::PlanningRoot);
	TestEqual(TEXT("Planning worker packet keeps solve seed"), PlanningPacket.SolveSeed, 2719);
	TestEqual(TEXT("Planning worker packet keeps attempt index"), PlanningPacket.AttemptIndex, 2);
	TestEqual(TEXT("Planning worker packet keeps stable record key"), PlanningPacket.StableTextKey, FString(TEXT("Planning.WorkerPacket.Root")));
	TestEqual(TEXT("Planning worker packet keeps primary location"), PlanningPacket.PrimaryBlockWorldPos, ReservationSelection.SiteCenterBlockWorldPos);
	TestEqual(TEXT("Planning worker packet keeps biome row"), PlanningPacket.BiomeRowName, FrontendSelection.BiomeRowName);
	TestEqual(TEXT("Planning worker packet keeps runtime binding id"), PlanningPacket.RuntimeSnapshot.BindingId, RuntimeView.BindingId);
	TestEqual(TEXT("Planning worker packet manifest keeps request binding id"), PlanningPacket.RequestManifest.WorldBindingId, Request.WorldBindingId);
	TestEqual(TEXT("Explicit worker packet manifest keeps request binding id"), ExplicitPacket.RequestManifest.WorldBindingId, Request.WorldBindingId);
	TestEqual(TEXT("Fallback worker packet manifest keeps request binding id"), FallbackPacket.RequestManifest.WorldBindingId, Request.WorldBindingId);
	TestEqual(TEXT("Explicit worker packet tags explicit-preview kind"), ExplicitPacket.Kind, ELayoutWorkerSolvePacketKind::ExplicitPreviewRoot);
	TestEqual(TEXT("Explicit worker packet keeps explicit solve seed"), ExplicitPacket.SolveSeed, 7331);
	TestEqual(TEXT("Explicit worker packet keeps explicit site center"), ExplicitPacket.PrimaryBlockWorldPos, FIntVector(80, 96, 112));
	TestEqual(TEXT("Observed fallback worker packet tags fallback kind"), FallbackPacket.Kind, ELayoutWorkerSolvePacketKind::ObservedFallbackRoot);
	TestEqual(TEXT("Observed fallback worker packet keeps fallback site center"), FallbackPacket.PrimaryBlockWorldPos, FIntVector(144, 160, 176));
	return true;
}

namespace
{
	/** Mirrors the fixture's unrotated stage-neutral topology into the parent before certificate capture. */
	void SetStageNeutralTestMapping(FLayoutChildSolveHandoff& Handoff, const TArray<FLayoutPlannedCell>& Cells)
	{
		auto& Mapping = Handoff.DirectCommitment.StageMapping;
		Mapping.MappingId = TEXT("StageMapping.AsyncFixture");
		Mapping.ParentRegionCellOffset = Handoff.ChildRegionCellOffset;
		Mapping.ChildLocalPlannedCells = Cells;
		Mapping.ParentTranslatedPlannedCells = Cells;
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			auto& Cell = Mapping.Cells.AddDefaulted_GetRef();
			Cell.SourceChildCell = Cell.MappedChildCell = Cells[Index].Cell;
			Cell.ParentCell = Cells[Index].Cell + Handoff.ChildRegionCellOffset;
			Cell.ModuleLevelIndex = Cells[Index].Cell.Z;
			Mapping.ParentTranslatedPlannedCells[Index].Cell = Cell.ParentCell;
		}
	}
}

bool FLayoutWorkerSolvePacketContinuationAndChildCaptureTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* const Profile = NewObject<ULayoutProfileAsset>();
	ULayoutRegionContentSetAsset* const ContentSet = NewObject<ULayoutRegionContentSetAsset>();
	
	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = TEXT("Binding.WorkerPacket.Continuation");
	ConnectorRecord.ContinuationFamilyCandidateId = TEXT("ContinuationCandidate");
	ConnectorRecord.PlacementKind = ELayoutWorldBindingPlacementKind::SurfacePath;
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.StartEndpointBlockWorldPos = FIntVector(10, 20, 30);
	ConnectorRecord.EndEndpointBlockWorldPos = FIntVector(40, 50, 60);
	ConnectorRecord.SolveSeed = 9216;
	ConnectorRecord.BiomeRowName = TEXT("ContinuationBiome");
	ConnectorRecord.RootSolveId = TEXT("ContinuationSolve");

	FLayoutRegionSolveRequest Request;
	Request.Seed = 9216;
	Request.RootPlacementKind = ELayoutWorldBindingPlacementKind::SurfacePath;
	Request.ProfileSnapshot.SourceProfile = Profile;
	Request.ContentSetSnapshot.SourceContentSet = ContentSet;

	FLayoutWorkerSolvePacket ContinuationPacket = FLayoutWorkerSolvePacket::CaptureContinuation(
		TEXT("Continuation WorkerPacket"),
		ConnectorRecord,
		77,
		4);
	ContinuationPacket.RequestManifest = FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(Request);
	ContinuationPacket.bHasRequestManifest = true;
	FLayoutChildSolveHandoff ChildSolveHandoff;
	ChildSolveHandoff.ParentRegionDebugPath = TEXT("Parent/Region");
	ChildSolveHandoff.ChildRegionDebugPath = TEXT("Child/Region");
	ChildSolveHandoff.ParentArtifactOrResumeId = TEXT("ParentResume");
	ChildSolveHandoff.StableChildKey = TEXT("Child/Packet");
	ChildSolveHandoff.AttemptIndex = 1;
	ChildSolveHandoff.ChildRegionCellOffset = FIntVector(5, 6, 7);
	ChildSolveHandoff.bHasChildBlockWorldAnchor = true;
	ChildSolveHandoff.ChildBlockWorldAnchor = FIntVector(80, 96, 112);
	ChildSolveHandoff.ContentMetadata.SourceContentEntryId = TEXT("Entry.Child");
	ChildSolveHandoff.ContentMetadata.ChildProfilePath = TSoftObjectPtr<ULayoutProfileAsset>(Profile).ToSoftObjectPath();
	ChildSolveHandoff.ContentMetadata.PlacementZone = ELayoutPlacementZone::Interior;
	ChildSolveHandoff.ContentMetadata.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::SpecificLevel;
	ChildSolveHandoff.ContentMetadata.SpecificLevel = 1;
	ChildSolveHandoff.ContentMetadata.bOptional = false;
	ChildSolveHandoff.ContentMetadata.bContributesHostVerticalAccess = true;
	ChildSolveHandoff.ChildCapabilityEnvelope.RegionDebugPath = ChildSolveHandoff.ChildRegionDebugPath;
	ChildSolveHandoff.ChildCapabilityEnvelope.SnapshotId = TEXT("ChildEnvelope");
	FLayoutChildCapabilityEndpoint& EndpointCapability = ChildSolveHandoff.ChildCapabilityEnvelope.EndpointCapabilities.AddDefaulted_GetRef();
	EndpointCapability.CapabilityId = TEXT("Endpoint.Cap");
	EndpointCapability.LocalCell = FIntVector(1, 0, 0);
	ChildSolveHandoff.DirectCommitment.ParentRegionDebugPath = ChildSolveHandoff.ParentRegionDebugPath;
	ChildSolveHandoff.DirectCommitment.ChildRegionDebugPath = ChildSolveHandoff.ChildRegionDebugPath;
	ChildSolveHandoff.DirectCommitment.bAllowsChildTraversalBridgeForCommittedContacts = true;
	ChildSolveHandoff.DirectCommitment.NegotiatedResponsibilityContract.ParentRegionDebugPath = ChildSolveHandoff.ParentRegionDebugPath;
	ChildSolveHandoff.DirectCommitment.NegotiatedResponsibilityContract.ChildRegionDebugPath = ChildSolveHandoff.ChildRegionDebugPath;
	ChildSolveHandoff.DirectCommitment.NegotiatedResponsibilityContract.HostVerticalAccessResponsibility = ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned;
	ChildSolveHandoff.DelegatedZoneFeatureRequirementIds = {TEXT("Feature.Req")};
	ChildSolveHandoff.DelegatedClosureRequirementIds = {TEXT("Closure.Req")};
	ChildSolveHandoff.bRequiresCertifiedWitnessBundle = true;
	ChildSolveHandoff.CertifiedWitnessBundle.SourceContentEntryId = TEXT("Entry.Child");
	ChildSolveHandoff.CertifiedWitnessBundle.StableChildId = TEXT("Child.Stable");
	ChildSolveHandoff.CertifiedWitnessBundle.BranchId = TEXT("Branch.A");
	ChildSolveHandoff.CertifiedWitnessBundle.SelectedEndpointCapabilityIds = {TEXT("Endpoint.Cap")};
	ChildSolveHandoff.CertifiedWitnessBundle.AssertionIds = {TEXT("Assert.Child.Endpoint")};
	SetStageNeutralTestMapping(ChildSolveHandoff, {
		{FIntVector::ZeroValue, ELayoutCellIntent::Interior},
		{EndpointCapability.LocalCell, ELayoutCellIntent::Entry}});
	ChildSolveHandoff.RefreshProofCertificate(TEXT("ChildCert"));

	Request.Seed = 5150;
	FLayoutWorkerSolvePacket ChildPacket = FLayoutWorkerSolvePacket::CaptureChild(
		TEXT("Child WorkerPacket"),
		FLayoutWorldBindingRuntimeSnapshot::CaptureFromConnectorRecord(ConnectorRecord),
		ChildSolveHandoff,
		5150,
		1);
	ChildPacket.RequestManifest = FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(Request);
	ChildPacket.bHasRequestManifest = true;

	FString FailureReason;
	TestTrue(TEXT("Continuation worker packet validates pointer-free"), ContinuationPacket.ValidateNoLiveObjectCarriers(FailureReason));
	TestTrue(TEXT("Child handoff validates pointer-free"), ChildSolveHandoff.ValidateNoLiveObjectCarriers(FailureReason));
	TestTrue(TEXT("Child worker packet with certified handoff validates pointer-free"), ChildPacket.ValidateNoLiveObjectCarriers(FailureReason));
	TestEqual(TEXT("Continuation worker packet tags continuation kind"), ContinuationPacket.Kind, ELayoutWorkerSolvePacketKind::Continuation);
	TestEqual(TEXT("Continuation worker packet keeps connector key"), ContinuationPacket.StableNumericKey, static_cast<uint64>(77));
	TestEqual(TEXT("Continuation worker packet keeps start endpoint"), ContinuationPacket.PrimaryBlockWorldPos, ConnectorRecord.StartEndpointBlockWorldPos);
	TestEqual(TEXT("Continuation worker packet keeps end endpoint"), ContinuationPacket.SecondaryBlockWorldPos, ConnectorRecord.EndEndpointBlockWorldPos);
	TestEqual(TEXT("Child worker packet tags child kind"), ChildPacket.Kind, ELayoutWorkerSolvePacketKind::Child);
	TestEqual(TEXT("Child worker packet keeps child anchor"), ChildPacket.PrimaryBlockWorldPos, FIntVector(80, 96, 112));
	TestEqual(TEXT("Child worker packet keeps child solve seed"), ChildPacket.SolveSeed, 5150);
	TestEqual(TEXT("Child worker packet keeps child stable key"), ChildPacket.StableTextKey, FString(TEXT("Child/Packet")));
	TestEqual(TEXT("Child worker packet keeps child certificate id"), ChildPacket.FrozenChildHandoff.ProofCertificate.CertificateId, FLayoutId(TEXT("ChildCert")));
	if (TestEqual(TEXT("Child packet retains both stage-mapped cells"), ChildPacket.FrozenChildHandoff.DirectCommitment.StageMapping.Cells.Num(), 2))
	{
		TestEqual(TEXT("Child packet captures mapped endpoint in parent coordinates"),
			ChildPacket.FrozenChildHandoff.DirectCommitment.StageMapping.Cells.Last().ParentCell,
			ChildSolveHandoff.ChildRegionCellOffset + EndpointCapability.LocalCell);
	}
	ChildSolveHandoff.DirectCommitment.StageMapping.Cells.Reset();
	ChildSolveHandoff.RefreshProofCertificate(TEXT("ChildCert"));
	TestFalse(TEXT("A fresh certificate cannot authorize missing stage mapping"), ChildSolveHandoff.ValidateNoLiveObjectCarriers(FailureReason));
	TestTrue(TEXT("Missing mapping rejection identifies stage authority"), FailureReason.Contains(TEXT("stage-mapping authority")));
	return true;
}

bool FLayoutChildSolveHandoffCertificateGuardTest::RunTest(const FString& Parameters)
{
	FLayoutChildSolveHandoff ChildSolveHandoff;
	ChildSolveHandoff.ParentRegionDebugPath = TEXT("Parent/Region");
	ChildSolveHandoff.ChildRegionDebugPath = TEXT("Child/Region");
	ChildSolveHandoff.StableChildKey = TEXT("Child/Packet");
	ChildSolveHandoff.ContentMetadata.ChildProfilePath = FSoftObjectPath(TEXT("/Game/Test/ChildProfile.ChildProfile"));
	ChildSolveHandoff.RefreshProofCertificate(TEXT("ChildCert"));
	ChildSolveHandoff.ProofCertificate.InputHash ^= 0x1ULL;

	FString FailureReason;
	TestFalse(TEXT("Child handoff rejects stale certificate hash"), ChildSolveHandoff.ValidateNoLiveObjectCarriers(FailureReason));
	TestTrue(TEXT("Child handoff reports stale certificate hash"), FailureReason.Contains(TEXT("hash")));

	FLayoutChildSolveHandoff MissingWitnessHandoff;
	MissingWitnessHandoff.ParentRegionDebugPath = TEXT("Parent/Region");
	MissingWitnessHandoff.ChildRegionDebugPath = TEXT("Child/Region");
	MissingWitnessHandoff.StableChildKey = TEXT("Child/Packet");
	MissingWitnessHandoff.ContentMetadata.ChildProfilePath = FSoftObjectPath(TEXT("/Game/Test/ChildProfile.ChildProfile"));
	MissingWitnessHandoff.bRequiresCertifiedWitnessBundle = true;
	MissingWitnessHandoff.RefreshProofCertificate(TEXT("ChildCert"));
	TestFalse(TEXT("Child handoff rejects missing required witness bundle"), MissingWitnessHandoff.ValidateNoLiveObjectCarriers(FailureReason));
	TestTrue(TEXT("Child handoff reports missing witness bundle"), FailureReason.Contains(TEXT("witness")));
	MissingWitnessHandoff.CertifiedWitnessBundle.SourceContentEntryId = TEXT("Entry.Child");
	MissingWitnessHandoff.CertifiedWitnessBundle.StableChildId = TEXT("Child.Stable");
	MissingWitnessHandoff.CertifiedWitnessBundle.BranchId = TEXT("Branch.A");
	MissingWitnessHandoff.CertifiedWitnessBundle.AssertionIds = {TEXT("Assert.Child")};
	MissingWitnessHandoff.RefreshProofCertificate(TEXT("ChildCert"));
	TestTrue(TEXT("Child handoff accepts required witness bundle carrier"), MissingWitnessHandoff.ValidateNoLiveObjectCarriers(FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorkerSolveRequestFinalizerCompatibilityTest,
	"PorismExtension.Layout.Async.Finalizer.BuildsWorkerRequestFromPacketManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorkerSolveRequestFinalizerChildHandoffTest,
	"PorismExtension.Layout.Async.Finalizer.AppliesChildHandoffOverrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorkerSolveRequestFinalizerCompatibilityTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* const Profile = NewObject<ULayoutProfileAsset>();
	ULayoutRegionContentSetAsset* const ContentSet = NewObject<ULayoutRegionContentSetAsset>();
	Profile->ContentSet = ContentSet;
	
	FLayoutWorldBindingRuntimeView RuntimeView;
	RuntimeView.PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	RuntimeView.BindingId = TEXT("Binding.Finalizer.Root");
	RuntimeView.CandidateId = TEXT("Candidate.Finalizer.Root");
	RuntimeView.LayoutProfile = Profile;
	RuntimeView.ContentSet = ContentSet;
	RuntimeView.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
	RuntimeView.TemplatePlacementZOffsetBlocks = 4;

	FPlannedLayoutSiteRecord PendingRecord;
	FLayoutPlannedSiteReservationSourceSelection ReservationSelection;
	ReservationSelection.ReservationKey = FIntPoint(3, 5);
	ReservationSelection.SiteCenterBlockWorldPos = FIntVector(64, 80, 96);
	PendingRecord.SetPlannedSiteReservationSourceSelection(ReservationSelection);
	FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata;
	LifecycleMetadata.StableRecordKey = TEXT("Planning.Finalizer.Root");
	PendingRecord.SetPlannedSiteLifecycleMetadata(LifecycleMetadata);
	FLayoutSiteSolveSourceSelection SolveSourceSelection;
	SolveSourceSelection.SolveSeed = 4040;
	PendingRecord.SetSiteSolveSourceSelection(SolveSourceSelection);

	FLayoutRegionSolveRequest Request;
	Request.EffectiveSnapshotId = TEXT("Effective.Finalizer.Root");
	Request.Seed = 4040;
	Request.TemplatePlacementZOffsetBlocks = 4;
	Request.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	Request.WorldBindingId = RuntimeView.BindingId;
	Request.WorldBindingPlacementPolicy = RuntimeView.PlacementPolicy;
	Request.ProfileSnapshot.SnapshotId = TEXT("Profile.Finalizer.Root");
	Request.ProfileSnapshot.SourceProfile = Profile;
	Request.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::Range;
	Request.ProfileSnapshot.VerticalAccessCount = 4;
	Request.ProfileSnapshot.MinVerticalAccessCount = 2;
	Request.ProfileSnapshot.MaxVerticalAccessCount = 6;
	Request.ContentSetSnapshot.SnapshotId = TEXT("Content.Finalizer.Root");
	Request.ContentSetSnapshot.SourceContentSet = ContentSet;
	Request.ContentSetSnapshot.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
	Request.ModuleCatalog.SnapshotId = TEXT("ModuleCatalog.Finalizer.Root");
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
	Request.FootprintSize = FIntPoint(1, 1);
	Request.PrecomputedPlannedCells.Add({FIntVector::ZeroValue, ELayoutCellIntent::Boundary});
	Request.PrecomputedActiveCells.Add({FIntVector::ZeroValue});

	FLayoutWorkerSolvePacket WorkerPacket = FLayoutWorkerSolvePacket::CapturePlanningRoot(
		TEXT("PlanningRoot Finalizer"),
		RuntimeView,
		PendingRecord,
		0);
	WorkerPacket.RequestManifest = FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(Request);
	WorkerPacket.bHasRequestManifest = true;
	{
		FLayoutContractModeSelectionInput ModeInput;
		ModeInput.SolveRequest = &Request;
		ModeInput.SiteCenterBlockWorldPos = ReservationSelection.SiteCenterBlockWorldPos;
		ModeInput.WorldSeed = 0;
		WorkerPacket.SelectedModePlan = FLayoutContractModeSelection::SelectModePlan(ModeInput);
		WorkerPacket.bHasSelectedModePlan = true;
		WorkerPacket.RequestManifest.bHasSelectedModePlan = true;
		WorkerPacket.RequestManifest.SelectedModePlan = WorkerPacket.SelectedModePlan;
	}
	WorkerPacket.bHasPrecomputedAdapterOutput = true;
	WorkerPacket.PrecomputedAdapterOutput.ModePlan = WorkerPacket.SelectedModePlan;
	WorkerPacket.PrecomputedAdapterOutput.PlannedCells = Request.PrecomputedPlannedCells;
	WorkerPacket.PrecomputedAdapterOutput.ActiveCells = Request.PrecomputedActiveCells;
	const FLayoutFrozenRequestManifestArtifact ManifestArtifact =
		WorkerPacket.RequestManifest.BuildFrozenRequestManifestArtifact();
	FString ArtifactFailureReason;
	TestTrue(TEXT("Worker packet request manifest builds a valid frozen manifest artifact"), FLayoutContractManifestCache::ValidateFrozenRequestManifestArtifact(ManifestArtifact, ArtifactFailureReason));

	FLayoutContractManifestCache ManifestCache;
	FLayoutContractManifestCacheEntry ManifestEntry;
	bool bManifestCacheHit = true;
	FLayoutRegionSolveRequest FinalizedRequest;
	FString FailureReason;
	if (!TestTrue(TEXT("Packet finalizer succeeds and consumes worker-side manifest cache"), LayoutWorkerSolveRequestFinalizer::FinalizeRequestAndManifestFromPacket(
			WorkerPacket,
			ManifestCache,
			FinalizedRequest,
			ManifestEntry,
			bManifestCacheHit,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	TestEqual(TEXT("Finalized request keeps packet seed"), FinalizedRequest.Seed, 4040);
	TestEqual(TEXT("Finalized request keeps packet placement kind"), FinalizedRequest.RootPlacementKind, ELayoutWorldBindingPlacementKind::OrdinaryRoot);
	TestEqual(TEXT("Finalized request keeps packet template offset"), FinalizedRequest.TemplatePlacementZOffsetBlocks, 4);
	TestNull(TEXT("Finalized request strips profile source"), FinalizedRequest.ProfileSnapshot.SourceProfile.Get());
	TestNull(TEXT("Finalized request strips content source"), FinalizedRequest.ContentSetSnapshot.SourceContentSet.Get());
	TestEqual(TEXT("Finalized request keeps authored VerticalAccess mode"), FinalizedRequest.ProfileSnapshot.VerticalAccessCountMode, ELayoutCountConstraintMode::Range);
	TestEqual(TEXT("Finalized request keeps authored VerticalAccess exact field"), FinalizedRequest.ProfileSnapshot.VerticalAccessCount, 4);
	TestEqual(TEXT("Finalized request keeps authored VerticalAccess minimum"), FinalizedRequest.ProfileSnapshot.MinVerticalAccessCount, 2);
	TestEqual(TEXT("Finalized request keeps authored VerticalAccess maximum"), FinalizedRequest.ProfileSnapshot.MaxVerticalAccessCount, 6);
	TestFalse(TEXT("First worker-side manifest cache consumption is a miss"), bManifestCacheHit);
	TestEqual(TEXT("Worker-side manifest cache stores one entry"), ManifestCache.Num(), 1);
	TestEqual(TEXT("Manifest cache entry keeps owning world-binding id"), ManifestEntry.Key.WorldBindingId, RuntimeView.BindingId);
	TestEqual(TEXT("Manifest cache entry uses packet-manifest artifact key"), ManifestEntry.Key.KeyId, ManifestArtifact.Key.KeyId);
	TestEqual(TEXT("Manifest payload keeps owning world-binding id"), ManifestEntry.Manifest.WorldBindingId, RuntimeView.BindingId);

	FLayoutContractManifestCacheEntry CachedManifestEntry;
	FLayoutRegionSolveRequest SecondFinalizedRequest;
	TestTrue(TEXT("Equivalent packet finalizer reuses worker-side manifest cache"), LayoutWorkerSolveRequestFinalizer::FinalizeRequestAndManifestFromPacket(
		WorkerPacket,
		ManifestCache,
		SecondFinalizedRequest,
		CachedManifestEntry,
		bManifestCacheHit,
		FailureReason));
	TestTrue(TEXT("Second worker-side manifest cache consumption is a hit"), bManifestCacheHit);
	TestEqual(TEXT("Equivalent packet keeps one worker-side cache entry"), ManifestCache.Num(), 1);
	TestEqual(TEXT("Cache hit returns same manifest cache key"), CachedManifestEntry.Key.KeyId, ManifestEntry.Key.KeyId);

	FLayoutWorkerSolveRequestManifest ExactManifest = WorkerPacket.RequestManifest;
	ExactManifest.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	ExactManifest.ProfileSnapshot.VerticalAccessCount = 1;
	FLayoutRegionSolveRequest ExactRequest;
	ExactManifest.PopulateSolveRequest(ExactRequest);
	const FLayoutFrozenRequestManifestArtifact ExactArtifact =
		FLayoutContractManifestCache::BuildFrozenRequestManifestArtifact(ExactRequest);
	FLayoutContractManifestCacheEntry ExactEntry;
	TestTrue(TEXT("Edited Exact1 manifest remains cacheable"),
		ManifestCache.FindOrAddManifest(ExactArtifact, ExactEntry, bManifestCacheHit));
	TestFalse(TEXT("Edited VerticalAccess count contract invalidates same-name manifest"), bManifestCacheHit);
	TestNotEqual(TEXT("Edited count contract receives distinct cache identity"), ExactEntry.Key.KeyId, ManifestEntry.Key.KeyId);
	TestEqual(TEXT("Edited count contract adds one manifest entry"), ManifestCache.Num(), 2);

	FLayoutWorkerSolveRequestManifest NoneManifest = WorkerPacket.RequestManifest;
	NoneManifest.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::None;
	NoneManifest.ProfileSnapshot.VerticalAccessCount = 0;
	NoneManifest.ProfileSnapshot.MinVerticalAccessCount = 0;
	NoneManifest.ProfileSnapshot.MaxVerticalAccessCount = 0;
	FLayoutRegionSolveRequest NoneRequest;
	NoneManifest.PopulateSolveRequest(NoneRequest);
	const FLayoutFrozenRequestManifestArtifact NoneArtifact =
		FLayoutContractManifestCache::BuildFrozenRequestManifestArtifact(NoneRequest);
	FLayoutContractManifestCacheEntry NoneEntry;
	TestTrue(TEXT("Edited None manifest remains cacheable"),
		ManifestCache.FindOrAddManifest(NoneArtifact, NoneEntry, bManifestCacheHit));
	TestFalse(TEXT("None invalidates prior Exact1 and Range identities"), bManifestCacheHit);
	TestEqual(TEXT("None adds third count-specific entry"), ManifestCache.Num(), 3);

	FLayoutContractManifestCacheEntry ReusedNoneEntry;
	TestTrue(TEXT("Repeated None manifest remains cacheable"),
		ManifestCache.FindOrAddManifest(NoneArtifact, ReusedNoneEntry, bManifestCacheHit));
	TestTrue(TEXT("Repeated None consumes its matching cache entry"), bManifestCacheHit);
	TestEqual(TEXT("Repeated None does not grow cache"), ManifestCache.Num(), 3);

	FLayoutContractManifestCacheEntry ReusedExactEntry;
	TestTrue(TEXT("None to Exact1 remains cacheable"),
		ManifestCache.FindOrAddManifest(ExactArtifact, ReusedExactEntry, bManifestCacheHit));
	TestTrue(TEXT("None to Exact1 reuses only Exact1 identity"), bManifestCacheHit);

	FLayoutContractManifestCacheEntry ReusedRangeEntry;
	TestTrue(TEXT("Exact1 to Range remains cacheable"),
		ManifestCache.FindOrAddManifest(ManifestArtifact, ReusedRangeEntry, bManifestCacheHit));
	TestTrue(TEXT("Exact1 to Range reuses only original Range identity"), bManifestCacheHit);

	TestTrue(TEXT("Range to None remains cacheable"),
		ManifestCache.FindOrAddManifest(NoneArtifact, ReusedNoneEntry, bManifestCacheHit));
	TestTrue(TEXT("Range to None reuses only None identity"), bManifestCacheHit);
	TestEqual(TEXT("None to Exact1 to Range to None does not duplicate cache entries"), ManifestCache.Num(), 3);
	return true;
}

bool FLayoutWorkerSolveRequestFinalizerChildHandoffTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* const Profile = NewObject<ULayoutProfileAsset>();
	ULayoutRegionContentSetAsset* const ContentSet = NewObject<ULayoutRegionContentSetAsset>();
	Profile->ContentSet = ContentSet;
	
	FLayoutWorldBindingRuntimeView RuntimeView;
	RuntimeView.PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	RuntimeView.BindingId = TEXT("Binding.Finalizer.Child");
	RuntimeView.CandidateId = TEXT("Candidate.Finalizer.Child");
	RuntimeView.LayoutProfile = Profile;
	RuntimeView.ContentSet = ContentSet;
	RuntimeView.SharedCellSizeInBlocks = FIntVector(16, 16, 16);

	FLayoutRegionSolveRequest Request;
	Request.Seed = 5150;
	Request.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	Request.WorldBindingPlacementPolicy = RuntimeView.PlacementPolicy;
	Request.ProfilePath = FSoftObjectPath(TEXT("/Game/Test/RootProfile.RootProfile"));
	Request.ProfileSnapshot.SourceProfile = Profile;
	Request.ContentSetSnapshot.SourceContentSet = ContentSet;
	Request.ContentSetSnapshot.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
	Request.FootprintSize = FIntPoint(1, 1);
	Request.PrecomputedPlannedCells.Add({FIntVector::ZeroValue, ELayoutCellIntent::Boundary});
	Request.PrecomputedActiveCells.Add({FIntVector::ZeroValue});

	FLayoutChildSolveHandoff ChildSolveHandoff;
	ChildSolveHandoff.ParentRegionDebugPath = TEXT("Parent/Region");
	ChildSolveHandoff.ChildRegionDebugPath = TEXT("Child/Region");
	ChildSolveHandoff.ParentArtifactOrResumeId = TEXT("ParentResume");
	ChildSolveHandoff.StableChildKey = TEXT("Child/Branch");
	ChildSolveHandoff.AttemptIndex = 2;
	ChildSolveHandoff.ChildRegionCellOffset = FIntVector(7, 8, 9);
	ChildSolveHandoff.ContentMetadata.SourceContentEntryId = TEXT("Entry.Child");
	ChildSolveHandoff.ContentMetadata.ChildProfilePath = TSoftObjectPtr<ULayoutProfileAsset>(Profile).ToSoftObjectPath();
	ChildSolveHandoff.ContentMetadata.bOptional = true;
	ChildSolveHandoff.DelegatedZoneFeatureRequirementIds = {TEXT("Feature.Requirement")};
	ChildSolveHandoff.DelegatedClosureRequirementIds = {TEXT("Closure.Requirement")};
	ChildSolveHandoff.ChildCapabilityEnvelope.RegionDebugPath = ChildSolveHandoff.ChildRegionDebugPath;
	ChildSolveHandoff.ChildCapabilityEnvelope.SnapshotId = TEXT("ChildEnvelope");
	ChildSolveHandoff.DirectCommitment.ParentRegionDebugPath = ChildSolveHandoff.ParentRegionDebugPath;
	ChildSolveHandoff.DirectCommitment.ChildRegionDebugPath = ChildSolveHandoff.ChildRegionDebugPath;
	FLayoutCommittedTraversalAnchor& TraversalAnchor = ChildSolveHandoff.DirectCommitment.ParentTraversalIngressCommitments.AddDefaulted_GetRef();
	TraversalAnchor.Cell = FIntVector(1, 2, 0);
	TraversalAnchor.TraversalChannel = FGameplayTag();
	FLayoutCommittedEndpointAnchor& EndpointAnchor = ChildSolveHandoff.DirectCommitment.EndpointCommitments.AddDefaulted_GetRef();
	EndpointAnchor.CommitmentId = TEXT("Endpoint.Commit");
	ChildSolveHandoff.DirectCommitment.NegotiatedResponsibilityContract.ParentRegionDebugPath = ChildSolveHandoff.ParentRegionDebugPath;
	ChildSolveHandoff.DirectCommitment.NegotiatedResponsibilityContract.ChildRegionDebugPath = ChildSolveHandoff.ChildRegionDebugPath;
	ChildSolveHandoff.ProtectedStructuralCells = {FIntVector(3, 4, 0)};
	FLayoutRouteConstraintRecord& RouteConstraint = ChildSolveHandoff.RequiredRouteConstraints.AddDefaulted_GetRef();
	RouteConstraint.ConstraintId = TEXT("Route.Constraint");
	SetStageNeutralTestMapping(ChildSolveHandoff, Request.PrecomputedPlannedCells);
	ChildSolveHandoff.RefreshProofCertificate(TEXT("ChildCert"));

	FLayoutWorkerSolvePacket WorkerPacket = FLayoutWorkerSolvePacket::CaptureChild(
		TEXT("Child Finalizer"),
		FLayoutWorldBindingRuntimeSnapshot::CaptureFromRuntimeView(RuntimeView),
		ChildSolveHandoff,
		5150,
		2);
	WorkerPacket.RequestManifest = FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(Request);
	WorkerPacket.bHasRequestManifest = true;
	{
		FLayoutRegionSolveRequest SelectedModeRequest = Request;
		SelectedModeRequest.SourceParentRegionDebugPath = ChildSolveHandoff.ParentRegionDebugPath;
		SelectedModeRequest.SourceContentEntryId = ChildSolveHandoff.ContentMetadata.SourceContentEntryId;
		SelectedModeRequest.ProfilePath = ChildSolveHandoff.ContentMetadata.ChildProfilePath;
		SelectedModeRequest.RegionCellOffset = ChildSolveHandoff.ChildRegionCellOffset;
		FLayoutContractModeSelectionInput ModeInput;
		ModeInput.SolveRequest = &SelectedModeRequest;
		ModeInput.SiteCenterBlockWorldPos = ChildSolveHandoff.ChildRegionCellOffset;
		ModeInput.WorldSeed = 0;
		WorkerPacket.SelectedModePlan = FLayoutContractModeSelection::SelectModePlan(ModeInput);
		WorkerPacket.bHasSelectedModePlan = true;
		WorkerPacket.RequestManifest.bHasSelectedModePlan = true;
		WorkerPacket.RequestManifest.SelectedModePlan = WorkerPacket.SelectedModePlan;
	}
	WorkerPacket.bHasPrecomputedAdapterOutput = true;
	WorkerPacket.PrecomputedAdapterOutput.ModePlan = WorkerPacket.SelectedModePlan;
	WorkerPacket.PrecomputedAdapterOutput.PlannedCells = Request.PrecomputedPlannedCells;
	WorkerPacket.PrecomputedAdapterOutput.ActiveCells = Request.PrecomputedActiveCells;

	FLayoutRegionSolveRequest FinalizedRequest;
	FString FailureReason;
	if (!TestTrue(TEXT("Child packet finalizer succeeds"), LayoutWorkerSolveRequestFinalizer::FinalizeRequestFromPacket(
			WorkerPacket,
			FinalizedRequest,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	TestEqual(TEXT("Child finalizer copies parent region path"), FinalizedRequest.SourceParentRegionDebugPath, FString(TEXT("Parent/Region")));
	TestEqual(TEXT("Child finalizer copies source entry id"), FinalizedRequest.SourceContentEntryId, FName(TEXT("Entry.Child")));
	TestEqual(TEXT("Child finalizer copies child profile path"), FinalizedRequest.ProfilePath, TSoftObjectPtr<ULayoutProfileAsset>(Profile).ToSoftObjectPath());
	TestTrue(TEXT("Child finalizer copies optional flag"), FinalizedRequest.bSourceContentEntryOptional);
	TestEqual(TEXT("Child finalizer copies region offset"), FinalizedRequest.RegionCellOffset, FIntVector(7, 8, 9));
	TestEqual(TEXT("Child finalizer copies protected structural cells"), FinalizedRequest.ProtectedStructuralCells.Num(), 1);
	TestEqual(TEXT("Child finalizer copies route constraints"), FinalizedRequest.RequiredRouteConstraints.Num(), 1);
	TestEqual(TEXT("Child finalizer copies endpoint anchors"), FinalizedRequest.CommittedEndpointAnchors.Num(), 1);
	TestEqual(TEXT("Child finalizer copies traversal anchors"), FinalizedRequest.CommittedTraversalAnchors.Num(), 1);
	TestTrue(TEXT("Child finalizer enables supplied capability envelope"), FinalizedRequest.bUseSuppliedChildCapabilityEnvelope);
	TestEqual(TEXT("Child finalizer copies delegated feature requirement ids"), FinalizedRequest.DelegatedZoneFeatureRequirementIds.Num(), 1);
	TestEqual(TEXT("Child finalizer copies delegated closure requirement ids"), FinalizedRequest.DelegatedClosureRequirementIds.Num(), 1);
	TestEqual(TEXT("Child finalizer copies negotiated child responsibility contract"), FinalizedRequest.NegotiatedChildResponsibilityContracts.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBackgroundSolveSnapshotScrubTest,
	"PorismExtension.Layout.Async.Snapshot.ScrubsLiveCarriersFromWorkerRequestAndResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBackgroundSolveSnapshotScrubTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.ProfileSnapshot.SourceProfile = NewObject<ULayoutProfileAsset>();
	Request.ContentSetSnapshot.SourceContentSet = NewObject<ULayoutRegionContentSetAsset>();
	FLayoutModuleSolveSnapshot& ModuleSnapshot = Request.ModuleCatalog.Modules.AddDefaulted_GetRef();
	ModuleSnapshot.SourceModule = NewObject<ULayoutModuleAsset>();
	ModuleSnapshot.SourceCompositeModule = NewObject<ULayoutCompositeModuleAsset>();
	ModuleSnapshot.SnapshotId = TEXT("Module.A");
	FLayoutLocalCellFaceRuleSnapshot& LocalCellDescriptor =
		ModuleSnapshot.GeneratedLocalCellFaceRules.AddDefaulted_GetRef();
	LocalCellDescriptor.LocalCell = FIntVector::ZeroValue;
	LocalCellDescriptor.TemplatePath = FSoftObjectPath(TEXT("/Game/Test/Templates/T_CompositeLeaf.T_CompositeLeaf"));
	LocalCellDescriptor.RelativeYawRotationSteps = 1;

	FString FailureReason;
	TestFalse(TEXT("Unscrubbed request fails worker validation"), LayoutBackgroundSolveSnapshot::ValidateWorkerSafeSolveRequest(Request, FailureReason));

	const FLayoutRegionSolveRequest WorkerRequest = LayoutBackgroundSolveSnapshot::MakeWorkerSafeSolveRequest(Request);
	TestTrue(TEXT("Scrubbed request passes worker validation"), LayoutBackgroundSolveSnapshot::ValidateWorkerSafeSolveRequest(WorkerRequest, FailureReason));
	TestNull(TEXT("Profile source stripped"), WorkerRequest.ProfileSnapshot.SourceProfile.Get());
	TestNull(TEXT("Content set source stripped"), WorkerRequest.ContentSetSnapshot.SourceContentSet.Get());
	TestNull(TEXT("Module source stripped"), WorkerRequest.ModuleCatalog.Modules[0].SourceModule.Get());
	TestNull(TEXT("Composite source stripped"), WorkerRequest.ModuleCatalog.Modules[0].SourceCompositeModule.Get());
	TestEqual(TEXT("Pointer-free local descriptor keeps the frozen leaf template path"), WorkerRequest.ModuleCatalog.Modules[0].GeneratedLocalCellFaceRules[0].TemplatePath, LocalCellDescriptor.TemplatePath);
	TestEqual(TEXT("Pointer-free local descriptor keeps the frozen relative yaw"), WorkerRequest.ModuleCatalog.Modules[0].GeneratedLocalCellFaceRules[0].RelativeYawRotationSteps, 1);

	FLayoutSolveResult Result;
	FLayoutPlacedModule& Placement = Result.Placements.AddDefaulted_GetRef();
	Placement.Module = NewObject<ULayoutModuleAsset>();
	Placement.CompositeModule = NewObject<ULayoutCompositeModuleAsset>();
	Placement.ModuleSnapshotId = TEXT("Module.A");
	Placement.ModuleSnapshotIndex = 0;
	TestFalse(TEXT("Unscrubbed result fails worker validation"), LayoutBackgroundSolveSnapshot::ValidateWorkerSafeSolveResult(Result, FailureReason));
	LayoutBackgroundSolveSnapshot::ScrubWorkerSolveResult(Result);
	TestTrue(TEXT("Scrubbed result passes worker validation"), LayoutBackgroundSolveSnapshot::ValidateWorkerSafeSolveResult(Result, FailureReason));
	TestNull(TEXT("Placement module carrier stripped"), Result.Placements[0].Module.Get());
	TestNull(TEXT("Placement composite carrier stripped"), Result.Placements[0].CompositeModule.Get());
	TestEqual(TEXT("Placement snapshot id survives scrub"), Result.Placements[0].ModuleSnapshotId, FLayoutId(TEXT("Module.A")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutActiveBiomeNoiseSnapshotInlineParityTest,
	"PorismExtension.Layout.Async.ActiveBiomeNoiseSnapshot.MatchesSamplerForInlineEncodedRowsAndHiddenFiltering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutActiveBiomeNoiseSnapshotInlineParityTest::RunTest(const FString& Parameters)
{
	constexpr const TCHAR* ConstantPositiveFastNoise = TEXT("AAAAAIA/");
	UWorldGenDef* const WorldGenDef = NewObject<UWorldGenDef>();
	WorldGenDef->BaseBlockSize = 100;
	WorldGenDef->NoiseScale = FVector::OneVector;
	WorldGenDef->NoiseCoordinateOffset = FIntVector::ZeroValue;

	FBiomeDualData& VisibleRow = WorldGenDef->WorldBiomes.AddDefaulted_GetRef();
	VisibleRow.BiomeName = TEXT("VisibleInline");
	VisibleRow.DomainOver = 1.0f;
	VisibleRow.GenU_Mat1.AddDefaulted();
	VisibleRow.Domain = ConstantPositiveFastNoise;
	VisibleRow.DualSwitch = ConstantPositiveFastNoise;
	VisibleRow.GenA = ConstantPositiveFastNoise;
	VisibleRow.GenB = ConstantPositiveFastNoise;

	FBiomeDualData& HiddenRow = WorldGenDef->WorldBiomes.AddDefaulted_GetRef();
	HiddenRow.BiomeName = TEXT("HiddenInline");
	HiddenRow.BiomeHidden = true;
	HiddenRow.Domain = ConstantPositiveFastNoise;
	HiddenRow.GenA = ConstantPositiveFastNoise;
	HiddenRow.GenB = ConstantPositiveFastNoise;

	FBiomeDualData& MissingDomain = WorldGenDef->WorldBiomes.AddDefaulted_GetRef();
	MissingDomain.BiomeName = TEXT("MissingDomain");
	MissingDomain.Domain.Reset();
	MissingDomain.DomainBP = nullptr;
	MissingDomain.DomainRun = nullptr;

	const int32 Seed = 77;
	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Reference sampler initializes inline row"), Sampler.Initialize(GetTransientPackage(), WorldGenDef, Seed));

	FString FailureReason;
	const TSharedRef<const FLayoutActiveBiomeNoiseSnapshot, ESPMode::ThreadSafe> Snapshot =
		FLayoutActiveBiomeNoiseSnapshot::CaptureFromWorldDefinition(GetTransientPackage(), WorldGenDef, Seed, FailureReason);
	TestTrue(TEXT("Snapshot initializes inline row"), Snapshot->IsInitialized());
	TestTrue(TEXT("Snapshot capture has no failure"), FailureReason.IsEmpty());
	TestTrue(TEXT("Visible row captured"), Snapshot->CapturedBiomeRows.Contains(FLayoutId(TEXT("VisibleInline"))));
	TestFalse(TEXT("Hidden row filtered"), Snapshot->CapturedBiomeRows.Contains(FLayoutId(TEXT("HiddenInline"))));
	// Native CompileRuntimeConfig supplies constant-positive missing nodes; capture must preserve that density contribution.
	TestTrue(TEXT("Missing Domain preserves native fallback in worker capture"), Snapshot->CapturedBiomeRows.Contains(FLayoutId(TEXT("MissingDomain"))));

	FLayoutNoiseCoordinateSettings CoordinateSettings;
	CoordinateSettings.BaseBlockSize = 100;
	CoordinateSettings.NoiseScale = FVector::OneVector;
	CoordinateSettings.NoiseCoordinateOffset = FIntVector::ZeroValue;

	FLayoutActiveBiomeSample SamplerSample;
	FLayoutActiveBiomeSample SnapshotSample;
	const FIntVector SamplePosition(32, 64, 12);
	TestTrue(TEXT("Reference sampler samples"), Sampler.SampleAtBlockPosition(SamplePosition, CoordinateSettings, SamplerSample));
	TestTrue(TEXT("Snapshot samples"), Snapshot->SampleAtBlockPosition(SamplePosition, CoordinateSettings, SnapshotSample));
	TestEqual(TEXT("Winning row matches"), SnapshotSample.WinningRow.RowName, SamplerSample.WinningRow.RowName);
	TestEqual(TEXT("Visible owning row wins rather than matching two empty identities"), SnapshotSample.WinningRow.RowName, FName(TEXT("VisibleInline")));
	TestEqual(TEXT("Domain state matches"), SnapshotSample.bAnyPositiveDomain, SamplerSample.bAnyPositiveDomain);
	TestEqual(TEXT("Terrain solid state matches"), SnapshotSample.bTerrainSolid, SamplerSample.bTerrainSolid);
	TestEqual(TEXT("Gen selector matches"), SnapshotSample.bSelectedGenA, SamplerSample.bSelectedGenA);
	TestEqual(TEXT("Terrain value matches"), SnapshotSample.TerrainValue, SamplerSample.TerrainValue);
	TestEqual(TEXT("Global, visible and missing-domain noise each contribute one; hidden row contributes nothing"), SnapshotSample.TerrainValue, 3.0f);
	FLayoutActiveBiomeSurfaceSample ReferenceSurface, WorkerSurface;
	TestEqual(TEXT("Worker shares existing surface search"),
		Snapshot->GetSampler().FindAnyActiveBiomeSurface(FIntPoint(32, 64), 12, 16, CoordinateSettings, WorkerSurface),
		Sampler.FindAnyActiveBiomeSurface(FIntPoint(32, 64), 12, 16, CoordinateSettings, ReferenceSurface));
	TestEqual(TEXT("Surface evidence matches"), WorkerSurface.bIsValid, ReferenceSurface.bIsValid);
	WorldGenDef->WorldBiomes.Empty();
	TestTrue(TEXT("Captured utility survives source configuration removal"), Snapshot->GetSampler().SampleAtBlockPosition(SamplePosition, CoordinateSettings, SnapshotSample));
	TestEqual(TEXT("Captured row remains frozen"), SnapshotSample.WinningRow.RowName, SamplerSample.WinningRow.RowName);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutActiveBiomeNoiseSnapshotBlueprintParityTest,
	"PorismExtension.Layout.Async.ActiveBiomeNoiseSnapshot.MatchesSamplerForBlueprintRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutActiveBiomeNoiseSnapshotBlueprintParityTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateActiveBiomeSnapshotWorldGenDef(GetTransientPackage());
	FBiomeDualData& Row = WorldGenDef->WorldBiomes.AddDefaulted_GetRef();
	Row.BiomeName = TEXT("BlueprintGenA");
	Row.Domain = ActiveBiomeConstantPositiveFastNoise;
	Row.DualSwitch = ActiveBiomeConstantPositiveFastNoise;
	Row.GenABP = UBiomeFastNoiseEditor::StaticClass();

	const int32 Seed = 101;
	const TSharedRef<const FLayoutActiveBiomeNoiseSnapshot, ESPMode::ThreadSafe> Snapshot =
		CaptureActiveBiomeSnapshotForTest(*this, WorldGenDef, Seed, TEXT("Blueprint row"));
	TestTrue(TEXT("Blueprint row captured"), Snapshot->CapturedBiomeRows.Contains(FLayoutId(TEXT("BlueprintGenA"))));
	return SampleAndCompareActiveBiomeSnapshot(
		*this,
		WorldGenDef,
		Snapshot,
		Seed,
		FIntVector(12, -8, 3),
		TEXT("Blueprint row"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutActiveBiomeNoiseSnapshotRuntimeNodeParityTest,
	"PorismExtension.Layout.Async.ActiveBiomeNoiseSnapshot.MatchesSamplerForRuntimeNodeRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutActiveBiomeNoiseSnapshotRuntimeNodeParityTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateActiveBiomeSnapshotWorldGenDef(GetTransientPackage());
	FBiomeDualData& Row = WorldGenDef->WorldBiomes.AddDefaulted_GetRef();
	Row.BiomeName = TEXT("RuntimeGenA");
	Row.Domain = ActiveBiomeConstantPositiveFastNoise;
	Row.DualSwitch = ActiveBiomeConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);

	const int32 Seed = 202;
	const TSharedRef<const FLayoutActiveBiomeNoiseSnapshot, ESPMode::ThreadSafe> Snapshot =
		CaptureActiveBiomeSnapshotForTest(*this, WorldGenDef, Seed, TEXT("Runtime-node row"));
	TestTrue(TEXT("Runtime-node row captured"), Snapshot->CapturedBiomeRows.Contains(FLayoutId(TEXT("RuntimeGenA"))));
	return SampleAndCompareActiveBiomeSnapshot(
		*this,
		WorldGenDef,
		Snapshot,
		Seed,
		FIntVector(-16, 9, 7),
		TEXT("Runtime-node row"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutActiveBiomeNoiseSnapshotDataTableParityTest,
	"PorismExtension.Layout.Async.ActiveBiomeNoiseSnapshot.MatchesSamplerForDataTableRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutActiveBiomeNoiseSnapshotDataTableParityTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateActiveBiomeSnapshotWorldGenDef(GetTransientPackage());
	UDataTable* const BiomeTable = NewObject<UDataTable>(WorldGenDef);
	BiomeTable->RowStruct = FBiomeDualData::StaticStruct();

	FBiomeDualData Row;
	Row.BiomeName = TEXT("DisplayNameCanDiffer");
	Row.Domain = ActiveBiomeConstantPositiveFastNoise;
	Row.DualSwitch = ActiveBiomeConstantPositiveFastNoise;
	Row.GenA = ActiveBiomeConstantPositiveFastNoise;
	BiomeTable->AddRow(TEXT("Biome.Async.DataTable"), Row);
	WorldGenDef->WorldBiomesDT = BiomeTable;

	const int32 Seed = 303;
	const TSharedRef<const FLayoutActiveBiomeNoiseSnapshot, ESPMode::ThreadSafe> Snapshot =
		CaptureActiveBiomeSnapshotForTest(*this, WorldGenDef, Seed, TEXT("DataTable row"));
	TestTrue(TEXT("DataTable row name captured"), Snapshot->CapturedBiomeRows.Contains(FLayoutId(TEXT("Biome.Async.DataTable"))));
	return SampleAndCompareActiveBiomeSnapshot(
		*this,
		WorldGenDef,
		Snapshot,
		Seed,
		FIntVector(5, 6, 7),
		TEXT("DataTable row"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutActiveBiomeNoiseSnapshotConcurrentSamplingTest,
	"PorismExtension.Layout.Async.ActiveBiomeNoiseSnapshot.SamplesConcurrentlyWithoutLiveWorldObjects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutActiveBiomeNoiseSnapshotConcurrentSamplingTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateActiveBiomeSnapshotWorldGenDef(GetTransientPackage());
	FBiomeDualData& InlineRow = WorldGenDef->WorldBiomes.AddDefaulted_GetRef();
	InlineRow.BiomeName = TEXT("ConcurrentInline");
	InlineRow.DomainOver = 1.0f;
	InlineRow.GenU_Mat1.AddDefaulted();
	InlineRow.Domain = ActiveBiomeConstantPositiveFastNoise;
	InlineRow.DualSwitch = ActiveBiomeConstantPositiveFastNoise;
	InlineRow.GenA = ActiveBiomeConstantPositiveFastNoise;

	FBiomeDualData& RuntimeRow = WorldGenDef->WorldBiomes.AddDefaulted_GetRef();
	RuntimeRow.BiomeName = TEXT("ConcurrentRuntimeHidden");
	RuntimeRow.BiomeHidden = true;
	RuntimeRow.Domain = ActiveBiomeConstantPositiveFastNoise;
	RuntimeRow.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);

	const int32 Seed = 404;
	const FLayoutNoiseCoordinateSettings CoordinateSettings = MakeActiveBiomeSnapshotCoordinateSettings(WorldGenDef);
	const TSharedRef<const FLayoutActiveBiomeNoiseSnapshot, ESPMode::ThreadSafe> Snapshot =
		CaptureActiveBiomeSnapshotForTest(*this, WorldGenDef, Seed, TEXT("Concurrent sampling"));
	TestTrue(TEXT("Concurrent visible row captured"), Snapshot->CapturedBiomeRows.Contains(FLayoutId(TEXT("ConcurrentInline"))));
	TestFalse(TEXT("Concurrent hidden row filtered"), Snapshot->CapturedBiomeRows.Contains(FLayoutId(TEXT("ConcurrentRuntimeHidden"))));

	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> SuccessCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> FailureCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	TArray<UE::Tasks::FTask> Tasks;
	for (int32 TaskIndex = 0; TaskIndex < 8; ++TaskIndex)
	{
		Tasks.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, [Snapshot, CoordinateSettings, SuccessCounter, FailureCounter, TaskIndex]()
		{
			for (int32 SampleIndex = 0; SampleIndex < 64; ++SampleIndex)
			{
				FLayoutActiveBiomeSample Sample;
				const FIntVector Position(TaskIndex * 7 + SampleIndex, TaskIndex * -3, SampleIndex % 11);
				if (!Snapshot->SampleAtBlockPosition(Position, CoordinateSettings, Sample)
					|| !Sample.bIsValid
					|| !Sample.bAnyPositiveDomain
					|| Sample.WinningRow.RowName != FLayoutId(TEXT("ConcurrentInline")))
				{
					FailureCounter->Increment();
					return;
				}
			}
			SuccessCounter->Increment();
		}));
	}

	for (UE::Tasks::FTask& Task : Tasks)
	{
		Task.Wait();
	}

	TestEqual(TEXT("All concurrent worker tasks sampled the compiled snapshot"), SuccessCounter->GetValue(), 8);
	TestEqual(TEXT("No concurrent worker task failed sampling"), FailureCounter->GetValue(), 0);
	return true;
}
