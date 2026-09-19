// Copyright 2026 Spotted Loaf Studio

#include "Layout/Planning/LayoutReservationPocketPlanning.h"
#include "Misc/AutomationTest.h"

namespace
{
	FLayoutReservationPocketSample MakePocketPlanningSample(
		const int32 GridX,
		const int32 GridY,
		const int32 BlockSpacing,
		const float NoiseValue)
	{
		FLayoutReservationPocketSample Sample;
		Sample.SampleGridXY = FIntPoint(GridX, GridY);
		Sample.BlockXY = FIntPoint(GridX * BlockSpacing, GridY * BlockSpacing);
		Sample.NoiseValue = NoiseValue;
		return Sample;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutReservationPocketPlanningFindsDisconnectedPocketsTest,
	"PorismExtension.Layout.Planning.ReservationPockets.FindsDisconnectedPockets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutReservationPocketPlanningFindsDisconnectedPocketsTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutReservationPocketSample> Samples;
	for (int32 Y = 0; Y < 3; ++Y)
	{
		for (int32 X = 0; X < 3; ++X)
		{
			Samples.Add(MakePocketPlanningSample(X, Y, 10, 1.0f));
		}
	}
	Samples.Add(MakePocketPlanningSample(6, 0, 10, 1.0f));
	Samples.Add(MakePocketPlanningSample(7, 0, 10, 1.0f));
	Samples.Add(MakePocketPlanningSample(3, 3, 10, -1.0f));

	FLayoutReservationPocketPlanningSettings Settings;
	Settings.EligibilityThreshold = 0.0f;

	const TArray<FLayoutReservationPocket> Pockets =
		ULayoutReservationPocketPlanningLibrary::FindConnectedReservationPockets(Samples, Settings);

	TestEqual(TEXT("Two disconnected pockets are discovered"), Pockets.Num(), 2);
	TestEqual(TEXT("First pocket contains the 3x3 sample area"), Pockets[0].SampleCount, 9);
	TestEqual(TEXT("First pocket centroid is the center sample in block space"), Pockets[0].CentroidBlockXY, FVector2D(10.0, 10.0));
	TestEqual(TEXT("First pocket keeps the eligible sample nearest the centroid on the discovered sample lattice"), Pockets[0].CentroidNearestSampleBlockXY, FIntPoint(10, 10));
	TestEqual(TEXT("First pocket approximate interior selects the center sample"), Pockets[0].ApproximateInteriorBlockXY, FIntPoint(10, 10));
	TestEqual(TEXT("Second pocket contains two samples"), Pockets[1].SampleCount, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutReservationPocketPlanningUsesCardinalConnectivityOnlyTest,
	"PorismExtension.Layout.Planning.ReservationPockets.UsesCardinalConnectivityOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutReservationPocketPlanningUsesCardinalConnectivityOnlyTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutReservationPocketSample> Samples;
	Samples.Add(MakePocketPlanningSample(0, 0, 16, 1.0f));
	Samples.Add(MakePocketPlanningSample(1, 1, 16, 1.0f));

	FLayoutReservationPocketPlanningSettings Settings;
	const TArray<FLayoutReservationPocket> Pockets =
		ULayoutReservationPocketPlanningLibrary::FindConnectedReservationPockets(Samples, Settings);
	TestEqual(TEXT("Diagonal-only adjacency does not merge pockets"), Pockets.Num(), 2);
	TestEqual(TEXT("Each diagonal sample remains its own pocket"), Pockets[0].SampleCount + Pockets[1].SampleCount, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutReservationPocketPlanningFiltersSmallPocketsTest,
	"PorismExtension.Layout.Planning.ReservationPockets.FiltersSmallPockets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutReservationPocketPlanningFiltersSmallPocketsTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutReservationPocketSample> Samples;
	Samples.Add(MakePocketPlanningSample(0, 0, 16, 1.0f));
	Samples.Add(MakePocketPlanningSample(1, 0, 16, 1.0f));
	Samples.Add(MakePocketPlanningSample(4, 0, 16, 1.0f));

	FLayoutReservationPocketPlanningSettings Settings;
	Settings.MinimumSampleCount = 2;

	const TArray<FLayoutReservationPocket> Pockets =
		ULayoutReservationPocketPlanningLibrary::FindConnectedReservationPockets(Samples, Settings);

	TestEqual(TEXT("Single-sample pockets are filtered out"), Pockets.Num(), 1);
	TestEqual(TEXT("Remaining pocket contains the connected pair"), Pockets[0].SampleCount, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutReservationPocketPlanningFiltersOversizedPocketsTest,
	"PorismExtension.Layout.Planning.ReservationPockets.FiltersOversizedPockets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutReservationPocketPlanningFiltersOversizedPocketsTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutReservationPocketSample> Samples;
	Samples.Add(MakePocketPlanningSample(0, 0, 16, 1.0f));
	Samples.Add(MakePocketPlanningSample(1, 0, 16, 1.0f));
	Samples.Add(MakePocketPlanningSample(2, 0, 16, 1.0f));
	Samples.Add(MakePocketPlanningSample(8, 0, 16, 1.0f));
	Samples.Add(MakePocketPlanningSample(9, 0, 16, 1.0f));

	FLayoutReservationPocketPlanningSettings Settings;
	Settings.MaximumSampleCount = 2;

	const TArray<FLayoutReservationPocket> Pockets =
		ULayoutReservationPocketPlanningLibrary::FindConnectedReservationPockets(Samples, Settings);

	TestEqual(TEXT("Oversized pockets are filtered out"), Pockets.Num(), 1);
	TestEqual(TEXT("Remaining pocket is the smaller connected pair"), Pockets[0].SampleCount, 2);
	TestEqual(TEXT("Remaining pocket starts at the smaller pair"), Pockets[0].MinSampleGridXY, FIntPoint(8, 0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutReservationPocketPlanningAppliesExclusionSamplesTest,
	"PorismExtension.Layout.Planning.ReservationPockets.AppliesExclusionSamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutReservationPocketPlanningAppliesExclusionSamplesTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutReservationPocketSample> Samples;
	for (int32 X = 0; X < 5; ++X)
	{
		Samples.Add(MakePocketPlanningSample(X, 0, 16, 1.0f));
	}

	TArray<FLayoutReservationPocketSample> ExclusionSamples;
	ExclusionSamples.Add(MakePocketPlanningSample(2, 0, 16, 1.0f));

	FLayoutReservationPocketPlanningSettings Settings;
	const TArray<FLayoutReservationPocket> Pockets =
		ULayoutReservationPocketPlanningLibrary::FindConnectedReservationPocketsWithExclusions(
			Samples,
			ExclusionSamples,
			Settings,
			0.0f);

	TestEqual(TEXT("Exclusion sample splits the connected reservation into two pockets"), Pockets.Num(), 2);
	TestEqual(TEXT("First split pocket contains the left pair"), Pockets[0].SampleCount, 2);
	TestEqual(TEXT("Second split pocket contains the right pair"), Pockets[1].SampleCount, 2);
	TestEqual(TEXT("Excluded center sample is absent from first pocket bounds"), Pockets[0].MaxSampleGridXY, FIntPoint(1, 0));
	TestEqual(TEXT("Excluded center sample is absent from second pocket bounds"), Pockets[1].MinSampleGridXY, FIntPoint(3, 0));

	return true;
}
