// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Misc/AutomationTest.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionScheduleSolverFacade.h"

namespace
{
	using namespace LayoutRegionScheduleSolverFacade;

	FNegotiatedDemandResult MakeOptionalPlannerResult(
		const FString& ParentRegionPath,
		const FString& ChildRegionPath,
		const bool bOptional,
		const bool bSucceeded,
		const FName SourceContentEntryId,
		const FLayoutId SnapshotId,
		const FString& FailureReason = FString())
	{
		FNegotiatedDemandResult Result;
		Result.bSucceeded = bSucceeded;
		Result.ChildRegionDebugPath = ChildRegionPath;
		Result.ChildRequest.RegionDebugPath = ChildRegionPath;
		Result.ChildRequest.SourceParentRegionDebugPath = ParentRegionPath;
		Result.ChildRequest.bSourceContentEntryOptional = bOptional;
		Result.ChildRequest.SourceContentEntryId = SourceContentEntryId;
		Result.ChildRequest.EffectiveSnapshotId = SnapshotId;
		Result.FailureReason = FailureReason;
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutOptionalPlannerTracksKeptAndDroppedOptionalChildrenTest,
	"PorismExtension.Layout.Solver.OptionalPlanner.TracksKeptAndDroppedOptionalChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutOptionalPlannerSortsOutcomesDeterministicallyTest,
	"PorismExtension.Layout.Solver.OptionalPlanner.SortsOutcomesDeterministically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutOptionalPlannerTracksKeptAndDroppedOptionalChildrenTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	const FNegotiatedDemandResult RequiredFailure =
		MakeOptionalPlannerResult(
			TEXT("Root"),
			TEXT("ChildRequired"),
			false,
			false,
			TEXT("RequiredEntry"),
			TEXT("RequiredSnapshot"),
			TEXT("Required child failed and must not be converted into an optional drop record."));
	const FNegotiatedDemandResult KeptOptional =
		MakeOptionalPlannerResult(
			TEXT("Root"),
			TEXT("ChildOptionalKept"),
			true,
			true,
			TEXT("OptionalKeptEntry"),
			TEXT("OptionalKeptSnapshot"));
	const FNegotiatedDemandResult DroppedOptional =
		MakeOptionalPlannerResult(
			TEXT("Root"),
			TEXT("ChildOptionalDropped"),
			true,
			false,
			TEXT("OptionalDroppedEntry"),
			TEXT("OptionalDroppedSnapshot"),
			TEXT("Negotiated seam ownership could not satisfy the optional child."));

	const FOptionalChildDecisionPlan OptionalPlan =
		BuildOptionalChildDecisionPlan(
			SolveContext,
			{RequiredFailure, DroppedOptional, KeptOptional});

	if (!TestEqual(
			TEXT("Optional planner keeps exactly one successful optional child"),
			OptionalPlan.KeptChildRegionDebugPaths.Num(),
			1)
		|| !TestEqual(
			TEXT("Optional planner records exactly one dropped optional child"),
			OptionalPlan.DroppedOptionalChildren.Num(),
			1))
	{
		return false;
	}

	TestEqual(
		TEXT("Optional planner records the kept optional child path"),
		OptionalPlan.KeptChildRegionDebugPaths[0],
		FString(TEXT("ChildOptionalKept")));

	const FLayoutDroppedOptionalChildRecord& DropRecord =
		OptionalPlan.DroppedOptionalChildren[0];
	TestEqual(
		TEXT("Optional planner keeps the dropped optional child's parent path"),
		DropRecord.ParentRegionDebugPath,
		FString(TEXT("Root")));
	TestEqual(
		TEXT("Optional planner keeps the dropped optional child's region path"),
		DropRecord.ChildRegionDebugPath,
		FString(TEXT("ChildOptionalDropped")));
	TestEqual(
		TEXT("Optional planner keeps the dropped optional child's source content-entry id"),
		DropRecord.SourceContentEntryId,
		FName(TEXT("OptionalDroppedEntry")));
	TestEqual(
		TEXT("Optional planner keeps the dropped optional child's snapshot id"),
		DropRecord.ChildProfileSnapshotId,
		FLayoutId(TEXT("OptionalDroppedSnapshot")));
	TestTrue(
		TEXT("Optional planner keeps the dropped optional child's source content-entry id as a related id"),
		DropRecord.RelatedIds.Contains(FLayoutId(TEXT("OptionalDroppedEntry"))));
	TestTrue(
		TEXT("Optional planner keeps the dropped optional child's snapshot id as a related id"),
		DropRecord.RelatedIds.Contains(FLayoutId(TEXT("OptionalDroppedSnapshot"))));
	TestEqual(
		TEXT("Optional planner keeps the dropped optional child's failure reason"),
		DropRecord.FailureReason,
		FString(TEXT("Negotiated seam ownership could not satisfy the optional child.")));
	TestTrue(
		TEXT("Optional planner builds a stable drop-decision id from the dropped optional child identity"),
		DropRecord.DropDecisionId.ToString().Contains(TEXT("ChildOptionalDropped"))
			&& DropRecord.DropDecisionId.ToString().Contains(TEXT("OptionalDroppedEntry")));
	return true;
}

bool FLayoutOptionalPlannerSortsOutcomesDeterministicallyTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	const FOptionalChildDecisionPlan OptionalPlan =
		BuildOptionalChildDecisionPlan(
			SolveContext,
			{
				MakeOptionalPlannerResult(
					TEXT("Root"),
					TEXT("ChildOptionalB"),
					true,
					true,
					TEXT("OptionalB"),
					TEXT("SnapshotB")),
				MakeOptionalPlannerResult(
					TEXT("Root"),
					TEXT("ChildOptionalD"),
					true,
					false,
					TEXT("OptionalD"),
					TEXT("SnapshotD"),
					TEXT("Failure D")),
				MakeOptionalPlannerResult(
					TEXT("Root"),
					TEXT("ChildOptionalA"),
					true,
					true,
					TEXT("OptionalA"),
					TEXT("SnapshotA")),
				MakeOptionalPlannerResult(
					TEXT("Root"),
					TEXT("ChildOptionalC"),
					true,
					false,
					TEXT("OptionalC"),
					TEXT("SnapshotC"),
					TEXT("Failure C"))
			});

	if (!TestEqual(
			TEXT("Optional planner keeps both successful optional children"),
			OptionalPlan.KeptChildRegionDebugPaths.Num(),
			2)
		|| !TestEqual(
			TEXT("Optional planner keeps both dropped optional children"),
			OptionalPlan.DroppedOptionalChildren.Num(),
			2))
	{
		return false;
	}

	TestEqual(
		TEXT("Optional planner sorts kept optional children deterministically"),
		OptionalPlan.KeptChildRegionDebugPaths[0],
		FString(TEXT("ChildOptionalA")));
	TestEqual(
		TEXT("Optional planner sorts kept optional children deterministically"),
		OptionalPlan.KeptChildRegionDebugPaths[1],
		FString(TEXT("ChildOptionalB")));
	TestEqual(
		TEXT("Optional planner sorts dropped optional children deterministically"),
		OptionalPlan.DroppedOptionalChildren[0].ChildRegionDebugPath,
		FString(TEXT("ChildOptionalC")));
	TestEqual(
		TEXT("Optional planner sorts dropped optional children deterministically"),
		OptionalPlan.DroppedOptionalChildren[1].ChildRegionDebugPath,
		FString(TEXT("ChildOptionalD")));
	return true;
}
