// Copyright 2026 Spotted Loaf Studio

#include "Biome/Noise/FoundationFastNoiseLibrary.h"
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

	bool EvaluateNoiseAt(const FNodeLink Node, const FVector& NoiseCoordinate, float& OutValue)
	{
		if (Node.Node == nullptr || !Node.Node->BaseNode)
		{
			OutValue = 0.0f;
			return false;
		}

		OutValue = Node.Node->BaseNode->GenSingle3D(
			static_cast<float>(NoiseCoordinate.X),
			static_cast<float>(NoiseCoordinate.Y),
			static_cast<float>(NoiseCoordinate.Z),
			0);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFoundationFastNoiseLibraryBuildsFloatingIslandBodyTest,
	"PorismExtension.Biome.FoundationNoise.FastNoise.BuildsFloatingIslandBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationFastNoiseLibraryBuildsFloatingIslandBodyTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);

	FFloatingIslandBodyNoiseSettings Settings;
	Settings.Center = FVector(1.0, -2.0, 0.25);
	Settings.TopRadius = 2.5f;
	Settings.TopHeight = 0.4f;
	Settings.BottomDepth = 1.6f;
	Settings.RimThickness = 0.25f;
	Settings.RimNoiseAmplitude = 0.1f;
	Settings.SurfaceNoiseAmplitude = 0.05f;
	Settings.SeedOffset = 17;

	const FNodeLink Island = UFoundationFastNoiseLibrary::BuildFloatingIslandBody(Editor, Settings);

	TestTrue(TEXT("Floating island body returns a valid node"), Island.Node != nullptr);
	TestTrue(TEXT("Floating island body intersects radial side and vertical body"), Island.Node->Flow.StartsWith(TEXT("Min")));
	TestTrue(TEXT("Floating island body projects planar distance by removing Z"), Island.Node->Flow.Contains(TEXT("RemoveDimension")));
	TestTrue(TEXT("Floating island body uses squared planar distance"), Island.Node->Flow.Contains(TEXT("DistanceToPoint1")));
	TestTrue(TEXT("Floating island body contains Z position output"), Island.Node->Flow.Contains(TEXT("PositionOutput")));
	TestFalse(TEXT("Floating island body avoids PowFloat underside artifacts"), Island.Node->Flow.Contains(TEXT("PowFloatFloat")));
	TestFalse(TEXT("Floating island body avoids MinSmooth interior artifacts"), Island.Node->Flow.Contains(TEXT("MinSmoothFloat")));
	TestTrue(TEXT("Floating island body contains simplex detail noise"), Island.Node->Flow.Contains(TEXT("OpenSimplex2")));
	TestTrue(TEXT("Floating island body contains seeded detail noise"), Island.Node->Flow.Contains(TEXT("SeedOffset")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFoundationFastNoiseLibraryFillsCenterColumnTest,
	"PorismExtension.Biome.FoundationNoise.FastNoise.FillsCenterColumn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationFastNoiseLibraryFillsCenterColumnTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);

	FFloatingIslandBodyNoiseSettings Settings;
	Settings.Center = FVector::ZeroVector;
	Settings.TopRadius = 1.2f;
	Settings.TopHeight = 0.3f;
	Settings.BottomDepth = 1.25f;
	Settings.RimThickness = 0.2f;
	Settings.RimNoiseAmplitude = 0.0f;
	Settings.SurfaceNoiseAmplitude = 0.0f;
	Settings.UndersideNoiseAmplitude = 0.0f;

	const FNodeLink Island = UFoundationFastNoiseLibrary::BuildFloatingIslandBody(Editor, Settings);

	float AtSurface = 0.0f;
	float CenterMiddle = 0.0f;
	float CenterDeep = 0.0f;
	float BelowIsland = 0.0f;
	float AboveIsland = 0.0f;
	TestTrue(TEXT("Floating island samples at top surface"), EvaluateNoiseAt(Island, FVector(0.0, 0.0, 0.0), AtSurface));
	TestTrue(TEXT("Floating island samples center middle"), EvaluateNoiseAt(Island, FVector(0.0, 0.0, -0.6), CenterMiddle));
	TestTrue(TEXT("Floating island samples center deep"), EvaluateNoiseAt(Island, FVector(0.0, 0.0, -1.0), CenterDeep));
	TestTrue(TEXT("Floating island samples below island"), EvaluateNoiseAt(Island, FVector(0.0, 0.0, -1.6), BelowIsland));
	TestTrue(TEXT("Floating island samples above island"), EvaluateNoiseAt(Island, FVector(0.0, 0.0, 0.45), AboveIsland));

	TestTrue(TEXT("Center column remains solid near the playable surface"), AtSurface > 0.0f);
	TestTrue(TEXT("Center column remains solid through the island body"), CenterMiddle > 0.0f);
	TestTrue(TEXT("Center column remains solid above the bottom point"), CenterDeep > 0.0f);
	TestTrue(TEXT("Center column is air below the island"), BelowIsland < 0.0f);
	TestTrue(TEXT("Center column is air above the island"), AboveIsland < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFoundationFastNoiseLibraryReturnsZeroForInvalidFloatingIslandSettingsTest,
	"PorismExtension.Biome.FoundationNoise.FastNoise.ReturnsZeroForInvalidFloatingIslandSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationFastNoiseLibraryReturnsZeroForInvalidFloatingIslandSettingsTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);

	FFloatingIslandBodyNoiseSettings Settings;
	Settings.TopRadius = 0.0f;

	const FNodeLink Island = UFoundationFastNoiseLibrary::BuildFloatingIslandBody(Editor, Settings);

	TestTrue(TEXT("Invalid floating island settings return a valid fallback node"), Island.Node != nullptr);
	TestTrue(TEXT("Invalid floating island settings return constant zero"), Island.Node->Flow.StartsWith(TEXT("Constant0")));

	return true;
}
