// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"

#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;

	UObject* CreateChildValidationTestOuter(const TCHAR* BaseName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return CreatePackage(*PackageName);
	}


	FLayoutRegionSolveRequest BuildSingleCellRequest(
		ULayoutProfileAsset* Profile,
		const int32 Seed,
		const FString& RegionPath,
		const FIntVector& Cell)
	{
		FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, Seed, RegionPath);
		/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
		Request.FootprintSize = FIntPoint(1, 1);
		FLayoutPlannedCell& PlannedCell = Request.PlannedCells.AddDefaulted_GetRef();
		PlannedCell.Cell = Cell;
		PlannedCell.Intent = ELayoutCellIntent::Boundary;
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildCommitmentRejectsGrandchildDependentCapabilityTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.RejectsGrandchildDependentCapability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildCommitmentRejectsGrandchildDependentCapabilityTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateChildValidationTestOuter(TEXT("LayoutChildCommitmentValidation"));


	ULayoutProfileAsset* ParentProfile = CreateProfile(Outer, TEXT("ParentProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	ULayoutProfileAsset* ChildProfile = CreateProfile(Outer, TEXT("ChildProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	ULayoutProfileAsset* GrandchildProfile = CreateProfile(Outer, TEXT("GrandchildProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);

	FLayoutRegionSolveRequest ParentRequest = BuildSingleCellRequest(ParentProfile, 101, TEXT("Parent"), FIntVector(0, 0, 0));
	FLayoutRegionSolveRequest ChildRequest = BuildSingleCellRequest(ChildProfile, 202, TEXT("Child"), FIntVector(1, 0, 0));
	FLayoutRegionSolveRequest GrandchildRequest = BuildSingleCellRequest(GrandchildProfile, 303, TEXT("Grandchild"), FIntVector(2, 0, 0));

	FLayoutRegionSolveScheduleRequest ScheduleRequest;
	ScheduleRequest.RegionRequests = {ParentRequest, ChildRequest, GrandchildRequest};
	ScheduleRequest.Dependencies.Add({TEXT("Parent"), TEXT("Child")});
	ScheduleRequest.Dependencies.Add({TEXT("Grandchild"), TEXT("Child")});

	FLayoutDirectChildRegionCommitment& Commitment = ScheduleRequest.DirectChildCommitments.AddDefaulted_GetRef();
	Commitment.ParentRegionDebugPath = TEXT("Parent");
	Commitment.ChildRegionDebugPath = TEXT("Child");
	FLayoutCommittedEndpointAnchor& Anchor = Commitment.EndpointCommitments.AddDefaulted_GetRef();
	Anchor.CommitmentId = TEXT("ParentToChildEntry");
	Anchor.LocalCell = FIntVector(0, 0, 0);
	Anchor.FaceDirection = ELayoutFaceDirection::PosX;
	Anchor.ConnectionTag = LayoutGameplayTags::FaceOpen;
	Anchor.AllowedConnectionTags = MakeTags({LayoutGameplayTags::FaceOpen});
	Anchor.TraversalChannels = MakeTags({LayoutGameplayTags::TraversalPrimary});

	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionsSynchronously(ScheduleRequest);
	TestFalse(TEXT("Grandchild-dependent child capability is rejected"), ScheduleResult.bSucceeded);
	TestTrue(TEXT("Failure reason cites another region solve dependency"), ScheduleResult.FailureReason.Contains(TEXT("depend on another region solve")));

	return true;
}
