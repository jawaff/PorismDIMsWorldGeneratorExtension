// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionScheduleSearchRuntime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildPlacementSeededLocalSnapOrderingTest,
	"PorismExtension.Layout.Solver.ChildPlacementOrdering.SeededLocalSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildPlacementSeededLocalSnapOrderingTest::RunTest(const FString& Parameters)
{
	using namespace LayoutRegionScheduleSolverPrivate;

	FSharedParentChildFace UnsupportedFace;
	UnsupportedFace.ParentCell = FIntVector::ZeroValue;
	UnsupportedFace.ChildLocalCell = FIntVector::ZeroValue;
	UnsupportedFace.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
	UnsupportedFace.ParentOwnerSupportRunLength = 3;
	UnsupportedFace.ChildOwnerSupportRunLength = 3;
	const int32 UnsupportedScore = ScoreSharedParentChildSeamOpportunity({UnsupportedFace});
	TestEqual(TEXT("Capability-only face receives no magnetism"), UnsupportedScore, 0);

	FSharedParentChildFace CertifiedFace = UnsupportedFace;
	CertifiedFace.ReciprocalDomainWitnessId = TEXT("ExactPair");
	const int32 CertifiedScore = ScoreSharedParentChildSeamOpportunity({CertifiedFace});
	TestTrue(TEXT("Exact reciprocal face receives magnetism"), CertifiedScore > 0);

	TArray<FCheapChildPlacementCandidateOrder> Candidates;
	Candidates.Add({0, FIntVector(0, 0, 0), 0, UnsupportedScore, 10});
	Candidates.Add({1, FIntVector(1, 0, 0), CertifiedScore, CertifiedScore, 30});
	Candidates.Add({2, FIntVector(5, 0, 0), 0, UnsupportedScore, 20});
	OrderSeededChildPlacementCandidatesWithLocalSnap(Candidates);

	TestEqual(TEXT("Nearby compatible seam snaps ahead of its seeded coarse value"), Candidates[0].CandidateIndex, 1);
	TestEqual(TEXT("Unsnapped seeded coarse value remains available after local snap"), Candidates[1].CandidateIndex, 0);
	TestEqual(TEXT("Distant candidate is not pulled forward by global magnetism"), Candidates[2].CandidateIndex, 2);

	TArray<FCheapChildPlacementCandidateOrder> NoSeamCandidates;
	NoSeamCandidates.Add({0, FIntVector(0, 0, 0), 500, 0, 30});
	NoSeamCandidates.Add({1, FIntVector(1, 0, 0), 0, 0, 10});
	OrderSeededChildPlacementCandidatesWithLocalSnap(NoSeamCandidates);
	TestEqual(TEXT("Without seam magnetism ordering follows deterministic seed rank"), NoSeamCandidates[0].CandidateIndex, 1);
	return true;
}
