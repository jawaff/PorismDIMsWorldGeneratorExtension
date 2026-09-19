// Copyright 2026 Spotted Loaf Studio

#include "Biome/Noise/FoundationFastNoiseLibrary.h"

namespace
{
	FNodeLink MakeFoundationZero(UFastNoiseEditor* Editor)
	{
		return Editor != nullptr ? Editor->Constant(0.0f) : FNodeLink();
	}

	bool IsFoundationNodeValid(const FNodeLink Link)
	{
		return Link.Node != nullptr;
	}

	FNodeLink BuildFoundationPlanarDistanceSquaredXY(UFastNoiseEditor* Editor, const FVector2D CenterXY)
	{
		FNodeLink Distance = Editor->DistanceToPoint(
			FastNoiseDistanceFunction::EuclideanSquared,
			FVector(CenterXY.X, CenterXY.Y, 0.0));
		return Editor->RemoveDimension(Distance, FastNoiseDim::Z);
	}

	FNodeLink BuildFoundationPlanarSimplexDetail(
		UFastNoiseEditor* Editor,
		const float DomainScale,
		const float Amplitude,
		const int32 SeedOffset)
	{
		if (Amplitude <= 0.0f || DomainScale <= 0.0f)
		{
			return MakeFoundationZero(Editor);
		}

		FNodeLink Source = Editor->OpenSimplex2();
		FNodeLink SeededSource = Editor->SeedOffset(Source, SeedOffset);
		FNodeLink PlanarSource = Editor->DomainAxisScale(SeededSource, FVector(DomainScale, DomainScale, 0.0));
		FNodeLink Gain = Editor->Constant(0.5f);
		FNodeLink WeightedStrength = Editor->Constant(0.15f);
		FNodeLink Fractal = Editor->FractalFBm(PlanarSource, Gain, WeightedStrength, 3, 2.0f);
		return Editor->MultiplyFloat(Fractal, Amplitude);
	}
}

FNodeLink UFoundationFastNoiseLibrary::BuildFloatingIslandBody(
	UFastNoiseEditor* Editor,
	const FFloatingIslandBodyNoiseSettings& Settings)
{
	return BuildFloatingIslandBodyWithSurfaceOffset(Editor, Settings, FNodeLink());
}

FNodeLink UFoundationFastNoiseLibrary::BuildFloatingIslandBodyWithSurfaceOffset(
	UFastNoiseEditor* Editor,
	const FFloatingIslandBodyNoiseSettings& Settings,
	FNodeLink SurfaceHeightOffset)
{
	if (Editor == nullptr
		|| Settings.TopRadius <= 0.0f
		|| Settings.TopHeight < 0.0f
		|| Settings.BottomDepth < 0.0f
		|| Settings.RimThickness <= 0.0f)
	{
		return MakeFoundationZero(Editor);
	}

	const FVector2D CenterXY(Settings.Center.X, Settings.Center.Y);
	const float TopZ = Settings.Center.Z + Settings.TopHeight;
	const float BottomZ = Settings.Center.Z - Settings.BottomDepth;

	FNodeLink PositionZ = Editor->PositionOutput(FVector(0.0, 0.0, 1.0), FVector::ZeroVector);
	FNodeLink PlanarDistanceSquared = BuildFoundationPlanarDistanceSquaredXY(Editor, CenterXY);

	FNodeLink TopSurface = Editor->Subtract(Editor->Constant(TopZ), PositionZ);
	FNodeLink SurfaceNoise = BuildFoundationPlanarSimplexDetail(
		Editor,
		Settings.SurfaceNoiseScale,
		Settings.SurfaceNoiseAmplitude,
		Settings.SeedOffset + 11);
	if (IsFoundationNodeValid(SurfaceNoise))
	{
		TopSurface = Editor->Add(TopSurface, SurfaceNoise);
	}
	if (IsFoundationNodeValid(SurfaceHeightOffset))
	{
		TopSurface = Editor->Add(TopSurface, SurfaceHeightOffset);
	}

	FNodeLink RimNoise = BuildFoundationPlanarSimplexDetail(
		Editor,
		Settings.RimNoiseScale,
		Settings.RimNoiseAmplitude,
		Settings.SeedOffset + 29);
	FNodeLink RadiusSquared = Editor->Constant(Settings.TopRadius * Settings.TopRadius);
	if (IsFoundationNodeValid(RimNoise))
	{
		RadiusSquared = Editor->Add(RadiusSquared, Editor->MultiplyFloat(RimNoise, Settings.TopRadius * 2.0f));
	}

	if (Settings.SideBulge > 0.0f)
	{
		const float BodyHeight = FMath::Max(UE_KINDA_SMALL_NUMBER, TopZ - BottomZ);
		FNodeLink TopFade = Editor->Subtract(Editor->Constant(TopZ), PositionZ);
		FNodeLink BottomFade = Editor->Subtract(PositionZ, Editor->Constant(BottomZ));
		FNodeLink BulgeMask = Editor->MaxFloat(Editor->MinFloat(Editor->DivideFloat(Editor->Min(TopFade, BottomFade), BodyHeight * 0.5f), 1.0f), 0.0f);
		RadiusSquared = Editor->Add(
			RadiusSquared,
			Editor->MultiplyFloat(BulgeMask, (Settings.TopRadius * Settings.SideBulge * 2.0f) + (Settings.SideBulge * Settings.SideBulge)));
	}
	FNodeLink RadialSide = Editor->Subtract(RadiusSquared, PlanarDistanceSquared);

	FNodeLink NormalizedDistance = Editor->MultiplyFloat(
		PlanarDistanceSquared,
		1.0f / (Settings.TopRadius * Settings.TopRadius));
	FNodeLink ClampedNormalizedDistance = Editor->MaxFloat(Editor->MinFloat(NormalizedDistance, 1.0f), 0.0f);
	const float RimBottomZ = TopZ - Settings.RimThickness;
	FNodeLink UndersideCurve = ClampedNormalizedDistance;
	if (Settings.LowerConeSharpness > 0.0f)
	{
		FNodeLink ConeLikeCurve = Editor->Subtract(
			Editor->MultiplyFloat(ClampedNormalizedDistance, 2.0f),
			Editor->Multiply(ClampedNormalizedDistance, ClampedNormalizedDistance));
		UndersideCurve = Editor->Add(
			Editor->MultiplyFloat(ClampedNormalizedDistance, 1.0f - FMath::Clamp(Settings.LowerConeSharpness, 0.0f, 1.0f)),
			Editor->MultiplyFloat(ConeLikeCurve, FMath::Clamp(Settings.LowerConeSharpness, 0.0f, 1.0f)));
	}
	FNodeLink UndersideHeight = Editor->MultiplyFloat(UndersideCurve, RimBottomZ - BottomZ);
	FNodeLink LowerSurface = Editor->Add(Editor->Constant(BottomZ), UndersideHeight);
	FNodeLink CenterFade = Editor->Subtract(Editor->Constant(1.0f), ClampedNormalizedDistance);
	if (Settings.BottomPointDepth > 0.0f)
	{
		LowerSurface = Editor->Subtract(LowerSurface, Editor->MultiplyFloat(CenterFade, Settings.BottomPointDepth));
	}
	FNodeLink UndersideNoise = BuildFoundationPlanarSimplexDetail(
		Editor,
		Settings.UndersideNoiseScale,
		Settings.UndersideNoiseAmplitude,
		Settings.SeedOffset + 47);
	if (IsFoundationNodeValid(UndersideNoise))
	{
		FNodeLink DownwardOnlyNoise = Editor->Max(UndersideNoise, Editor->Constant(0.0f));
		DownwardOnlyNoise = Editor->MultiplyFloat(DownwardOnlyNoise, 2.0f);
		FNodeLink RimFade = Editor->Subtract(Editor->Constant(1.0f), NormalizedDistance);
		FNodeLink RimFadeLimit = Editor->MultiplyFloat(RimFade, Settings.UndersideNoiseAmplitude);
		FNodeLink DownwardProtrusion = Editor->Min(DownwardOnlyNoise, RimFadeLimit);
		LowerSurface = Editor->Subtract(LowerSurface, DownwardProtrusion);
	}
	FNodeLink BottomSpikeNoise = BuildFoundationPlanarSimplexDetail(
		Editor,
		Settings.BottomSpikeNoiseScale,
		Settings.BottomSpikeNoiseAmplitude,
		Settings.SeedOffset + 59);
	if (IsFoundationNodeValid(BottomSpikeNoise))
	{
		FNodeLink DownwardOnlySpikeNoise = Editor->Max(BottomSpikeNoise, Editor->Constant(0.0f));
		DownwardOnlySpikeNoise = Editor->MultiplyFloat(DownwardOnlySpikeNoise, 2.0f);
		FNodeLink BottomSpikeLimit = Editor->MultiplyFloat(CenterFade, Settings.BottomSpikeNoiseAmplitude);
		FNodeLink DownwardSpike = Editor->Min(DownwardOnlySpikeNoise, BottomSpikeLimit);
		LowerSurface = Editor->Subtract(LowerSurface, DownwardSpike);
	}
	FNodeLink LowerCurve = Editor->Subtract(PositionZ, LowerSurface);

	FNodeLink VerticalBody = Editor->Min(TopSurface, LowerCurve);
	return Editor->Min(RadialSide, VerticalBody);
}
