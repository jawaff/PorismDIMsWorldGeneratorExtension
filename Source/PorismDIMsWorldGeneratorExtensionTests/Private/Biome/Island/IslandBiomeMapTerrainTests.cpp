// Copyright 2026 Spotted Loaf Studio

#include "Biome/Island/IslandBiomeFastNoiseTestSupport.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIslandCylinderTerrainTest,
	"PorismExtension.Biome.IslandNoise.CylinderThicknessAndReservations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandCylinderTerrainTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* Strategy = CreateNoReservationHierarchyStrategy();
	auto& Provider = Strategy->RootFoundationProvider;
	auto& Payload = Provider.ProviderPayload.GetMutable<FIslandFoundationShapePayload>();
	Payload.IslandBody.bCylindrical = true;
	Payload.IslandBody.Center = FVector(0, 0, 2);
	Payload.IslandBody.TopRadius = 100;
	Payload.IslandBody.TopHeight = 1;
	Payload.IslandBody.BottomDepth = 1;
	Provider.TerrainProfile.InitializeAs<FFlatFoundationTerrainProfilePayload>();
	const auto Scale = ResolveTestScaleContext(Strategy);
	std::vector<FNodeLink> Nodes;
	auto* Editor = CreateTestFastNoiseEditor(Nodes);
	FNodeLink Density = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy, &Scale);
	for (int32 X : {0, 20, 60, 95})
	{
		int32 Solid = 0;
		for (int32 Z = -4; Z <= 5; ++Z)
		{
			float Value = 0;
			TestTrue(TEXT("Cylinder density evaluates"), EvaluateNoiseAtAuthoredBlock(Density, Scale, FVector(X, 0, Z), Value));
			TestEqual(TEXT("Exactly the two authored layers are solid"), Value < 0, Z == 0 || Z == 1);
			Solid += Value < 0;
		}
		TestEqual(TEXT("Thickness does not taper near the rim"), Solid, 2);
	}

	FReservationDefinition Reservation;
	Reservation.DebugName = TEXT("Spawn");
	Reservation.BiomeTag = TestReservationBiomeTag();
	FSurfaceAnchorReservationPayload Anchor;
	Anchor.Shape = ESurfaceAnchorReservationShape::Box;
	Anchor.BoxHalfExtent = FVector(8, 8, 18);
	Anchor.SurfaceHeightSolveMode = ESurfaceAnchorHeightSolveMode::CenterSample;
	Anchor.TerrainPayload.InitializeAs<FSurfaceAnchorFlatTerrainPayload>();
	Reservation.ReservationPayload.InitializeAs<FSurfaceAnchorReservationPayload>(Anchor);
	Provider.Reservations.Add(Reservation);
	FNodeLink Pad = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(Editor, Strategy, TestReservationBiomeTag(), &Scale);
	for (int32 Z = -8; Z < 5; ++Z)
	{
		float Value = 0;
		EvaluateNoiseAtAuthoredBlock(Pad, Scale, FVector(0, 0, Z), Value);
		TestEqual(TEXT("Reservation cannot thicken the slab"), Value < 0, Z == 0 || Z == 1);
	}

	Provider.Reservations.Reset();
	Provider.TerrainProfile.InitializeAs<FNoisyFoundationTerrainProfilePayload>();
	auto& Noise = Provider.TerrainProfile.GetMutable<FNoisyFoundationTerrainProfilePayload>();
	Noise.SurfaceNoiseAmplitude = 4;
	Noise.SurfaceNoiseScale = 1;
	Density = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy, &Scale);
	int32 MinTop = MAX_int32, MaxTop = MIN_int32;
	for (int32 X = -80; X <= 80; X += 10)
	{
		int32 Solid = 0, Top = MIN_int32;
		for (int32 Z = -10; Z <= 12; ++Z)
		{
			float Value = 0;
			EvaluateNoiseAtAuthoredBlock(Density, Scale, FVector(X, 13, Z), Value);
			if (Value < 0) { ++Solid; Top = Z; }
		}
		// Fractional profile height plus calibrated voxel boundaries can round to three samples.
		TestTrue(TEXT("Noise retains a finite thin slab"), Solid >= 2 && Solid <= 3);
		MinTop = FMath::Min(MinTop, Top); MaxTop = FMath::Max(MaxTop, Top);
	}
	TestTrue(TEXT("Cylinder supports non-flat terrain profiles"), MaxTop > MinTop);
	Payload.IslandBody.BottomDepth = -1;
	TestTrue(TEXT("Invalid cylinder thickness rejected"), Strategy->ValidateStrategy().HasErrors());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIslandCalderaTerrainTest,
	"PorismExtension.Biome.IslandNoise.CalderaTraversal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandCalderaTerrainTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* Strategy = CreateNoReservationHierarchyStrategy();
	auto& Provider = Strategy->RootFoundationProvider;
	auto& Payload = Provider.ProviderPayload.GetMutable<FIslandFoundationShapePayload>();
	Payload.IslandBody.Center = FVector(0, 0, 8);
	Payload.IslandBody.TopRadius = 165;
	Payload.BodyDetail.bEnableRimNoise = false;
	Payload.BodyDetail.bEnableUndersideNoise = false;
	Payload.BodyDetail.bEnableBottomSpikeNoise = false;
	Provider.TerrainProfile.InitializeAs<FCalderaFoundationTerrainProfilePayload>();
	TestFalse(TEXT("Caldera profile validates"), Strategy->ValidateStrategy().HasErrors());
	const auto Scale = ResolveTestScaleContext(Strategy);
	std::vector<FNodeLink> Nodes;
	auto* Editor = CreateTestFastNoiseEditor(Nodes);
	const FNodeLink Density = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy, &Scale);
	auto Height = [&](const double X, const double Y)
	{
		double Low = 0, High = 64;
		for (int32 Iteration = 0; Iteration < 16; ++Iteration)
		{
			const double Mid = (Low + High) * 0.5;
			float Value = 0;
			EvaluateNoiseAtAuthoredBlock(Density, Scale, FVector(X, Y, Mid), Value);
			if (Value < 0) Low = Mid; else High = Mid;
		}
		return (Low + High) * 0.5;
	};
	const double Floor = Height(0, 0);
	TestTrue(TEXT("Castle-pad corners remain on flat caldera floor"), FMath::Abs(Height(30, 30) - Floor) < 0.05);
	TestTrue(TEXT("Caldera floor is elevated above perimeter"), Floor - Height(150, 0) > 23);
	TestTrue(TEXT("Rim is above caldera floor"), Height(65, 0) - Floor > 7);
	for (int32 X = 2; X <= 150; X += 2)
	{
		const double Before = Height(X - 2, 0), After = Height(X, 0);
		TestTrue(*FString::Printf(TEXT("Radial approach below 30 degrees at X=%d, Z=%.3f -> %.3f"), X, Before, After), FMath::Abs(After - Before) / 2 < 0.5774);
	}
	Provider.TerrainProfile.GetMutable<FCalderaFoundationTerrainProfilePayload>().RimRadiusBlocks = 40;
	TestTrue(TEXT("Reversed caldera radii rejected"), Strategy->ValidateStrategy().HasErrors());
	return true;
}
