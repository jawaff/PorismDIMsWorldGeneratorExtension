// Copyright 2026 Spotted Loaf Studio

#include "Biome/Noise/ReservationFastNoiseLibrary.h"
#include "Misc/AutomationTest.h"

#include <vector>

namespace
{
	UFastNoiseEditor* CreateTestFastNoiseEditor(std::vector<FNodeLink>& Nodes)
	{
		UFastNoiseEditor* const Editor = NewObject<UFastNoiseEditor>(GetTransientPackage());
		Editor->Nodes = &Nodes;
		return Editor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReservationFastNoiseLibraryBuildsSphereReservationTest,
	"PorismExtension.Biome.ReservationNoise.FastNoise.BuildsSphereReservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReservationFastNoiseLibraryBuildsSphereReservationTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);

	const FNodeLink Sphere = UReservationFastNoiseLibrary::BuildSphereReservation(
		Editor,
		FVector(1.0, 2.0, 3.0),
		64.0f);

	TestTrue(TEXT("Sphere reservation returns a valid node"), UReservationFastNoiseLibrary::IsReservationNoiseLinkValid(Sphere));
	TestTrue(TEXT("Sphere reservation subtracts distance from radius"), Sphere.Node->Flow.StartsWith(TEXT("Subtract")));
	TestTrue(TEXT("Sphere reservation contains radius constant"), Sphere.Node->Flow.Contains(TEXT("Constant64")));
	TestTrue(TEXT("Sphere reservation contains distance node"), Sphere.Node->Flow.Contains(TEXT("DistanceToPoint")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReservationFastNoiseLibraryBuildsRoundReservationXYTest,
	"PorismExtension.Biome.ReservationNoise.FastNoise.BuildsRoundReservationXY",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReservationFastNoiseLibraryBuildsRoundReservationXYTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);

	const FNodeLink RoundFootprint = UReservationFastNoiseLibrary::BuildRoundReservationXY(
		Editor,
		FVector2D(12.0, -8.0),
		32.0f);

	TestTrue(TEXT("Round XY reservation returns a valid node"), UReservationFastNoiseLibrary::IsReservationNoiseLinkValid(RoundFootprint));
	TestTrue(TEXT("Round XY reservation subtracts planar distance from radius"), RoundFootprint.Node->Flow.StartsWith(TEXT("Subtract")));
	TestTrue(TEXT("Round XY reservation contains domain axis scale"), RoundFootprint.Node->Flow.Contains(TEXT("DomainAxisScale")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReservationFastNoiseLibraryBuildsMirroredRoundReservationsTest,
	"PorismExtension.Biome.ReservationNoise.FastNoise.BuildsMirroredRoundReservations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReservationFastNoiseLibraryBuildsMirroredRoundReservationsTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);

	const FNodeLink SideA = UReservationFastNoiseLibrary::BuildMirroredRoundReservationXY(
		Editor,
		FVector2D::ZeroVector,
		EBiomeReservationMirrorAxis::X,
		64.0f,
		16.0f,
		EBiomeReservationOwnershipRole::SideA);
	const FNodeLink NeutralPair = UReservationFastNoiseLibrary::BuildMirroredRoundReservationXY(
		Editor,
		FVector2D::ZeroVector,
		EBiomeReservationMirrorAxis::Y,
		64.0f,
		16.0f,
		EBiomeReservationOwnershipRole::Neutral);

	TestTrue(TEXT("Side-specific mirrored reservation returns a valid node"), UReservationFastNoiseLibrary::IsReservationNoiseLinkValid(SideA));
	TestTrue(TEXT("Side-specific mirrored reservation is one round footprint"), SideA.Node->Flow.StartsWith(TEXT("Subtract")));
	TestTrue(TEXT("Neutral mirrored reservation returns a valid node"), UReservationFastNoiseLibrary::IsReservationNoiseLinkValid(NeutralPair));
	TestTrue(TEXT("Neutral mirrored reservation unions both sides"), NeutralPair.Node->Flow.StartsWith(TEXT("Max")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReservationFastNoiseLibraryBuildsRepeatedCellReservationTest,
	"PorismExtension.Biome.ReservationNoise.FastNoise.BuildsRepeatedCellReservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReservationFastNoiseLibraryBuildsRepeatedCellReservationTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);

	const FNodeLink RepeatedCells = UReservationFastNoiseLibrary::BuildRepeatedCellReservationXY(
		Editor,
		0.02f,
		0.2f,
		37);

	TestTrue(TEXT("Repeated cell reservation returns a valid node"), UReservationFastNoiseLibrary::IsReservationNoiseLinkValid(RepeatedCells));
	TestTrue(TEXT("Repeated cell reservation subtracts nearest-cell distance from radius"), RepeatedCells.Node->Flow.StartsWith(TEXT("Subtract")));
	TestTrue(TEXT("Repeated cell reservation contains cellular distance"), RepeatedCells.Node->Flow.Contains(TEXT("CellularDistance")));
	TestTrue(TEXT("Repeated cell reservation contains seed offset"), RepeatedCells.Node->Flow.Contains(TEXT("SeedOffset")));
	TestTrue(TEXT("Repeated cell reservation contains planar scaling"), RepeatedCells.Node->Flow.Contains(TEXT("DomainAxisScale")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReservationFastNoiseLibraryComposesUnionAndCarveTest,
	"PorismExtension.Biome.ReservationNoise.FastNoise.ComposesUnionAndCarve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReservationFastNoiseLibraryComposesUnionAndCarveTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);

	const FNodeLink Base = UReservationFastNoiseLibrary::BuildSphereReservation(
		Editor,
		FVector::ZeroVector,
		128.0f);
	const FNodeLink ReservationA = UReservationFastNoiseLibrary::BuildRoundReservationXY(
		Editor,
		FVector2D(-32.0, 0.0),
		16.0f);
	const FNodeLink ReservationB = UReservationFastNoiseLibrary::BuildUniformCubeReservation(
		Editor,
		FVector(32.0, 0.0, 0.0),
		16.0f);

	const FNodeLink Union = UReservationFastNoiseLibrary::UnionReservations(
		Editor,
		{ReservationA, ReservationB});
	const FNodeLink Carved = UReservationFastNoiseLibrary::CarveReservationFromBase(
		Editor,
		Base,
		Union);

	TestTrue(TEXT("Union returns a valid node"), UReservationFastNoiseLibrary::IsReservationNoiseLinkValid(Union));
	TestTrue(TEXT("Union uses Max composition"), Union.Node->Flow.StartsWith(TEXT("Max")));
	TestTrue(TEXT("Carved reservation returns a valid node"), UReservationFastNoiseLibrary::IsReservationNoiseLinkValid(Carved));
	TestTrue(TEXT("Carve uses Min composition"), Carved.Node->Flow.StartsWith(TEXT("Min")));
	TestTrue(TEXT("Carve negates the reservation union"), Carved.Node->Flow.Contains(TEXT("MultiplyFloat-1")));

	return true;
}
