// Copyright 2026 Spotted Loaf Studio

#include "Layout/Planning/LayoutNoiseCoordinateLibrary.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutNoiseCoordinateLibraryConvertsBlockWorldPositionTest,
	"PorismExtension.Layout.Planning.NoiseCoordinates.ConvertsBlockWorldPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutNoiseCoordinateLibraryConvertsBlockWorldPositionTest::RunTest(const FString& Parameters)
{
	FLayoutNoiseCoordinateSettings Settings;
	Settings.BaseBlockSize = 100;
	Settings.NoiseScale = FVector(2.0, 0.5, 1.0);
	Settings.NoiseCoordinateOffset = FIntVector(200, -100, 0);

	const FVector NoiseCoordinate = ULayoutNoiseCoordinateLibrary::BlockWorldPositionToNoiseCoordinate(
		FIntVector(10, 20, 30),
		Settings);

	TestEqual(TEXT("X noise coordinate includes offset, base block size, and scale"), NoiseCoordinate.X, 0.24);
	TestEqual(TEXT("Y noise coordinate includes offset, base block size, and scale"), NoiseCoordinate.Y, 0.095);
	TestEqual(TEXT("Z noise coordinate includes base block size and scale"), NoiseCoordinate.Z, 0.3);

	Settings.NoiseCoordinateOffset = FIntVector(50, -50, 0);
	const FVector QuantizedOffsetCoordinate = ULayoutNoiseCoordinateLibrary::BlockWorldPositionToNoiseCoordinate(
		FIntVector(10, 20, 30),
		Settings);
	TestEqual(TEXT("Sub-block positive offset is truncated like native generation"), QuantizedOffsetCoordinate.X, 0.2);
	TestEqual(TEXT("Sub-block negative offset is truncated like native generation"), QuantizedOffsetCoordinate.Y, 0.1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutNoiseCoordinateLibraryClampsInvalidBaseBlockSizeTest,
	"PorismExtension.Layout.Planning.NoiseCoordinates.ClampsInvalidBaseBlockSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutNoiseCoordinateLibraryClampsInvalidBaseBlockSizeTest::RunTest(const FString& Parameters)
{
	FLayoutNoiseCoordinateSettings Settings;
	Settings.BaseBlockSize = 0;
	Settings.NoiseScale = FVector::OneVector;

	const FVector2D NoiseCoordinate = ULayoutNoiseCoordinateLibrary::BlockWorldXYToNoiseCoordinate(
		FIntPoint(100, 200),
		Settings);

	TestEqual(TEXT("Invalid base block size is clamped for X conversion"), NoiseCoordinate.X, 0.01);
	TestEqual(TEXT("Invalid base block size is clamped for Y conversion"), NoiseCoordinate.Y, 0.02);

	return true;
}
