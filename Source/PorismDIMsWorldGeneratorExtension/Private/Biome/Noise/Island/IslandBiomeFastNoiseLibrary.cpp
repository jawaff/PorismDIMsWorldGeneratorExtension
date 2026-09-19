// Copyright 2026 Spotted Loaf Studio

#include "Biome/Noise/Island/IslandBiomeFastNoiseLibrary.h"

#include "Biome/Noise/Foundation/InfinitePlaneFoundationPayloads.h"
#include "Biome/Noise/Foundation/VoidFoundationPayloads.h"
#include "Biome/Noise/Foundation/FoundationTerrainProfileOps.h"
#include "Biome/Noise/Island/IslandBiomeStrategyPayloads.h"
#include "Biome/Noise/FoundationFastNoiseLibrary.h"
#include "Biome/Noise/ReservationFastNoiseLibrary.h"
#include "Biome/Noise/Reservation/SurfaceAnchorReservationPayloads.h"

#include <vector>

namespace
{
	constexpr float FoundationDomainCarveStrength = 4.0f;
	constexpr float InternalTerrainDensityScale = 15.0f;
	constexpr float InternalSurfaceSolidBias = 0.07f;
	constexpr float SurfaceAnchorFixedBoundaryBlendBlocks = 1.0f;
	constexpr float SurfaceAnchorSpherePlanarEdgeScaleFactor = 0.7f;
	constexpr float SurfaceAnchorSpherePlanarEdgeBias = 0.5f;
	constexpr float ReservationGenADomainGateStrength = 15.0f;
	constexpr float ReservationGenAOutsideAirCeiling = 0.05f;
	constexpr float AuthoredSurfaceTopFaceSolidSampleOffsetBlocks = -1.0f;
	constexpr float MinimumDomainTopAirClearanceBlocks = 1.0f;

	FNodeLink MakeIslandZero(UFastNoiseEditor* Editor)
	{
		return Editor != nullptr ? Editor->Constant(0.0f) : FNodeLink();
	}

	bool IsIslandNodeValid(const FNodeLink Link)
	{
		return Link.Node != nullptr;
	}

	FNodeLink BuildPlanarDistanceSquaredXY(UFastNoiseEditor* Editor, const FVector2D CenterXY)
	{
		FNodeLink Distance = Editor->DistanceToPoint(
			FastNoiseDistanceFunction::EuclideanSquared,
			FVector(CenterXY.X, CenterXY.Y, 0.0));
		return Editor->RemoveDimension(Distance, FastNoiseDim::Z);
	}

	FNodeLink BuildPlanarDistanceXY(UFastNoiseEditor* Editor, const FVector2D CenterXY)
	{
		FNodeLink Distance = Editor->DistanceToPoint(
			FastNoiseDistanceFunction::Euclidean,
			FVector(CenterXY.X, CenterXY.Y, 0.0));
		return Editor->RemoveDimension(Distance, FastNoiseDim::Z);
	}

	FResolvedWorldGenScaleContext ResolveFallbackScaleContext(const UBiomeStrategyData* Strategy)
	{
		return FWorldGenScaleContextResolver::Resolve(
			nullptr,
			Strategy != nullptr ? &Strategy->ScaleOverride : nullptr,
			Strategy != nullptr && Strategy->bUseScaleOverride);
	}

	const FIslandFoundationShapePayload* GetIslandFoundationPayload(const UBiomeStrategyData* Strategy)
	{
		return Strategy != nullptr ? Strategy->RootFoundationProvider.ProviderPayload.GetPtr<FIslandFoundationShapePayload>() : nullptr;
	}

	const FIslandFoundationShapePayload* GetIslandFoundationPayload(const FFoundationProviderDefinition& Provider)
	{
		return Provider.ProviderPayload.GetPtr<FIslandFoundationShapePayload>();
	}

	const FInfinitePlaneFoundationPayload* GetInfinitePlaneFoundationPayload(const FFoundationProviderDefinition& Provider)
	{
		return Provider.ProviderPayload.GetPtr<FInfinitePlaneFoundationPayload>();
	}

	const FVCutFoundationPayload* GetVCutFoundationPayload(const FFoundationProviderDefinition& Provider)
	{
		return Provider.ProviderPayload.GetPtr<FVCutFoundationPayload>();
	}

	const FSurfaceAnchorReservationPayload* GetSurfaceAnchorReservationPayload(const FReservationDefinition& Reservation)
	{
		return Reservation.ReservationPayload.GetPtr<FSurfaceAnchorReservationPayload>();
	}

	FSurfaceAnchorReservationPayload ResolveProviderLocalReservationPayload(
		const FIslandFoundationShapePayload& IslandPayload,
		const FSurfaceAnchorReservationPayload& ReservationPayload)
	{
		FSurfaceAnchorReservationPayload ResolvedPayload = ReservationPayload;
		ResolvedPayload.CenterXY += FVector2D(IslandPayload.IslandBody.Center.X, IslandPayload.IslandBody.Center.Y);
		return ResolvedPayload;
	}

	FVector GetPositiveVector(const FVector& Value, const FVector& Fallback)
	{
		return FVector(
			Value.X > 0.0 ? Value.X : Fallback.X,
			Value.Y > 0.0 ? Value.Y : Fallback.Y,
			Value.Z > 0.0 ? Value.Z : Fallback.Z);
	}

	FVector GetAnchorSphereRadii(const FSurfaceAnchorReservationPayload& Reservation)
	{
		return GetPositiveVector(Reservation.SphereRadii, FVector(35.0, 35.0, 18.0));
	}

	FVector GetAnchorBoxHalfExtent(const FSurfaceAnchorReservationPayload& Reservation)
	{
		return GetPositiveVector(Reservation.BoxHalfExtent, FVector(35.0, 35.0, 18.0));
	}

	FVector GetSpawnSphereRadii(const FSurfaceAnchorReservationPayload& Reservation)
	{
		const FVector Fallback = GetAnchorSphereRadii(Reservation) * 0.5;
		return GetPositiveVector(Reservation.SpawnSphereRadii, Fallback);
	}

	FVector GetSpawnBoxHalfExtent(const FSurfaceAnchorReservationPayload& Reservation)
	{
		const FVector Fallback = GetAnchorBoxHalfExtent(Reservation) * 0.5;
		return GetPositiveVector(Reservation.SpawnBoxHalfExtent, Fallback);
	}

	FFloatingIslandBodyNoiseSettings ConvertIslandBodyToNoiseSettings(
		const FIslandFoundationBodySettings& BlockSettings,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		FFloatingIslandBodyNoiseSettings NoiseSettings;
		NoiseSettings.Center = BlockSettings.Center;
		NoiseSettings.TopRadius = BlockSettings.TopRadius;
		NoiseSettings.TopHeight = BlockSettings.TopHeight;
		NoiseSettings.BottomDepth = BlockSettings.BottomDepth;
		NoiseSettings.RimThickness = BlockSettings.RimThickness;
		const FVector AuthoredBodyCenter(
			BlockSettings.Center.X,
			BlockSettings.Center.Y,
			BlockSettings.Center.Z - BlockSettings.TopHeight);
		NoiseSettings.Center = ScaleContext.AuthoredBlockPositionToNoise(AuthoredBodyCenter);

		const FVector RadiusNoise = ScaleContext.BlockDistanceToNoise(FVector(BlockSettings.TopRadius, BlockSettings.TopRadius, BlockSettings.TopRadius)).GetAbs();
		NoiseSettings.TopRadius = FMath::Max(UE_KINDA_SMALL_NUMBER, static_cast<float>((RadiusNoise.X + RadiusNoise.Y) * 0.5));
		NoiseSettings.TopHeight = FMath::Max(0.0f, static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(0.0, 0.0, BlockSettings.TopHeight)).Z)));
		NoiseSettings.BottomDepth = FMath::Max(0.0f, static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(0.0, 0.0, BlockSettings.BottomDepth)).Z)));
		NoiseSettings.RimThickness = FMath::Max(UE_KINDA_SMALL_NUMBER, static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(0.0, 0.0, BlockSettings.RimThickness)).Z)));
		NoiseSettings.SideBulge = FMath::Max(0.0f, static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(BlockSettings.SideBulgeBlocks, 0.0, 0.0)).X)));
		NoiseSettings.LowerConeSharpness = FMath::Clamp(BlockSettings.LowerConeSharpness, 0.0f, 1.0f);
		NoiseSettings.BottomPointDepth = FMath::Max(0.0f, static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(0.0, 0.0, BlockSettings.BottomPointDepthBlocks)).Z)));
		return NoiseSettings;
	}

	FFloatingIslandBodyNoiseSettings ConvertIslandBodyToNoiseSettings(
		const FFloatingIslandBodyNoiseSettings& BlockSettings,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		FFloatingIslandBodyNoiseSettings NoiseSettings;
		NoiseSettings.Center = BlockSettings.Center;
		NoiseSettings.TopRadius = BlockSettings.TopRadius;
		NoiseSettings.TopHeight = BlockSettings.TopHeight;
		NoiseSettings.BottomDepth = BlockSettings.BottomDepth;
		NoiseSettings.RimThickness = BlockSettings.RimThickness;
		const FVector AuthoredBodyCenter(
			BlockSettings.Center.X,
			BlockSettings.Center.Y,
			BlockSettings.Center.Z - BlockSettings.TopHeight);
		NoiseSettings.Center = ScaleContext.AuthoredBlockPositionToNoise(AuthoredBodyCenter);

		const FVector RadiusNoise = ScaleContext.BlockDistanceToNoise(FVector(BlockSettings.TopRadius, BlockSettings.TopRadius, BlockSettings.TopRadius)).GetAbs();
		NoiseSettings.TopRadius = FMath::Max(UE_KINDA_SMALL_NUMBER, static_cast<float>((RadiusNoise.X + RadiusNoise.Y) * 0.5));
		NoiseSettings.TopHeight = FMath::Max(0.0f, static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(0.0, 0.0, BlockSettings.TopHeight)).Z)));
		NoiseSettings.BottomDepth = FMath::Max(0.0f, static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(0.0, 0.0, BlockSettings.BottomDepth)).Z)));
		NoiseSettings.RimThickness = FMath::Max(UE_KINDA_SMALL_NUMBER, static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(0.0, 0.0, BlockSettings.RimThickness)).Z)));
		NoiseSettings.SideBulge = FMath::Max(0.0f, static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(BlockSettings.SideBulge, 0.0, 0.0)).X)));
		NoiseSettings.LowerConeSharpness = FMath::Clamp(BlockSettings.LowerConeSharpness, 0.0f, 1.0f);
		NoiseSettings.BottomPointDepth = FMath::Max(0.0f, static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(0.0, 0.0, BlockSettings.BottomPointDepth)).Z)));
		NoiseSettings.RimNoiseAmplitude = FMath::Max(0.0f, static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(BlockSettings.RimNoiseAmplitude, 0.0, 0.0)).X)));
		NoiseSettings.SurfaceNoiseAmplitude = FMath::Max(0.0f, static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(0.0, 0.0, BlockSettings.SurfaceNoiseAmplitude)).Z)));
		NoiseSettings.UndersideNoiseAmplitude = FMath::Max(0.0f, static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(0.0, 0.0, BlockSettings.UndersideNoiseAmplitude)).Z)));
		NoiseSettings.BottomSpikeNoiseAmplitude = FMath::Max(0.0f, static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(0.0, 0.0, BlockSettings.BottomSpikeNoiseAmplitude)).Z)));
		NoiseSettings.RimNoiseScale = BlockSettings.RimNoiseScale;
		NoiseSettings.SurfaceNoiseScale = BlockSettings.SurfaceNoiseScale;
		NoiseSettings.UndersideNoiseScale = BlockSettings.UndersideNoiseScale;
		NoiseSettings.BottomSpikeNoiseScale = BlockSettings.BottomSpikeNoiseScale;
		NoiseSettings.SeedOffset = BlockSettings.SeedOffset;
		return NoiseSettings;
	}

	FVector2D GetProviderTerrainProfileOriginXY(const FFoundationProviderDefinition& Provider)
	{
		if (const FIslandFoundationShapePayload* IslandPayload = GetIslandFoundationPayload(Provider))
		{
			return FVector2D(IslandPayload->IslandBody.Center.X, IslandPayload->IslandBody.Center.Y);
		}

		return FVector2D::ZeroVector;
	}

	FVector2D ConvertAuthoredCenterXYToNoise(
		const FVector2D AuthoredCenterXY,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		const FVector NoiseCenter = ScaleContext.AuthoredBlockPositionToNoise(FVector(AuthoredCenterXY.X, AuthoredCenterXY.Y, 0.0));
		return FVector2D(NoiseCenter.X, NoiseCenter.Y);
	}

	float ConvertBlockXDistanceToNoise(
		const float BlockDistance,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		return static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(BlockDistance, 0.0, 0.0)).X));
	}

	FVector ConvertBlockDistanceToNoiseAbs(
		const FVector BlockDistance,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		const FVector NoiseDistance = ScaleContext.BlockDistanceToNoise(BlockDistance);
		return FVector(FMath::Abs(NoiseDistance.X), FMath::Abs(NoiseDistance.Y), FMath::Abs(NoiseDistance.Z));
	}

	float ConvertBlockZDistanceToNoise(
		const float BlockDistance,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		return static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(0.0, 0.0, BlockDistance)).Z));
	}

	float GetIslandDomainRadiusPaddingBlocks(const FIslandFoundationShapePayload& IslandPayload)
	{
		return IslandPayload.DomainRadiusPadding
			+ FMath::Max(0.0f, IslandPayload.IslandBody.SideBulgeBlocks)
			+ (IslandPayload.BodyDetail.bEnableRimNoise ? FMath::Max(0.0f, IslandPayload.BodyDetail.RimNoiseAmplitude) : 0.0f);
	}

	float GetIslandDomainBottomPaddingBlocks(const FIslandFoundationShapePayload& IslandPayload)
	{
		return IslandPayload.DomainVerticalPadding
			+ FMath::Max(0.0f, IslandPayload.IslandBody.BottomPointDepthBlocks)
			+ (IslandPayload.BodyDetail.bEnableUndersideNoise ? FMath::Max(0.0f, IslandPayload.BodyDetail.UndersideNoiseAmplitude) : 0.0f)
			+ (IslandPayload.BodyDetail.bEnableBottomSpikeNoise ? FMath::Max(0.0f, IslandPayload.BodyDetail.BottomSpikeNoiseAmplitude) : 0.0f);
	}

	float ConvertBlockZOffsetToNoise(
		const float BlockOffset,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		return static_cast<float>(ScaleContext.BlockDistanceToNoise(FVector(0.0, 0.0, BlockOffset)).Z);
	}

	float GetSurfaceZLiftBlocks(const FSurfaceAnchorReservationPayload& ReservationPayload)
	{
		return ReservationPayload.SurfaceZLift;
	}

	FVector ExpandExtentForVoxelCenterCoverage(const FVector& AuthoredExtent)
	{
		return AuthoredExtent + FVector(0.5f);
	}

	FVector ExpandSphereRadiiForVoxelCenterCoverage(const FVector& AuthoredRadii)
	{
		return AuthoredRadii + FVector(0.0f, 0.0f, 0.5f);
	}

	struct FResolvedSurfaceAnchorTerrain
	{
		bool bUseSurfaceNoise = false;
		float SurfaceNoiseAmplitude = 0.0f;
		float SurfaceNoiseScale = 3.0f;
		int32 SurfaceNoiseSeedOffset = 83;
		bool bUseTerraces = false;
		float TerraceStepHeight = 1.0f;
		float TerraceSmoothness = 0.0f;
		bool bUseEdgeBlend = false;
		float EdgeBlendWidthBlocks = 0.0f;
	};

	FResolvedSurfaceAnchorTerrain ResolveSurfaceAnchorTerrain(const FSurfaceAnchorReservationPayload& ReservationPayload)
	{
		FResolvedSurfaceAnchorTerrain Resolved;

		if (const FSurfaceAnchorNoisyTerrainPayload* NoisyTerrain = ReservationPayload.TerrainPayload.GetPtr<FSurfaceAnchorNoisyTerrainPayload>())
		{
			Resolved.bUseSurfaceNoise = NoisyTerrain->SurfaceNoiseAmplitude > 0.0f && NoisyTerrain->SurfaceNoiseScale > 0.0f;
			Resolved.SurfaceNoiseAmplitude = NoisyTerrain->SurfaceNoiseAmplitude;
			Resolved.SurfaceNoiseScale = NoisyTerrain->SurfaceNoiseScale;
			Resolved.SurfaceNoiseSeedOffset = NoisyTerrain->SurfaceNoiseSeedOffset;
			Resolved.bUseTerraces = NoisyTerrain->bUseTerraces;
			Resolved.TerraceStepHeight = NoisyTerrain->TerraceStepHeight;
			Resolved.TerraceSmoothness = NoisyTerrain->TerraceSmoothness;
			return Resolved;
		}

		if (const FSurfaceAnchorBlendedSupportTerrainPayload* BlendedTerrain = ReservationPayload.TerrainPayload.GetPtr<FSurfaceAnchorBlendedSupportTerrainPayload>())
		{
			Resolved.bUseEdgeBlend = BlendedTerrain->EdgeBlendWidthBlocks > 0.0f;
			Resolved.EdgeBlendWidthBlocks = BlendedTerrain->EdgeBlendWidthBlocks;
			return Resolved;
		}

		return Resolved;
	}

	float GetMaxPositiveSurfaceZLiftBlocks(const FFoundationProviderDefinition& Provider)
	{
		float MaxLiftBlocks = 0.0f;
		for (const FReservationDefinition& Reservation : Provider.Reservations)
		{
			if (const FSurfaceAnchorReservationPayload* ReservationPayload = GetSurfaceAnchorReservationPayload(Reservation))
			{
				MaxLiftBlocks = FMath::Max(MaxLiftBlocks, FMath::Max(0.0f, GetSurfaceZLiftBlocks(*ReservationPayload)));
			}
		}

		return MaxLiftBlocks;
	}

	float GetMaxPositiveSurfaceZLiftBlocks(const UBiomeStrategyData* Strategy)
	{
		return Strategy != nullptr ? GetMaxPositiveSurfaceZLiftBlocks(Strategy->RootFoundationProvider) : 0.0f;
	}

	FNodeLink BuildIslandEnvelopeDomainForPayload(
		UFastNoiseEditor* Editor,
		const FIslandFoundationShapePayload& IslandPayload,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const float ExtraTopPaddingBlocks)
	{
		if (Editor == nullptr)
		{
			return MakeIslandZero(Editor);
		}

		const FFloatingIslandBodyNoiseSettings Body = ConvertIslandBodyToNoiseSettings(IslandPayload.IslandBody, ScaleContext);
		if (Body.TopRadius <= 0.0f
			|| Body.TopHeight < 0.0f
			|| Body.BottomDepth < 0.0f
			|| Body.RimThickness <= 0.0f)
		{
			return MakeIslandZero(Editor);
		}

		const FVector2D CenterXY(Body.Center.X, Body.Center.Y);
		const float TopPaddingBlocks = FMath::Max(IslandPayload.DomainVerticalPadding, MinimumDomainTopAirClearanceBlocks)
			+ FMath::Max(0.0f, ExtraTopPaddingBlocks);
		const float TopZ = Body.Center.Z + Body.TopHeight + ConvertBlockZDistanceToNoise(TopPaddingBlocks, ScaleContext);
		const float BottomZ = Body.Center.Z - Body.BottomDepth - ConvertBlockZDistanceToNoise(GetIslandDomainBottomPaddingBlocks(IslandPayload), ScaleContext);
		const float Radius = Body.TopRadius + ConvertBlockXDistanceToNoise(GetIslandDomainRadiusPaddingBlocks(IslandPayload), ScaleContext);

		FNodeLink PositionZ = Editor->PositionOutput(FVector(0.0, 0.0, 1.0), FVector::ZeroVector);
		FNodeLink PlanarDistanceSquared = BuildPlanarDistanceSquaredXY(Editor, CenterXY);
		FNodeLink RadiusSquared = Editor->Constant(Radius * Radius);
		FNodeLink RadialSide = Editor->Subtract(RadiusSquared, PlanarDistanceSquared);
		FNodeLink TopCap = Editor->Subtract(Editor->Constant(TopZ), PositionZ);
		FNodeLink NormalizedDistance = Editor->DivideFloat(PlanarDistanceSquared, Radius * Radius);
		FNodeLink UndersideHeight = Editor->MultiplyFloat(NormalizedDistance, TopZ - Body.RimThickness - BottomZ);
		FNodeLink LowerSurface = Editor->Add(Editor->Constant(BottomZ), UndersideHeight);
		FNodeLink LowerCurve = Editor->Subtract(PositionZ, LowerSurface);
		FNodeLink VerticalBody = Editor->Min(TopCap, LowerCurve);
		return Editor->Min(RadialSide, VerticalBody);
	}

	FNodeLink BuildInfinitePlaneDomainForPayload(
		UFastNoiseEditor* Editor,
		const FInfinitePlaneFoundationPayload& PlanePayload,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const float ExtraTopPaddingBlocks)
	{
		if (Editor == nullptr || PlanePayload.DomainBottomDepth <= 0.0f)
		{
			return MakeIslandZero(Editor);
		}

		const float SurfaceZNoise = static_cast<float>(ScaleContext.AuthoredBlockPositionToNoise(FVector(0.0, 0.0, PlanePayload.SurfaceZ)).Z);
		const float TopPaddingBlocks = FMath::Max(PlanePayload.DomainTopPadding, MinimumDomainTopAirClearanceBlocks)
			+ FMath::Max(0.0f, ExtraTopPaddingBlocks);
		const float TopZ = SurfaceZNoise + ConvertBlockZDistanceToNoise(TopPaddingBlocks, ScaleContext);
		const float BottomZ = SurfaceZNoise - ConvertBlockZDistanceToNoise(PlanePayload.DomainBottomDepth, ScaleContext);
		FNodeLink PositionZ = Editor->PositionOutput(FVector(0.0, 0.0, 1.0), FVector::ZeroVector);
		FNodeLink VerticalDomain = Editor->Min(
			Editor->Subtract(Editor->Constant(TopZ), PositionZ),
			Editor->Subtract(PositionZ, Editor->Constant(BottomZ)));
		FNodeLink DomainMask;
		if (const FInfiniteFoundationNoisePatchDomainMaskPayload* NoisePatch = PlanePayload.DomainMask.GetPtr<FInfiniteFoundationNoisePatchDomainMaskPayload>())
		{
			if (NoisePatch->FrequencyScale <= 0.0f)
			{
				return MakeIslandZero(Editor);
			}

			FNodeLink Source = Editor->OpenSimplex2();
			FNodeLink SeededSource = Editor->SeedOffset(Source, NoisePatch->SeedOffset);
			FNodeLink PlanarSource = Editor->DomainAxisScale(SeededSource, FVector(NoisePatch->FrequencyScale, NoisePatch->FrequencyScale, 0.0));
			FNodeLink Gain = Editor->Constant(0.5f);
			FNodeLink WeightedStrength = Editor->Constant(0.25f);
			FNodeLink PatchNoise = Editor->FractalFBm(PlanarSource, Gain, WeightedStrength, 4, 2.0f);
			DomainMask = NoisePatch->bInvert
				? Editor->Subtract(Editor->Constant(NoisePatch->Threshold), PatchNoise)
				: Editor->Subtract(PatchNoise, Editor->Constant(NoisePatch->Threshold));
			if (NoisePatch->EdgeSoftness > UE_KINDA_SMALL_NUMBER)
			{
				DomainMask = Editor->DivideFloat(DomainMask, NoisePatch->EdgeSoftness);
			}
		}
		else if (const FInfiniteFoundationRepeatedHolesDomainMaskPayload* RepeatedHoles = PlanePayload.DomainMask.GetPtr<FInfiniteFoundationRepeatedHolesDomainMaskPayload>())
		{
			const float CellSize = FMath::Max(
				UE_KINDA_SMALL_NUMBER,
				static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(RepeatedHoles->CellSizeBlocks, 0.0, 0.0)).X)));
			const float HoleRadius = FMath::Max(
				UE_KINDA_SMALL_NUMBER,
				static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(RepeatedHoles->HoleRadiusBlocks, 0.0, 0.0)).X)));
			const float CellFrequency = 1.0f / CellSize;
			const float ScaledHoleRadius = HoleRadius * CellFrequency;
			FNodeLink HoleMask = UReservationFastNoiseLibrary::BuildRepeatedCellReservationXYWithDistance(
				Editor,
				CellFrequency,
				ScaledHoleRadius,
				RepeatedHoles->SeedOffset,
				RepeatedHoles->HoleShape == EInfiniteFoundationRepeatedHoleShape::Square
					? FastNoiseDistanceFunction::MaxAxis
					: FastNoiseDistanceFunction::Euclidean);
			DomainMask = RepeatedHoles->bInvert
				? HoleMask
				: Editor->MultiplyFloat(HoleMask, -1.0f);
			if (RepeatedHoles->EdgeSoftnessBlocks > UE_KINDA_SMALL_NUMBER)
			{
				const float EdgeSoftness = FMath::Max(
					UE_KINDA_SMALL_NUMBER,
					static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(RepeatedHoles->EdgeSoftnessBlocks, 0.0, 0.0)).X)));
				DomainMask = Editor->DivideFloat(DomainMask, EdgeSoftness * CellFrequency);
			}
		}

		return IsIslandNodeValid(DomainMask)
			? Editor->Min(VerticalDomain, DomainMask)
			: VerticalDomain;
	}

	FNodeLink BuildAbsoluteNode(UFastNoiseEditor* Editor, FNodeLink Value)
	{
		return Editor->Max(Value, Editor->MultiplyFloat(Value, -1.0f));
	}

	FNodeLink BuildVCutOrganicEdgeOffset(
		UFastNoiseEditor* Editor,
		const FVCutFoundationPayload& Payload,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		if (Editor == nullptr
			|| !Payload.bEnableOrganicVariation
			|| Payload.EdgeNoiseAmplitudeBlocks <= 0.0f
			|| Payload.EdgeNoiseScale <= 0.0f)
		{
			return MakeIslandZero(Editor);
		}

		FNodeLink Source = Editor->SeedOffset(Editor->OpenSimplex2(), Payload.OrganicSeedOffset);
		if (Payload.bEnableDomainWarp && Payload.DomainWarpAmplitudeBlocks > 0.0f && Payload.DomainWarpFrequency > 0.0f)
		{
			Source = Editor->DomainWarpGradient(
				Source,
				ConvertBlockXDistanceToNoise(Payload.DomainWarpAmplitudeBlocks, ScaleContext),
				Payload.DomainWarpFrequency);
		}

		FNodeLink PlanarSource = Editor->DomainAxisScale(Source, FVector(Payload.EdgeNoiseScale, Payload.EdgeNoiseScale, 0.0));
		FNodeLink Gain = Editor->Constant(0.55f);
		FNodeLink WeightedStrength = Editor->Constant(0.35f);
		FNodeLink Ridged = Editor->FractalRidged(PlanarSource, Gain, WeightedStrength, 4, 2.0f);
		FNodeLink NormalizedRidges = Editor->Remap(Ridged, 0.05f, 0.4f, 0.0f, 1.0f);
		NormalizedRidges = Editor->MaxFloat(Editor->MinFloat(NormalizedRidges, 1.0f), 0.0f);
		FNodeLink Centered = Editor->SubtractFloat(Editor->MultiplyFloat(NormalizedRidges, 2.0f), 1.0f);
		return Editor->MultiplyFloat(Centered, ConvertBlockXDistanceToNoise(Payload.EdgeNoiseAmplitudeBlocks, ScaleContext));
	}

	FNodeLink BuildVCutCenterlineWanderOffset(
		UFastNoiseEditor* Editor,
		const FVCutFoundationPayload& Payload,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		if (Editor == nullptr
			|| !Payload.bEnableOrganicVariation
			|| Payload.CenterlineWanderAmplitudeBlocks <= 0.0f
			|| Payload.CenterlineWanderScale <= 0.0f)
		{
			return MakeIslandZero(Editor);
		}

		FNodeLink Source = Editor->SeedOffset(Editor->OpenSimplex2(), Payload.OrganicSeedOffset + 17);
		const FVector AxisScale = Payload.Axis == EVCutFoundationAxis::X
			? FVector(Payload.CenterlineWanderScale, 0.0, 0.0)
			: FVector(0.0, Payload.CenterlineWanderScale, 0.0);
		FNodeLink AxisSource = Editor->DomainAxisScale(Source, AxisScale);
		FNodeLink Gain = Editor->Constant(0.5f);
		FNodeLink WeightedStrength = Editor->Constant(0.25f);
		FNodeLink Wander = Editor->FractalFBm(AxisSource, Gain, WeightedStrength, 3, 2.0f);
		return Editor->MultiplyFloat(Wander, ConvertBlockXDistanceToNoise(Payload.CenterlineWanderAmplitudeBlocks, ScaleContext));
	}

	FNodeLink BuildVCutDomainForPayload(
		UFastNoiseEditor* Editor,
		const FVCutFoundationPayload& Payload,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		if (Editor == nullptr
			|| Payload.HalfLengthBlocks <= 0.0f
			|| Payload.SurfaceHalfWidthBlocks <= 0.0f
			|| Payload.BottomHalfWidthBlocks <= 0.0f
			|| Payload.DepthBlocks <= 0.0f)
		{
			return MakeIslandZero(Editor);
		}

		const FVector CenterNoise = ScaleContext.AuthoredBlockPositionToNoise(Payload.Center);
		const float HalfLengthNoise = FMath::Max(UE_KINDA_SMALL_NUMBER, ConvertBlockXDistanceToNoise(Payload.HalfLengthBlocks, ScaleContext));
		const float SurfaceHalfWidthNoise = FMath::Max(UE_KINDA_SMALL_NUMBER, ConvertBlockXDistanceToNoise(Payload.SurfaceHalfWidthBlocks, ScaleContext));
		const float BottomHalfWidthNoise = FMath::Max(UE_KINDA_SMALL_NUMBER, ConvertBlockXDistanceToNoise(Payload.BottomHalfWidthBlocks, ScaleContext));
		const float DepthNoise = FMath::Max(UE_KINDA_SMALL_NUMBER, ConvertBlockZDistanceToNoise(Payload.DepthBlocks, ScaleContext));
		const float TopZ = static_cast<float>(CenterNoise.Z);
		const float BottomZ = TopZ - DepthNoise;

		FNodeLink PositionX = Editor->PositionOutput(FVector(1.0, 0.0, 0.0), FVector::ZeroVector);
		FNodeLink PositionY = Editor->PositionOutput(FVector(0.0, 1.0, 0.0), FVector::ZeroVector);
		FNodeLink PositionZ = Editor->PositionOutput(FVector(0.0, 0.0, 1.0), FVector::ZeroVector);
		FNodeLink LocalX = Editor->Subtract(PositionX, Editor->Constant(static_cast<float>(CenterNoise.X)));
		FNodeLink LocalY = Editor->Subtract(PositionY, Editor->Constant(static_cast<float>(CenterNoise.Y)));
		if (Payload.bEnableOrganicVariation)
		{
			FNodeLink CenterlineWander = BuildVCutCenterlineWanderOffset(Editor, Payload, ScaleContext);
			if (IsIslandNodeValid(CenterlineWander))
			{
				if (Payload.Axis == EVCutFoundationAxis::X)
				{
					LocalY = Editor->Subtract(LocalY, CenterlineWander);
				}
				else
				{
					LocalX = Editor->Subtract(LocalX, CenterlineWander);
				}
			}
		}
		FNodeLink AlongDistance = BuildAbsoluteNode(Editor, Payload.Axis == EVCutFoundationAxis::X ? LocalX : LocalY);
		FNodeLink AcrossDistance = BuildAbsoluteNode(Editor, Payload.Axis == EVCutFoundationAxis::X ? LocalY : LocalX);

		FNodeLink HeightFromBottom = Editor->Subtract(PositionZ, Editor->Constant(BottomZ));
		FNodeLink HeightForWidth = HeightFromBottom;
		if (Payload.bEnableOrganicVariation && Payload.bEnableSteps && Payload.StepHeightBlocks > 0.0f)
		{
			const float StepNoise = ConvertBlockZDistanceToNoise(Payload.StepHeightBlocks, ScaleContext);
			if (StepNoise > UE_KINDA_SMALL_NUMBER)
			{
				HeightForWidth = Editor->Terrace(HeightForWidth, 1.0f / StepNoise, FMath::Max(0.0f, Payload.StepSmoothness));
			}
		}
		FNodeLink NormalizedHeight = Editor->DivideFloat(HeightForWidth, DepthNoise);
		FNodeLink DynamicHalfWidth = Editor->Add(
			Editor->Constant(BottomHalfWidthNoise),
			Editor->MultiplyFloat(NormalizedHeight, SurfaceHalfWidthNoise - BottomHalfWidthNoise));
		if (Payload.bEnableOrganicVariation)
		{
			FNodeLink EdgeOffset = BuildVCutOrganicEdgeOffset(Editor, Payload, ScaleContext);
			if (IsIslandNodeValid(EdgeOffset))
			{
				DynamicHalfWidth = Editor->Add(DynamicHalfWidth, EdgeOffset);
				DynamicHalfWidth = Editor->MaxFloat(DynamicHalfWidth, FMath::Min(BottomHalfWidthNoise, SurfaceHalfWidthNoise) * 0.25f);
			}
		}

		FNodeLink WidthSide = Editor->Subtract(DynamicHalfWidth, AcrossDistance);
		FNodeLink LengthSide = Editor->Subtract(Editor->Constant(HalfLengthNoise), AlongDistance);
		FNodeLink TopCap = Editor->Subtract(Editor->Constant(TopZ), PositionZ);
		FNodeLink BottomCap = Editor->Subtract(PositionZ, Editor->Constant(BottomZ));
		FNodeLink Domain = Editor->Min(Editor->Min(WidthSide, LengthSide), Editor->Min(TopCap, BottomCap));
		if (Payload.EdgeSoftnessBlocks > UE_KINDA_SMALL_NUMBER)
		{
			const float EdgeSoftness = FMath::Max(UE_KINDA_SMALL_NUMBER, ConvertBlockXDistanceToNoise(Payload.EdgeSoftnessBlocks, ScaleContext));
			Domain = Editor->DivideFloat(Domain, EdgeSoftness);
		}
		return Domain;
	}

	FNodeLink BuildIslandEnvelopeDomainInternal(
		UFastNoiseEditor* Editor,
		const UBiomeStrategyData* Strategy,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const float ExtraTopPaddingBlocks)
	{
		const FIslandFoundationShapePayload* IslandPayload = GetIslandFoundationPayload(Strategy);
		return IslandPayload != nullptr
			? BuildIslandEnvelopeDomainForPayload(Editor, *IslandPayload, ScaleContext, ExtraTopPaddingBlocks)
			: MakeIslandZero(Editor);
	}

	float GetMaxPositiveFoundationTerrainProfileHeightBlocks(const FFoundationProviderDefinition& Provider)
	{
		return GetFoundationTerrainProfileMaxPositiveHeightBlocks(Provider.TerrainProfile);
	}

	FNodeLink BuildFoundationTerrainProfileSurfaceOffset(
		UFastNoiseEditor* Editor,
		const FFoundationProviderDefinition& Provider,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		FFoundationTerrainProfileBuildContext ProfileContext;
		ProfileContext.Editor = Editor;
		ProfileContext.Provider = &Provider;
		ProfileContext.ScaleContext = &ScaleContext;
		ProfileContext.ProviderOriginXYBlocks = GetProviderTerrainProfileOriginXY(Provider);
		return BuildFoundationTerrainProfileSurfaceOffsetNode(Provider.TerrainProfile, ProfileContext);
	}

	FNodeLink BuildIslandPlanarSimplexHeightNoise(
		UFastNoiseEditor* Editor,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const float AmplitudeBlocks,
		const float DomainScale,
		const int32 SeedOffset)
	{
		if (Editor == nullptr || AmplitudeBlocks <= 0.0f || DomainScale <= 0.0f)
		{
			return MakeIslandZero(Editor);
		}

		FNodeLink Source = Editor->OpenSimplex2();
		FNodeLink SeededSource = Editor->SeedOffset(Source, SeedOffset);
		FNodeLink PlanarSource = Editor->DomainAxisScale(SeededSource, FVector(DomainScale, DomainScale, 0.0));
		FNodeLink Gain = Editor->Constant(0.5f);
		FNodeLink WeightedStrength = Editor->Constant(0.25f);
		FNodeLink Fractal = Editor->FractalFBm(PlanarSource, Gain, WeightedStrength, 4, 2.0f);
		return Editor->MultiplyFloat(Fractal, ConvertBlockZDistanceToNoise(AmplitudeBlocks, ScaleContext));
	}

	FFloatingIslandBodyNoiseSettings ResolveIslandBlockBodyForTerrainProfile(const FFoundationProviderDefinition& Provider, const FIslandFoundationShapePayload& IslandPayload)
	{
		FFloatingIslandBodyNoiseSettings Body;
		Body.Center = IslandPayload.IslandBody.Center;
		Body.TopRadius = IslandPayload.IslandBody.TopRadius;
		Body.TopHeight = IslandPayload.IslandBody.TopHeight;
		Body.BottomDepth = IslandPayload.IslandBody.BottomDepth;
		Body.RimThickness = IslandPayload.IslandBody.RimThickness;
		Body.SideBulge = IslandPayload.IslandBody.SideBulgeBlocks;
		Body.LowerConeSharpness = IslandPayload.IslandBody.LowerConeSharpness;
		Body.BottomPointDepth = IslandPayload.IslandBody.BottomPointDepthBlocks;
		Body.RimNoiseAmplitude = IslandPayload.BodyDetail.bEnableRimNoise ? IslandPayload.BodyDetail.RimNoiseAmplitude : 0.0f;
		Body.RimNoiseScale = IslandPayload.BodyDetail.RimNoiseScale;
		Body.SurfaceNoiseAmplitude = 0.0f;
		Body.UndersideNoiseAmplitude = IslandPayload.BodyDetail.bEnableUndersideNoise ? IslandPayload.BodyDetail.UndersideNoiseAmplitude : 0.0f;
		Body.UndersideNoiseScale = IslandPayload.BodyDetail.UndersideNoiseScale;
		Body.BottomSpikeNoiseAmplitude = IslandPayload.BodyDetail.bEnableBottomSpikeNoise ? IslandPayload.BodyDetail.BottomSpikeNoiseAmplitude : 0.0f;
		Body.BottomSpikeNoiseScale = IslandPayload.BodyDetail.BottomSpikeNoiseScale;
		Body.SeedOffset = IslandPayload.BodyDetail.SeedOffset;

		return Body;
	}

	FNodeLink ApplySurfaceTerraces(
		UFastNoiseEditor* Editor,
		const FResolvedWorldGenScaleContext& ScaleContext,
		FNodeLink SurfaceVariation,
		const FResolvedSurfaceAnchorTerrain& TerrainSettings)
	{
		if (Editor == nullptr || !IsIslandNodeValid(SurfaceVariation) || !TerrainSettings.bUseTerraces || TerrainSettings.TerraceStepHeight <= 0.0f)
		{
			return SurfaceVariation;
		}

		const float StepNoise = ConvertBlockZDistanceToNoise(TerrainSettings.TerraceStepHeight, ScaleContext);
		if (StepNoise <= UE_KINDA_SMALL_NUMBER)
		{
			return SurfaceVariation;
		}

		return Editor->Terrace(SurfaceVariation, 1.0f / StepNoise, FMath::Max(0.0f, TerrainSettings.TerraceSmoothness));
	}

	FNodeLink BuildIslandPositiveInsideBody(
		UFastNoiseEditor* Editor,
		const FIslandFoundationShapePayload& IslandPayload,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		const FFloatingIslandBodyNoiseSettings NoiseBody = ConvertIslandBodyToNoiseSettings(IslandPayload.IslandBody, ScaleContext);
		return UFoundationFastNoiseLibrary::BuildFloatingIslandBody(Editor, NoiseBody);
	}

	FNodeLink BuildIslandPositiveInsideBody(
		UFastNoiseEditor* Editor,
		const FFoundationProviderDefinition& Provider,
		const FIslandFoundationShapePayload& IslandPayload,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		const FFloatingIslandBodyNoiseSettings ProfiledBody = ResolveIslandBlockBodyForTerrainProfile(Provider, IslandPayload);
		const FFloatingIslandBodyNoiseSettings NoiseBody = ConvertIslandBodyToNoiseSettings(ProfiledBody, ScaleContext);
		const FNodeLink SurfaceOffset = BuildFoundationTerrainProfileSurfaceOffset(Editor, Provider, ScaleContext);
		return UFoundationFastNoiseLibrary::BuildFloatingIslandBodyWithSurfaceOffset(Editor, NoiseBody, SurfaceOffset);
	}

	bool FindFoundationSurfaceZNoiseAtAuthoredXY(
		const FNodeLink PositiveInsideBody,
		const FIslandFoundationShapePayload& IslandPayload,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const FVector2D AuthoredXY,
		const double SurfaceSearchAmplitudeBlocks,
		float& OutSurfaceZNoise)
	{
		if (!IsIslandNodeValid(PositiveInsideBody) || !PositiveInsideBody.Node->BaseNode)
		{
			return false;
		}

		const double SearchPaddingBlocks = 8.0;
		const double SurfaceAmplitude = FMath::Abs(SurfaceSearchAmplitudeBlocks);
		const double AuthoredTopZ = IslandPayload.IslandBody.Center.Z;
		const double AuthoredBodyCenterZ = AuthoredTopZ - IslandPayload.IslandBody.TopHeight;
		const double AuthoredMinZ = FMath::Max(
			ScaleContext.AuthoredMinBlock.Z,
			AuthoredBodyCenterZ - IslandPayload.IslandBody.BottomDepth - SurfaceAmplitude - SearchPaddingBlocks);
		const double AuthoredMaxZ = FMath::Min(
			ScaleContext.AuthoredMaxBlock.Z,
			AuthoredTopZ + SurfaceAmplitude + SearchPaddingBlocks);

		if (AuthoredMaxZ < AuthoredMinZ)
		{
			return false;
		}

		for (double AuthoredZ = AuthoredMaxZ; AuthoredZ >= AuthoredMinZ; AuthoredZ -= 1.0)
		{
			const FVector NoisePosition = ScaleContext.AuthoredBlockPositionToNoise(FVector(AuthoredXY.X, AuthoredXY.Y, AuthoredZ));
			const float BodyValue = PositiveInsideBody.Node->BaseNode->GenSingle3D(
				static_cast<float>(NoisePosition.X),
				static_cast<float>(NoisePosition.Y),
				static_cast<float>(NoisePosition.Z),
				0);
			if (BodyValue >= 0.0f)
			{
				OutSurfaceZNoise = static_cast<float>(NoisePosition.Z);
				return true;
			}
		}

		return false;
	}

	TArray<FVector2D> BuildProviderSurfaceSamples(const FFoundationSurfaceQuery& Query, TArray<FVector2D>& OutCornerSamples)
	{
		TArray<FVector2D> Samples;
		Samples.Reserve(9);
		OutCornerSamples.Reset();
		OutCornerSamples.Reserve(4);

		const FVector2D Footprint(
			FMath::Max(0.0, Query.FootprintHalfExtent.X),
			FMath::Max(0.0, Query.FootprintHalfExtent.Y));
		for (int32 YIndex = -1; YIndex <= 1; ++YIndex)
		{
			for (int32 XIndex = -1; XIndex <= 1; ++XIndex)
			{
				const FVector2D Offset(Footprint.X * XIndex, Footprint.Y * YIndex);
				if (Query.bEllipticalFootprint && !Offset.IsNearlyZero())
				{
					const double Normalized = FMath::Square(Offset.X / FMath::Max(Footprint.X, UE_KINDA_SMALL_NUMBER))
						+ FMath::Square(Offset.Y / FMath::Max(Footprint.Y, UE_KINDA_SMALL_NUMBER));
					if (Normalized > 1.0)
					{
						continue;
					}
				}

				const FVector2D Sample = Query.CenterXY + Offset;
				Samples.Add(Sample);
				if (XIndex != 0 && YIndex != 0)
				{
					OutCornerSamples.Add(Sample);
				}
			}
		}

		return Samples;
	}

	bool QueryIslandFoundationSurfaceInternal(
		UFastNoiseEditor* Editor,
		const UBiomeStrategyData* Strategy,
		const FIslandFoundationShapePayload& IslandPayload,
		const FFoundationSurfaceQuery& Query,
		const FResolvedWorldGenScaleContext& ScaleContext,
		FResolvedFoundationSurface& OutSurface)
	{
		OutSurface = FResolvedFoundationSurface();
		const FNodeLink PositiveInsideBody = BuildIslandPositiveInsideBody(Editor, IslandPayload, ScaleContext);
		if (!IsIslandNodeValid(PositiveInsideBody) || !PositiveInsideBody.Node->BaseNode)
		{
			return false;
		}

		TArray<FVector2D> CornerSamples;
		const TArray<FVector2D> Samples = BuildProviderSurfaceSamples(Query, CornerSamples);
		double SurfaceZSum = 0.0;
		double MaxSurfaceZ = -TNumericLimits<double>::Max();
		double CornerMaxSurfaceZ = -TNumericLimits<double>::Max();
		float CenterSurfaceZ = 0.0f;
		bool bHasCenterSurface = false;
		int32 SurfaceCount = 0;
		for (const FVector2D& Sample : Samples)
		{
			float SampleSurfaceZ = 0.0f;
			if (FindFoundationSurfaceZNoiseAtAuthoredXY(PositiveInsideBody, IslandPayload, ScaleContext, Sample, 0.0, SampleSurfaceZ))
			{
				SurfaceZSum += SampleSurfaceZ;
				MaxSurfaceZ = FMath::Max(MaxSurfaceZ, static_cast<double>(SampleSurfaceZ));
				if ((Sample - Query.CenterXY).IsNearlyZero())
				{
					CenterSurfaceZ = SampleSurfaceZ;
					bHasCenterSurface = true;
				}
				if (CornerSamples.Contains(Sample))
				{
					CornerMaxSurfaceZ = FMath::Max(CornerMaxSurfaceZ, static_cast<double>(SampleSurfaceZ));
				}
				++SurfaceCount;
			}
		}

		if (SurfaceCount <= 0)
		{
			return false;
		}

		double SurfaceZ = SurfaceZSum / SurfaceCount;
		switch (Query.SampleMode)
		{
		case EFoundationSurfaceSampleMode::CenterSample:
			SurfaceZ = bHasCenterSurface ? CenterSurfaceZ : SurfaceZ;
			break;
		case EFoundationSurfaceSampleMode::MaxSamples:
			SurfaceZ = MaxSurfaceZ;
			break;
		case EFoundationSurfaceSampleMode::CornerMaxSamples:
			SurfaceZ = CornerMaxSurfaceZ > -TNumericLimits<double>::Max()
				? CornerMaxSurfaceZ
				: MaxSurfaceZ;
			break;
		case EFoundationSurfaceSampleMode::AverageSamples:
		default:
			break;
		}

		OutSurface.bValid = true;
		OutSurface.DebugPath = FString::Printf(TEXT("%s.Foundation[Island].Surface"), Strategy != nullptr ? *Strategy->GetPathName() : TEXT("Island"));
		OutSurface.SurfaceZNoise = static_cast<float>(SurfaceZ);
		OutSurface.SurfaceZBlock = ScaleContext.NoiseZToAuthoredBlockZ(OutSurface.SurfaceZNoise);
		OutSurface.SampleCount = SurfaceCount;
		return true;
	}

	bool QueryIslandProviderSurfaceInternal(
		UFastNoiseEditor* Editor,
		const FFoundationProviderDefinition& Provider,
		const FIslandFoundationShapePayload& IslandPayload,
		const FFoundationSurfaceQuery& Query,
		const FResolvedWorldGenScaleContext& ScaleContext,
		FResolvedFoundationSurface& OutSurface)
	{
		OutSurface = FResolvedFoundationSurface();
		const FNodeLink PositiveInsideBody = BuildIslandPositiveInsideBody(Editor, Provider, IslandPayload, ScaleContext);
		if (!IsIslandNodeValid(PositiveInsideBody) || !PositiveInsideBody.Node->BaseNode)
		{
			return false;
		}

		double SurfaceSearchAmplitudeBlocks = static_cast<double>(
			GetFoundationTerrainProfileSurfaceSearchAmplitudeBlocks(Provider.TerrainProfile));

		TArray<FVector2D> CornerSamples;
		const TArray<FVector2D> Samples = BuildProviderSurfaceSamples(Query, CornerSamples);
		double SurfaceZSum = 0.0;
		double MaxSurfaceZ = -TNumericLimits<double>::Max();
		double CornerMaxSurfaceZ = -TNumericLimits<double>::Max();
		float CenterSurfaceZ = 0.0f;
		bool bHasCenterSurface = false;
		int32 SurfaceCount = 0;
		for (const FVector2D& Sample : Samples)
		{
			float SampleSurfaceZ = 0.0f;
			if (FindFoundationSurfaceZNoiseAtAuthoredXY(PositiveInsideBody, IslandPayload, ScaleContext, Sample, SurfaceSearchAmplitudeBlocks, SampleSurfaceZ))
			{
				SurfaceZSum += SampleSurfaceZ;
				MaxSurfaceZ = FMath::Max(MaxSurfaceZ, static_cast<double>(SampleSurfaceZ));
				if ((Sample - Query.CenterXY).IsNearlyZero())
				{
					CenterSurfaceZ = SampleSurfaceZ;
					bHasCenterSurface = true;
				}
				if (CornerSamples.Contains(Sample))
				{
					CornerMaxSurfaceZ = FMath::Max(CornerMaxSurfaceZ, static_cast<double>(SampleSurfaceZ));
				}
				++SurfaceCount;
			}
		}

		if (SurfaceCount <= 0)
		{
			return false;
		}

		double SurfaceZ = SurfaceZSum / SurfaceCount;
		switch (Query.SampleMode)
		{
		case EFoundationSurfaceSampleMode::CenterSample:
			SurfaceZ = bHasCenterSurface ? CenterSurfaceZ : SurfaceZ;
			break;
		case EFoundationSurfaceSampleMode::MaxSamples:
			SurfaceZ = MaxSurfaceZ;
			break;
		case EFoundationSurfaceSampleMode::CornerMaxSamples:
			SurfaceZ = CornerMaxSurfaceZ > -TNumericLimits<double>::Max()
				? CornerMaxSurfaceZ
				: MaxSurfaceZ;
			break;
		case EFoundationSurfaceSampleMode::AverageSamples:
		default:
			break;
		}

		OutSurface.bValid = true;
		OutSurface.DebugPath = TEXT("Island.Foundation.Surface");
		OutSurface.SurfaceZNoise = static_cast<float>(SurfaceZ);
		OutSurface.SurfaceZBlock = ScaleContext.NoiseZToAuthoredBlockZ(OutSurface.SurfaceZNoise);
		OutSurface.SampleCount = SurfaceCount;
		return true;
	}

	float GetAnalyticFoundationSurfaceZNoise(
		const FIslandFoundationShapePayload& IslandPayload,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		const FVector AuthoredSurfacePosition(
			ReservationPayload.CenterXY.X,
			ReservationPayload.CenterXY.Y,
			IslandPayload.IslandBody.Center.Z);
		return static_cast<float>(ScaleContext.AuthoredBlockPositionToNoise(AuthoredSurfacePosition).Z);
	}

	struct FResolvedReservationSurfaceZNoise
	{
		float FoundationSurfaceZ = 0.0f;
		float SupportedSurfaceZ = 0.0f;
		float AnchorSurfaceZ = 0.0f;
	};

	FResolvedReservationSurfaceZNoise SolveReservationSurfaceZNoise(
		UFastNoiseEditor* Editor,
		const FIslandFoundationShapePayload& IslandPayload,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		const FVector Footprint = ReservationPayload.Shape == ESurfaceAnchorReservationShape::Box
			? GetAnchorBoxHalfExtent(ReservationPayload)
			: GetAnchorSphereRadii(ReservationPayload);
		FFoundationSurfaceQuery SurfaceQuery;
		SurfaceQuery.CenterXY = ReservationPayload.CenterXY;
		SurfaceQuery.FootprintHalfExtent = FVector2D(Footprint.X, Footprint.Y);
		SurfaceQuery.bEllipticalFootprint = ReservationPayload.Shape == ESurfaceAnchorReservationShape::Sphere;
		switch (ReservationPayload.SurfaceHeightSolveMode)
		{
		case ESurfaceAnchorHeightSolveMode::CenterSample:
			SurfaceQuery.SampleMode = EFoundationSurfaceSampleMode::CenterSample;
			break;
		case ESurfaceAnchorHeightSolveMode::AverageSamples:
			SurfaceQuery.SampleMode = EFoundationSurfaceSampleMode::AverageSamples;
			break;
		case ESurfaceAnchorHeightSolveMode::CornerMaxSamples:
			SurfaceQuery.SampleMode = EFoundationSurfaceSampleMode::CornerMaxSamples;
			break;
		case ESurfaceAnchorHeightSolveMode::MaxSamples:
		default:
			SurfaceQuery.SampleMode = EFoundationSurfaceSampleMode::MaxSamples;
			break;
		}

		FResolvedFoundationSurface FoundationSurface;
		if (!QueryIslandFoundationSurfaceInternal(Editor, nullptr, IslandPayload, SurfaceQuery, ScaleContext, FoundationSurface))
		{
			const float FoundationSurfaceZ = GetAnalyticFoundationSurfaceZNoise(IslandPayload, ReservationPayload, ScaleContext);
			const float LiftNoise = ConvertBlockZOffsetToNoise(GetSurfaceZLiftBlocks(ReservationPayload), ScaleContext);
			const float OffsetNoise = ConvertBlockZOffsetToNoise(ReservationPayload.SurfaceZOffset, ScaleContext);
			const float SupportedSurfaceZ = FoundationSurfaceZ + LiftNoise;
			const float AnchorSurfaceZ = SupportedSurfaceZ + OffsetNoise;
			return FResolvedReservationSurfaceZNoise{ FoundationSurfaceZ, SupportedSurfaceZ, AnchorSurfaceZ };
		}

		const float FoundationSurfaceZ = FoundationSurface.SurfaceZNoise;
		const float LiftNoise = ConvertBlockZOffsetToNoise(GetSurfaceZLiftBlocks(ReservationPayload), ScaleContext);
		const float OffsetNoise = ConvertBlockZOffsetToNoise(ReservationPayload.SurfaceZOffset, ScaleContext);
		const float SupportedSurfaceZ = FoundationSurfaceZ + LiftNoise;
		const float AnchorSurfaceZ = SupportedSurfaceZ + OffsetNoise;
		return FResolvedReservationSurfaceZNoise{ FoundationSurfaceZ, SupportedSurfaceZ, AnchorSurfaceZ };
	}

	FResolvedReservationSurfaceZNoise SolveIslandProviderReservationSurfaceZNoise(
		UFastNoiseEditor* Editor,
		const FFoundationProviderDefinition& Provider,
		const FIslandFoundationShapePayload& IslandPayload,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		const FVector Footprint = ReservationPayload.Shape == ESurfaceAnchorReservationShape::Box
			? GetAnchorBoxHalfExtent(ReservationPayload)
			: GetAnchorSphereRadii(ReservationPayload);
		FFoundationSurfaceQuery SurfaceQuery;
		SurfaceQuery.CenterXY = ReservationPayload.CenterXY;
		SurfaceQuery.FootprintHalfExtent = FVector2D(Footprint.X, Footprint.Y);
		SurfaceQuery.bEllipticalFootprint = ReservationPayload.Shape == ESurfaceAnchorReservationShape::Sphere;
		switch (ReservationPayload.SurfaceHeightSolveMode)
		{
		case ESurfaceAnchorHeightSolveMode::CenterSample:
			SurfaceQuery.SampleMode = EFoundationSurfaceSampleMode::CenterSample;
			break;
		case ESurfaceAnchorHeightSolveMode::AverageSamples:
			SurfaceQuery.SampleMode = EFoundationSurfaceSampleMode::AverageSamples;
			break;
		case ESurfaceAnchorHeightSolveMode::CornerMaxSamples:
			SurfaceQuery.SampleMode = EFoundationSurfaceSampleMode::CornerMaxSamples;
			break;
		case ESurfaceAnchorHeightSolveMode::MaxSamples:
		default:
			SurfaceQuery.SampleMode = EFoundationSurfaceSampleMode::MaxSamples;
			break;
		}

		FResolvedFoundationSurface FoundationSurface;
		if (!QueryIslandProviderSurfaceInternal(Editor, Provider, IslandPayload, SurfaceQuery, ScaleContext, FoundationSurface))
		{
			const float FoundationSurfaceZ = GetAnalyticFoundationSurfaceZNoise(IslandPayload, ReservationPayload, ScaleContext);
			const float LiftNoise = ConvertBlockZOffsetToNoise(GetSurfaceZLiftBlocks(ReservationPayload), ScaleContext);
			const float OffsetNoise = ConvertBlockZOffsetToNoise(ReservationPayload.SurfaceZOffset, ScaleContext);
			const float SupportedSurfaceZ = FoundationSurfaceZ + LiftNoise;
			const float AnchorSurfaceZ = SupportedSurfaceZ + OffsetNoise;
			return FResolvedReservationSurfaceZNoise{ FoundationSurfaceZ, SupportedSurfaceZ, AnchorSurfaceZ };
		}

		const float FoundationSurfaceZ = FoundationSurface.SurfaceZNoise;
		const float LiftNoise = ConvertBlockZOffsetToNoise(GetSurfaceZLiftBlocks(ReservationPayload), ScaleContext);
		const float OffsetNoise = ConvertBlockZOffsetToNoise(ReservationPayload.SurfaceZOffset, ScaleContext);
		const float SupportedSurfaceZ = FoundationSurfaceZ + LiftNoise;
		const float AnchorSurfaceZ = SupportedSurfaceZ + OffsetNoise;
		return FResolvedReservationSurfaceZNoise{ FoundationSurfaceZ, SupportedSurfaceZ, AnchorSurfaceZ };
	}

	FResolvedReservationSurfaceZNoise SolveInfinitePlaneReservationSurfaceZNoise(
		const FInfinitePlaneFoundationPayload& PlanePayload,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		const float FoundationSurfaceZ = static_cast<float>(ScaleContext.AuthoredBlockPositionToNoise(FVector(ReservationPayload.CenterXY.X, ReservationPayload.CenterXY.Y, PlanePayload.SurfaceZ)).Z);
		const float LiftNoise = ConvertBlockZOffsetToNoise(GetSurfaceZLiftBlocks(ReservationPayload), ScaleContext);
		const float OffsetNoise = ConvertBlockZOffsetToNoise(ReservationPayload.SurfaceZOffset, ScaleContext);
		const float SupportedSurfaceZ = FoundationSurfaceZ + LiftNoise;
		const float AnchorSurfaceZ = SupportedSurfaceZ + OffsetNoise;
		return FResolvedReservationSurfaceZNoise{ FoundationSurfaceZ, SupportedSurfaceZ, AnchorSurfaceZ };
	}

	FNodeLink BuildIslandReservationClipDomain(
		UFastNoiseEditor* Editor,
		const FIslandFoundationShapePayload& IslandPayload,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		FFloatingIslandBodyNoiseSettings Body = ConvertIslandBodyToNoiseSettings(IslandPayload.IslandBody, ScaleContext);
		if (Editor == nullptr
			|| Body.TopRadius <= 0.0f
			|| Body.BottomDepth < 0.0f
			|| Body.RimThickness <= 0.0f)
		{
			return MakeIslandZero(Editor);
		}

		const FVector2D CenterXY(Body.Center.X, Body.Center.Y);
		const float TopZ = Body.Center.Z + Body.TopHeight + ConvertBlockZDistanceToNoise(IslandPayload.DomainVerticalPadding, ScaleContext);
		const float BottomZ = Body.Center.Z - Body.BottomDepth - ConvertBlockZDistanceToNoise(GetIslandDomainBottomPaddingBlocks(IslandPayload), ScaleContext);
		const float Radius = Body.TopRadius + ConvertBlockXDistanceToNoise(GetIslandDomainRadiusPaddingBlocks(IslandPayload), ScaleContext);

		FNodeLink PositionZ = Editor->PositionOutput(FVector(0.0, 0.0, 1.0), FVector::ZeroVector);
		FNodeLink PlanarDistanceSquared = BuildPlanarDistanceSquaredXY(Editor, CenterXY);
		FNodeLink RadiusSquared = Editor->Constant(Radius * Radius);
		FNodeLink RadialSide = Editor->Subtract(RadiusSquared, PlanarDistanceSquared);
		FNodeLink NormalizedDistance = Editor->DivideFloat(PlanarDistanceSquared, Radius * Radius);
		FNodeLink UndersideHeight = Editor->MultiplyFloat(NormalizedDistance, TopZ - Body.RimThickness - BottomZ);
		FNodeLink LowerSurface = Editor->Add(Editor->Constant(BottomZ), UndersideHeight);
		FNodeLink LowerCurve = Editor->Subtract(PositionZ, LowerSurface);
		return Editor->Min(RadialSide, LowerCurve);
	}

	FNodeLink BuildPositionDelta(UFastNoiseEditor* Editor, const FVector& Axis, const float Offset)
	{
		FNodeLink Position = Editor->PositionOutput(Axis, FVector::ZeroVector);
		return Editor->Subtract(Position, Editor->Constant(Offset));
	}

	FNodeLink BuildSurfaceLocalSphereReservationMask(
		UFastNoiseEditor* Editor,
		const FIslandFoundationShapePayload& IslandPayload,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const FVector& Radii,
		const bool bUseReservationSurfaceClearance)
	{
		const FResolvedReservationSurfaceZNoise SurfaceZ = SolveReservationSurfaceZNoise(Editor, IslandPayload, ReservationPayload, ScaleContext);
		const FVector2D CenterXY = ConvertAuthoredCenterXYToNoise(ReservationPayload.CenterXY, ScaleContext);
		const FVector InclusiveRadii = ExpandSphereRadiiForVoxelCenterCoverage(Radii);
		const FVector NoiseRadii = ConvertBlockDistanceToNoiseAbs(InclusiveRadii, ScaleContext);
		FNodeLink NormalizedX = Editor->DivideFloat(
			BuildPositionDelta(Editor, FVector(1.0, 0.0, 0.0), CenterXY.X),
			FMath::Max(static_cast<float>(NoiseRadii.X), UE_KINDA_SMALL_NUMBER));
		FNodeLink NormalizedY = Editor->DivideFloat(
			BuildPositionDelta(Editor, FVector(0.0, 1.0, 0.0), CenterXY.Y),
			FMath::Max(static_cast<float>(NoiseRadii.Y), UE_KINDA_SMALL_NUMBER));
		FNodeLink X2 = Editor->PowInt(NormalizedX, 2);
		FNodeLink Y2 = Editor->PowInt(NormalizedY, 2);
		FNodeLink PlanarDistanceSquared = Editor->Add(X2, Y2);
		FNodeLink PlanarMask = Editor->Subtract(Editor->Constant(1.0f), PlanarDistanceSquared);
		const float AverageRadiusNoise = FMath::Max(
			UE_KINDA_SMALL_NUMBER,
			static_cast<float>((FMath::Abs(NoiseRadii.X) + FMath::Abs(NoiseRadii.Y)) * 0.5));
		const FVector EdgeBlendXYNoise = ConvertBlockDistanceToNoiseAbs(
			FVector(SurfaceAnchorFixedBoundaryBlendBlocks, SurfaceAnchorFixedBoundaryBlendBlocks, 0.0),
			ScaleContext);
		const float EdgeBlendNoise = FMath::Min(
			AverageRadiusNoise,
			static_cast<float>((EdgeBlendXYNoise.X + EdgeBlendXYNoise.Y) * 0.5));
		const float PlanarEdgeScale = FMath::Max(UE_KINDA_SMALL_NUMBER, SurfaceAnchorSpherePlanarEdgeScaleFactor * EdgeBlendNoise / AverageRadiusNoise);
		PlanarMask = Editor->MinFloat(Editor->AddFloat(Editor->DivideFloat(PlanarMask, PlanarEdgeScale), SurfaceAnchorSpherePlanarEdgeBias), 1.0f);

		const float DomainBottomZ = SurfaceZ.AnchorSurfaceZ - ConvertBlockZDistanceToNoise(InclusiveRadii.Z, ScaleContext);
		const float StructureClearanceBlocks = FMath::Max(Radii.X, Radii.Y) + 0.5f;
		const float DomainTopZ = bUseReservationSurfaceClearance
			? FMath::Max(
				SurfaceZ.AnchorSurfaceZ + ConvertBlockZDistanceToNoise(StructureClearanceBlocks, ScaleContext),
				SurfaceZ.FoundationSurfaceZ + ConvertBlockZDistanceToNoise(0.5f, ScaleContext))
			: SurfaceZ.AnchorSurfaceZ + ConvertBlockZDistanceToNoise(InclusiveRadii.Z, ScaleContext);
		FNodeLink PositionZ = Editor->PositionOutput(FVector(0.0, 0.0, 1.0), FVector::ZeroVector);
		FNodeLink BelowTopZ = Editor->Subtract(Editor->Constant(DomainTopZ), PositionZ);
		FNodeLink AboveBottomZ = Editor->Subtract(PositionZ, Editor->Constant(DomainBottomZ));
		const float EdgeBlendZ = FMath::Max(
			UE_KINDA_SMALL_NUMBER,
			FMath::Min(ConvertBlockZDistanceToNoise(SurfaceAnchorFixedBoundaryBlendBlocks, ScaleContext), ConvertBlockZDistanceToNoise(InclusiveRadii.Z, ScaleContext)));
		FNodeLink ZMask = Editor->MinFloat(Editor->DivideFloat(Editor->Min(BelowTopZ, AboveBottomZ), EdgeBlendZ), 1.0f);
		return Editor->Min(PlanarMask, ZMask);
	}

	FNodeLink BuildSurfaceLocalBoxReservationMask(
		UFastNoiseEditor* Editor,
		const FIslandFoundationShapePayload& IslandPayload,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const FVector& HalfExtent,
		const bool bUseReservationSurfaceClearance)
	{
		const FResolvedReservationSurfaceZNoise SurfaceZ = SolveReservationSurfaceZNoise(Editor, IslandPayload, ReservationPayload, ScaleContext);
		const FVector2D CenterXY = ConvertAuthoredCenterXYToNoise(ReservationPayload.CenterXY, ScaleContext);
		const FVector InclusiveHalfExtent = ExpandExtentForVoxelCenterCoverage(HalfExtent);
		const FVector NoiseHalfExtent = ConvertBlockDistanceToNoiseAbs(InclusiveHalfExtent, ScaleContext);
		const float HalfExtentX = FMath::Max(static_cast<float>(NoiseHalfExtent.X), UE_KINDA_SMALL_NUMBER);
		const float HalfExtentY = FMath::Max(static_cast<float>(NoiseHalfExtent.Y), UE_KINDA_SMALL_NUMBER);
		const float DomainBottomZ = SurfaceZ.AnchorSurfaceZ - ConvertBlockZDistanceToNoise(InclusiveHalfExtent.Z, ScaleContext);
		const float DomainTopZ = bUseReservationSurfaceClearance
			? FMath::Max(
				SurfaceZ.AnchorSurfaceZ + ConvertBlockZDistanceToNoise(FMath::Max(InclusiveHalfExtent.X, InclusiveHalfExtent.Y), ScaleContext),
				SurfaceZ.FoundationSurfaceZ + ConvertBlockZDistanceToNoise(0.5f, ScaleContext))
			: SurfaceZ.AnchorSurfaceZ + ConvertBlockZDistanceToNoise(InclusiveHalfExtent.Z, ScaleContext);
		FNodeLink PositionX = Editor->PositionOutput(FVector(1.0, 0.0, 0.0), FVector::ZeroVector);
		FNodeLink PositionY = Editor->PositionOutput(FVector(0.0, 1.0, 0.0), FVector::ZeroVector);
		FNodeLink PositionZ = Editor->PositionOutput(FVector(0.0, 0.0, 1.0), FVector::ZeroVector);
		FNodeLink BelowMaxX = Editor->Subtract(Editor->Constant(CenterXY.X + HalfExtentX), PositionX);
		FNodeLink AboveMinX = Editor->Subtract(PositionX, Editor->Constant(CenterXY.X - HalfExtentX));
		FNodeLink BelowMaxY = Editor->Subtract(Editor->Constant(CenterXY.Y + HalfExtentY), PositionY);
		FNodeLink AboveMinY = Editor->Subtract(PositionY, Editor->Constant(CenterXY.Y - HalfExtentY));
		FNodeLink BelowMaxZ = Editor->Subtract(Editor->Constant(DomainTopZ), PositionZ);
		FNodeLink AboveMinZ = Editor->Subtract(PositionZ, Editor->Constant(DomainBottomZ));
		const float EdgeBlendX = FMath::Max(
			UE_KINDA_SMALL_NUMBER,
			FMath::Min(ConvertBlockDistanceToNoiseAbs(FVector(SurfaceAnchorFixedBoundaryBlendBlocks, 0.0, 0.0), ScaleContext).X, HalfExtentX));
		const float EdgeBlendY = FMath::Max(
			UE_KINDA_SMALL_NUMBER,
			FMath::Min(ConvertBlockDistanceToNoiseAbs(FVector(0.0, SurfaceAnchorFixedBoundaryBlendBlocks, 0.0), ScaleContext).Y, HalfExtentY));
		const float EdgeBlendZ = FMath::Max(
			UE_KINDA_SMALL_NUMBER,
			FMath::Min(ConvertBlockZDistanceToNoise(SurfaceAnchorFixedBoundaryBlendBlocks, ScaleContext), ConvertBlockZDistanceToNoise(InclusiveHalfExtent.Z, ScaleContext)));
		FNodeLink XMask = Editor->MinFloat(Editor->DivideFloat(Editor->Min(BelowMaxX, AboveMinX), EdgeBlendX), 1.0f);
		FNodeLink YMask = Editor->MinFloat(Editor->DivideFloat(Editor->Min(BelowMaxY, AboveMinY), EdgeBlendY), 1.0f);
		FNodeLink ZMask = Editor->MinFloat(Editor->DivideFloat(Editor->Min(BelowMaxZ, AboveMinZ), EdgeBlendZ), 1.0f);
		return Editor->Min(Editor->Min(XMask, YMask), ZMask);
	}

	FNodeLink BuildSurfaceLocalSphereReservationMaskForSolvedSurface(
		UFastNoiseEditor* Editor,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const FVector& Radii,
		const FResolvedReservationSurfaceZNoise& SurfaceZ,
		const bool bUseReservationSurfaceClearance)
	{
		const FVector2D CenterXY = ConvertAuthoredCenterXYToNoise(ReservationPayload.CenterXY, ScaleContext);
		const FVector InclusiveRadii = ExpandSphereRadiiForVoxelCenterCoverage(Radii);
		const FVector NoiseRadii = ConvertBlockDistanceToNoiseAbs(InclusiveRadii, ScaleContext);
		FNodeLink NormalizedX = Editor->DivideFloat(
			BuildPositionDelta(Editor, FVector(1.0, 0.0, 0.0), CenterXY.X),
			FMath::Max(static_cast<float>(NoiseRadii.X), UE_KINDA_SMALL_NUMBER));
		FNodeLink NormalizedY = Editor->DivideFloat(
			BuildPositionDelta(Editor, FVector(0.0, 1.0, 0.0), CenterXY.Y),
			FMath::Max(static_cast<float>(NoiseRadii.Y), UE_KINDA_SMALL_NUMBER));
		FNodeLink PlanarDistanceSquared = Editor->Add(Editor->PowInt(NormalizedX, 2), Editor->PowInt(NormalizedY, 2));
		FNodeLink PlanarMask = Editor->Subtract(Editor->Constant(1.0f), PlanarDistanceSquared);
		const float AverageRadiusNoise = FMath::Max(
			UE_KINDA_SMALL_NUMBER,
			static_cast<float>((FMath::Abs(NoiseRadii.X) + FMath::Abs(NoiseRadii.Y)) * 0.5));
		const FVector EdgeBlendXYNoise = ConvertBlockDistanceToNoiseAbs(
			FVector(SurfaceAnchorFixedBoundaryBlendBlocks, SurfaceAnchorFixedBoundaryBlendBlocks, 0.0),
			ScaleContext);
		const float EdgeBlendNoise = FMath::Min(
			AverageRadiusNoise,
			static_cast<float>((EdgeBlendXYNoise.X + EdgeBlendXYNoise.Y) * 0.5));
		const float PlanarEdgeScale = FMath::Max(UE_KINDA_SMALL_NUMBER, SurfaceAnchorSpherePlanarEdgeScaleFactor * EdgeBlendNoise / AverageRadiusNoise);
		PlanarMask = Editor->MinFloat(Editor->AddFloat(Editor->DivideFloat(PlanarMask, PlanarEdgeScale), SurfaceAnchorSpherePlanarEdgeBias), 1.0f);

		const float DomainBottomZ = SurfaceZ.AnchorSurfaceZ - ConvertBlockZDistanceToNoise(InclusiveRadii.Z, ScaleContext);
		const float StructureClearanceBlocks = FMath::Max(Radii.X, Radii.Y) + 0.5f;
		const float DomainTopZ = bUseReservationSurfaceClearance
			? FMath::Max(
				SurfaceZ.AnchorSurfaceZ + ConvertBlockZDistanceToNoise(StructureClearanceBlocks, ScaleContext),
				SurfaceZ.FoundationSurfaceZ + ConvertBlockZDistanceToNoise(0.5f, ScaleContext))
			: SurfaceZ.AnchorSurfaceZ + ConvertBlockZDistanceToNoise(InclusiveRadii.Z, ScaleContext);
		FNodeLink PositionZ = Editor->PositionOutput(FVector(0.0, 0.0, 1.0), FVector::ZeroVector);
		FNodeLink BelowTopZ = Editor->Subtract(Editor->Constant(DomainTopZ), PositionZ);
		FNodeLink AboveBottomZ = Editor->Subtract(PositionZ, Editor->Constant(DomainBottomZ));
		const float EdgeBlendZ = FMath::Max(
			UE_KINDA_SMALL_NUMBER,
			FMath::Min(ConvertBlockZDistanceToNoise(SurfaceAnchorFixedBoundaryBlendBlocks, ScaleContext), ConvertBlockZDistanceToNoise(InclusiveRadii.Z, ScaleContext)));
		FNodeLink ZMask = Editor->MinFloat(Editor->DivideFloat(Editor->Min(BelowTopZ, AboveBottomZ), EdgeBlendZ), 1.0f);
		return Editor->Min(PlanarMask, ZMask);
	}

	FNodeLink BuildSurfaceLocalBoxReservationMaskForSolvedSurface(
		UFastNoiseEditor* Editor,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const FVector& HalfExtent,
		const FResolvedReservationSurfaceZNoise& SurfaceZ,
		const bool bUseReservationSurfaceClearance)
	{
		const FVector2D CenterXY = ConvertAuthoredCenterXYToNoise(ReservationPayload.CenterXY, ScaleContext);
		const FVector InclusiveHalfExtent = ExpandExtentForVoxelCenterCoverage(HalfExtent);
		const FVector NoiseHalfExtent = ConvertBlockDistanceToNoiseAbs(InclusiveHalfExtent, ScaleContext);
		const float HalfExtentX = FMath::Max(static_cast<float>(NoiseHalfExtent.X), UE_KINDA_SMALL_NUMBER);
		const float HalfExtentY = FMath::Max(static_cast<float>(NoiseHalfExtent.Y), UE_KINDA_SMALL_NUMBER);
		const float DomainBottomZ = SurfaceZ.AnchorSurfaceZ - ConvertBlockZDistanceToNoise(InclusiveHalfExtent.Z, ScaleContext);
		const float DomainTopZ = bUseReservationSurfaceClearance
			? FMath::Max(
				SurfaceZ.AnchorSurfaceZ + ConvertBlockZDistanceToNoise(FMath::Max(InclusiveHalfExtent.X, InclusiveHalfExtent.Y), ScaleContext),
				SurfaceZ.FoundationSurfaceZ + ConvertBlockZDistanceToNoise(0.5f, ScaleContext))
			: SurfaceZ.AnchorSurfaceZ + ConvertBlockZDistanceToNoise(InclusiveHalfExtent.Z, ScaleContext);
		FNodeLink PositionX = Editor->PositionOutput(FVector(1.0, 0.0, 0.0), FVector::ZeroVector);
		FNodeLink PositionY = Editor->PositionOutput(FVector(0.0, 1.0, 0.0), FVector::ZeroVector);
		FNodeLink PositionZ = Editor->PositionOutput(FVector(0.0, 0.0, 1.0), FVector::ZeroVector);
		FNodeLink BelowMaxX = Editor->Subtract(Editor->Constant(CenterXY.X + HalfExtentX), PositionX);
		FNodeLink AboveMinX = Editor->Subtract(PositionX, Editor->Constant(CenterXY.X - HalfExtentX));
		FNodeLink BelowMaxY = Editor->Subtract(Editor->Constant(CenterXY.Y + HalfExtentY), PositionY);
		FNodeLink AboveMinY = Editor->Subtract(PositionY, Editor->Constant(CenterXY.Y - HalfExtentY));
		FNodeLink BelowMaxZ = Editor->Subtract(Editor->Constant(DomainTopZ), PositionZ);
		FNodeLink AboveMinZ = Editor->Subtract(PositionZ, Editor->Constant(DomainBottomZ));
		const float EdgeBlendX = FMath::Max(
			UE_KINDA_SMALL_NUMBER,
			FMath::Min(ConvertBlockDistanceToNoiseAbs(FVector(SurfaceAnchorFixedBoundaryBlendBlocks, 0.0, 0.0), ScaleContext).X, HalfExtentX));
		const float EdgeBlendY = FMath::Max(
			UE_KINDA_SMALL_NUMBER,
			FMath::Min(ConvertBlockDistanceToNoiseAbs(FVector(0.0, SurfaceAnchorFixedBoundaryBlendBlocks, 0.0), ScaleContext).Y, HalfExtentY));
		const float EdgeBlendZ = FMath::Max(
			UE_KINDA_SMALL_NUMBER,
			FMath::Min(ConvertBlockZDistanceToNoise(SurfaceAnchorFixedBoundaryBlendBlocks, ScaleContext), ConvertBlockZDistanceToNoise(InclusiveHalfExtent.Z, ScaleContext)));
		FNodeLink XMask = Editor->MinFloat(Editor->DivideFloat(Editor->Min(BelowMaxX, AboveMinX), EdgeBlendX), 1.0f);
		FNodeLink YMask = Editor->MinFloat(Editor->DivideFloat(Editor->Min(BelowMaxY, AboveMinY), EdgeBlendY), 1.0f);
		FNodeLink ZMask = Editor->MinFloat(Editor->DivideFloat(Editor->Min(BelowMaxZ, AboveMinZ), EdgeBlendZ), 1.0f);
		return Editor->Min(Editor->Min(XMask, YMask), ZMask);
	}

	FNodeLink BuildSurfaceLocalReservationMask(
		UFastNoiseEditor* Editor,
		const FIslandFoundationShapePayload& IslandPayload,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const bool bUseSpawnDimensions)
	{
		const bool bUseReservationSurfaceClearance = !bUseSpawnDimensions;
		if (ReservationPayload.Shape == ESurfaceAnchorReservationShape::Box)
		{
			const FVector HalfExtent = bUseSpawnDimensions
				? GetSpawnBoxHalfExtent(ReservationPayload)
				: GetAnchorBoxHalfExtent(ReservationPayload);
			return BuildSurfaceLocalBoxReservationMask(Editor, IslandPayload, ReservationPayload, ScaleContext, HalfExtent, bUseReservationSurfaceClearance);
		}

		const FVector Radii = bUseSpawnDimensions
			? GetSpawnSphereRadii(ReservationPayload)
			: GetAnchorSphereRadii(ReservationPayload);
		return BuildSurfaceLocalSphereReservationMask(Editor, IslandPayload, ReservationPayload, ScaleContext, Radii, bUseReservationSurfaceClearance);
	}

	FNodeLink BuildInfinitePlaneSurfaceLocalReservationMask(
		UFastNoiseEditor* Editor,
		const FInfinitePlaneFoundationPayload& PlanePayload,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const bool bUseSpawnDimensions)
	{
		const bool bUseReservationSurfaceClearance = !bUseSpawnDimensions;
		const FResolvedReservationSurfaceZNoise SurfaceZ = SolveInfinitePlaneReservationSurfaceZNoise(PlanePayload, ReservationPayload, ScaleContext);
		if (ReservationPayload.Shape == ESurfaceAnchorReservationShape::Box)
		{
			const FVector HalfExtent = bUseSpawnDimensions
				? GetSpawnBoxHalfExtent(ReservationPayload)
				: GetAnchorBoxHalfExtent(ReservationPayload);
			return BuildSurfaceLocalBoxReservationMaskForSolvedSurface(Editor, ReservationPayload, ScaleContext, HalfExtent, SurfaceZ, bUseReservationSurfaceClearance);
		}

		const FVector Radii = bUseSpawnDimensions
			? GetSpawnSphereRadii(ReservationPayload)
			: GetAnchorSphereRadii(ReservationPayload);
		return BuildSurfaceLocalSphereReservationMaskForSolvedSurface(Editor, ReservationPayload, ScaleContext, Radii, SurfaceZ, bUseReservationSurfaceClearance);
	}

	FNodeLink BuildSingleReservationDomain(
		UFastNoiseEditor* Editor,
		const FIslandFoundationShapePayload& IslandPayload,
		const FReservationDefinition& Reservation,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const bool bUseSpawnDimensions)
	{
		if (Editor == nullptr)
		{
			return MakeIslandZero(Editor);
		}

		const FSurfaceAnchorReservationPayload* ReservationPayload = GetSurfaceAnchorReservationPayload(Reservation);
		if (ReservationPayload == nullptr)
		{
			return MakeIslandZero(Editor);
		}

		return BuildSurfaceLocalReservationMask(
			Editor,
			IslandPayload,
			*ReservationPayload,
			ScaleContext,
			bUseSpawnDimensions);
	}

	FNodeLink BuildSingleReservationDomain(
		UFastNoiseEditor* Editor,
		const UBiomeStrategyData* Strategy,
		const FReservationDefinition& Reservation,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const bool bUseSpawnDimensions)
	{
		const FIslandFoundationShapePayload* IslandPayload = GetIslandFoundationPayload(Strategy);
		return IslandPayload != nullptr
			? BuildSingleReservationDomain(Editor, *IslandPayload, Reservation, ScaleContext, bUseSpawnDimensions)
			: MakeIslandZero(Editor);
	}

	FNodeLink BuildSurfaceLiftFalloff(
		UFastNoiseEditor* Editor,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const float SupportWidthBlocks)
	{
		if (Editor == nullptr)
		{
			return FNodeLink();
		}

		const float WidthBlocks = FMath::Max(UE_KINDA_SMALL_NUMBER, SupportWidthBlocks);
		const FVector2D CenterXY = ConvertAuthoredCenterXYToNoise(ReservationPayload.CenterXY, ScaleContext);
		if (ReservationPayload.Shape == ESurfaceAnchorReservationShape::Box)
		{
			const FVector ExpandedHalfExtent = ExpandExtentForVoxelCenterCoverage(GetAnchorBoxHalfExtent(ReservationPayload) + FVector(WidthBlocks, WidthBlocks, 0.0f));
			const FVector NoiseHalfExtent = ConvertBlockDistanceToNoiseAbs(ExpandedHalfExtent, ScaleContext);
			const float HalfExtentX = FMath::Max(static_cast<float>(NoiseHalfExtent.X), UE_KINDA_SMALL_NUMBER);
			const float HalfExtentY = FMath::Max(static_cast<float>(NoiseHalfExtent.Y), UE_KINDA_SMALL_NUMBER);
			const FVector WidthNoise = ConvertBlockDistanceToNoiseAbs(FVector(WidthBlocks, WidthBlocks, 0.0), ScaleContext);
			const float WidthX = FMath::Max(static_cast<float>(WidthNoise.X), UE_KINDA_SMALL_NUMBER);
			const float WidthY = FMath::Max(static_cast<float>(WidthNoise.Y), UE_KINDA_SMALL_NUMBER);

			FNodeLink PositionX = Editor->PositionOutput(FVector(1.0, 0.0, 0.0), FVector::ZeroVector);
			FNodeLink PositionY = Editor->PositionOutput(FVector(0.0, 1.0, 0.0), FVector::ZeroVector);
			FNodeLink BelowMaxX = Editor->Subtract(Editor->Constant(CenterXY.X + HalfExtentX), PositionX);
			FNodeLink AboveMinX = Editor->Subtract(PositionX, Editor->Constant(CenterXY.X - HalfExtentX));
			FNodeLink BelowMaxY = Editor->Subtract(Editor->Constant(CenterXY.Y + HalfExtentY), PositionY);
			FNodeLink AboveMinY = Editor->Subtract(PositionY, Editor->Constant(CenterXY.Y - HalfExtentY));
			FNodeLink XFalloff = Editor->MinFloat(Editor->DivideFloat(Editor->Min(BelowMaxX, AboveMinX), WidthX), 1.0f);
			FNodeLink YFalloff = Editor->MinFloat(Editor->DivideFloat(Editor->Min(BelowMaxY, AboveMinY), WidthY), 1.0f);
			return Editor->Min(XFalloff, YFalloff);
		}

		const FVector ExpandedRadii = ExpandSphereRadiiForVoxelCenterCoverage(GetAnchorSphereRadii(ReservationPayload) + FVector(WidthBlocks, WidthBlocks, 0.0f));
		const FVector InnerRadii = ExpandSphereRadiiForVoxelCenterCoverage(GetAnchorSphereRadii(ReservationPayload));
		const FVector NoiseRadii = ConvertBlockDistanceToNoiseAbs(ExpandedRadii, ScaleContext);
		FNodeLink NormalizedX = Editor->DivideFloat(
			BuildPositionDelta(Editor, FVector(1.0, 0.0, 0.0), CenterXY.X),
			FMath::Max(static_cast<float>(NoiseRadii.X), UE_KINDA_SMALL_NUMBER));
		FNodeLink NormalizedY = Editor->DivideFloat(
			BuildPositionDelta(Editor, FVector(0.0, 1.0, 0.0), CenterXY.Y),
			FMath::Max(static_cast<float>(NoiseRadii.Y), UE_KINDA_SMALL_NUMBER));
		FNodeLink PlanarDistanceSquared = Editor->Add(Editor->PowInt(NormalizedX, 2), Editor->PowInt(NormalizedY, 2));
		FNodeLink ExpandedMask = Editor->Subtract(Editor->Constant(1.0f), PlanarDistanceSquared);
		const float InnerToOuterRatioX = FMath::Clamp(static_cast<float>(FMath::Abs(ConvertBlockDistanceToNoiseAbs(InnerRadii, ScaleContext).X) / FMath::Max(FMath::Abs(NoiseRadii.X), UE_KINDA_SMALL_NUMBER)), 0.0f, 1.0f);
		const float InnerToOuterRatioY = FMath::Clamp(static_cast<float>(FMath::Abs(ConvertBlockDistanceToNoiseAbs(InnerRadii, ScaleContext).Y) / FMath::Max(FMath::Abs(NoiseRadii.Y), UE_KINDA_SMALL_NUMBER)), 0.0f, 1.0f);
		const float EdgeScale = FMath::Max(UE_KINDA_SMALL_NUMBER, 1.0f - FMath::Square((InnerToOuterRatioX + InnerToOuterRatioY) * 0.5f));
		return Editor->MinFloat(Editor->DivideFloat(ExpandedMask, EdgeScale), 1.0f);
	}

	FNodeLink BuildSurfaceAnchorInteriorEdgeBlendAlpha(
		UFastNoiseEditor* Editor,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const float EdgeBlendWidthBlocks)
	{
		if (Editor == nullptr || EdgeBlendWidthBlocks <= 0.0f)
		{
			return FNodeLink();
		}

		const FVector2D CenterXY = ConvertAuthoredCenterXYToNoise(ReservationPayload.CenterXY, ScaleContext);
		if (ReservationPayload.Shape == ESurfaceAnchorReservationShape::Box)
		{
			const FVector HalfExtent = ExpandExtentForVoxelCenterCoverage(GetAnchorBoxHalfExtent(ReservationPayload));
			const FVector NoiseHalfExtent = ConvertBlockDistanceToNoiseAbs(HalfExtent, ScaleContext);
			const float HalfExtentX = FMath::Max(static_cast<float>(NoiseHalfExtent.X), UE_KINDA_SMALL_NUMBER);
			const float HalfExtentY = FMath::Max(static_cast<float>(NoiseHalfExtent.Y), UE_KINDA_SMALL_NUMBER);
			const FVector WidthNoise = ConvertBlockDistanceToNoiseAbs(FVector(EdgeBlendWidthBlocks, EdgeBlendWidthBlocks, 0.0), ScaleContext);
			const float WidthX = FMath::Max(UE_KINDA_SMALL_NUMBER, FMath::Min(static_cast<float>(WidthNoise.X), HalfExtentX));
			const float WidthY = FMath::Max(UE_KINDA_SMALL_NUMBER, FMath::Min(static_cast<float>(WidthNoise.Y), HalfExtentY));

			FNodeLink PositionX = Editor->PositionOutput(FVector(1.0, 0.0, 0.0), FVector::ZeroVector);
			FNodeLink PositionY = Editor->PositionOutput(FVector(0.0, 1.0, 0.0), FVector::ZeroVector);
			FNodeLink BelowMaxX = Editor->Subtract(Editor->Constant(CenterXY.X + HalfExtentX), PositionX);
			FNodeLink AboveMinX = Editor->Subtract(PositionX, Editor->Constant(CenterXY.X - HalfExtentX));
			FNodeLink BelowMaxY = Editor->Subtract(Editor->Constant(CenterXY.Y + HalfExtentY), PositionY);
			FNodeLink AboveMinY = Editor->Subtract(PositionY, Editor->Constant(CenterXY.Y - HalfExtentY));
			FNodeLink XAlpha = Editor->MinFloat(Editor->DivideFloat(Editor->Min(BelowMaxX, AboveMinX), WidthX), 1.0f);
			FNodeLink YAlpha = Editor->MinFloat(Editor->DivideFloat(Editor->Min(BelowMaxY, AboveMinY), WidthY), 1.0f);
			return Editor->MaxFloat(Editor->Min(XAlpha, YAlpha), 0.0f);
		}

		const FVector Radii = ExpandSphereRadiiForVoxelCenterCoverage(GetAnchorSphereRadii(ReservationPayload));
		const FVector NoiseRadii = ConvertBlockDistanceToNoiseAbs(Radii, ScaleContext);
		const float AverageRadiusNoise = FMath::Max(
			UE_KINDA_SMALL_NUMBER,
			static_cast<float>((FMath::Abs(NoiseRadii.X) + FMath::Abs(NoiseRadii.Y)) * 0.5));
		const float BlendWidthNoise = FMath::Min(
			AverageRadiusNoise,
			FMath::Max(UE_KINDA_SMALL_NUMBER, ConvertBlockXDistanceToNoise(EdgeBlendWidthBlocks, ScaleContext)));
		FNodeLink NormalizedX = Editor->DivideFloat(
			BuildPositionDelta(Editor, FVector(1.0, 0.0, 0.0), CenterXY.X),
			FMath::Max(static_cast<float>(NoiseRadii.X), UE_KINDA_SMALL_NUMBER));
		FNodeLink NormalizedY = Editor->DivideFloat(
			BuildPositionDelta(Editor, FVector(0.0, 1.0, 0.0), CenterXY.Y),
			FMath::Max(static_cast<float>(NoiseRadii.Y), UE_KINDA_SMALL_NUMBER));
		FNodeLink PlanarDistanceSquared = Editor->Add(Editor->PowInt(NormalizedX, 2), Editor->PowInt(NormalizedY, 2));
		FNodeLink PlanarMask = Editor->Subtract(Editor->Constant(1.0f), PlanarDistanceSquared);
		const float InnerRadiusRatio = FMath::Clamp((AverageRadiusNoise - BlendWidthNoise) / AverageRadiusNoise, 0.0f, 1.0f);
		const float EdgeScale = FMath::Max(UE_KINDA_SMALL_NUMBER, 1.0f - FMath::Square(InnerRadiusRatio));
		return Editor->MaxFloat(Editor->MinFloat(Editor->DivideFloat(PlanarMask, EdgeScale), 1.0f), 0.0f);
	}

	FNodeLink ApplySurfaceAnchorTerrainEdgeBlend(
		UFastNoiseEditor* Editor,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const FResolvedReservationSurfaceZNoise& SurfaceZ,
		const FResolvedSurfaceAnchorTerrain& TerrainSettings,
		FNodeLink SurfaceHeight)
	{
		if (Editor == nullptr || !IsIslandNodeValid(SurfaceHeight) || !TerrainSettings.bUseEdgeBlend)
		{
			return SurfaceHeight;
		}

		FNodeLink BlendAlpha = BuildSurfaceAnchorInteriorEdgeBlendAlpha(Editor, ReservationPayload, ScaleContext, TerrainSettings.EdgeBlendWidthBlocks);
		if (!IsIslandNodeValid(BlendAlpha))
		{
			return SurfaceHeight;
		}

		FNodeLink FoundationSurface = Editor->Constant(SurfaceZ.FoundationSurfaceZ);
		return Editor->Add(FoundationSurface, Editor->Multiply(Editor->Subtract(SurfaceHeight, FoundationSurface), BlendAlpha));
	}

	FNodeLink BuildSurfaceLiftSupportDensity(
		UFastNoiseEditor* Editor,
		const FIslandFoundationShapePayload& IslandPayload,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		const float LiftBlocks = GetSurfaceZLiftBlocks(ReservationPayload);
		if (Editor == nullptr || FMath::IsNearlyZero(LiftBlocks, UE_KINDA_SMALL_NUMBER))
		{
			return FNodeLink();
		}

		const FResolvedReservationSurfaceZNoise SurfaceZ = SolveReservationSurfaceZNoise(Editor, IslandPayload, ReservationPayload, ScaleContext);
		const float SupportWidthBlocks = FMath::Max(1.0f, FMath::Abs(LiftBlocks));
		FNodeLink Falloff = BuildSurfaceLiftFalloff(Editor, ReservationPayload, ScaleContext, SupportWidthBlocks);
		if (!IsIslandNodeValid(Falloff))
		{
			return FNodeLink();
		}

		FNodeLink PositionZ = Editor->PositionOutput(FVector(0.0, 0.0, 1.0), FVector::ZeroVector);
		FNodeLink LiftNoise = Editor->MultiplyFloat(Falloff, ConvertBlockZOffsetToNoise(LiftBlocks, ScaleContext));
		FNodeLink SupportSurface = Editor->Add(Editor->Constant(SurfaceZ.FoundationSurfaceZ), LiftNoise);
		return Editor->Subtract(PositionZ, SupportSurface);
	}

	FNodeLink BuildIslandProviderSurfaceLiftSupportDensity(
		UFastNoiseEditor* Editor,
		const FFoundationProviderDefinition& Provider,
		const FIslandFoundationShapePayload& IslandPayload,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		const float LiftBlocks = GetSurfaceZLiftBlocks(ReservationPayload);
		if (Editor == nullptr || FMath::IsNearlyZero(LiftBlocks, UE_KINDA_SMALL_NUMBER))
		{
			return FNodeLink();
		}

		const FResolvedReservationSurfaceZNoise SurfaceZ = SolveIslandProviderReservationSurfaceZNoise(Editor, Provider, IslandPayload, ReservationPayload, ScaleContext);
		const float SupportWidthBlocks = FMath::Max(1.0f, FMath::Abs(LiftBlocks));
		FNodeLink Falloff = BuildSurfaceLiftFalloff(Editor, ReservationPayload, ScaleContext, SupportWidthBlocks);
		if (!IsIslandNodeValid(Falloff))
		{
			return FNodeLink();
		}

		FNodeLink PositionZ = Editor->PositionOutput(FVector(0.0, 0.0, 1.0), FVector::ZeroVector);
		FNodeLink LiftNoise = Editor->MultiplyFloat(Falloff, ConvertBlockZOffsetToNoise(LiftBlocks, ScaleContext));
		FNodeLink SupportSurface = Editor->Add(Editor->Constant(SurfaceZ.FoundationSurfaceZ), LiftNoise);
		return Editor->Subtract(PositionZ, SupportSurface);
	}

	TArray<const FFoundationProviderDefinition*> GetChildFoundationProviders(const FFoundationProviderDefinition& Provider)
	{
		TArray<const FFoundationProviderDefinition*> Children;
		Children.Reserve(Provider.ChildFoundations.Num());
		for (const FInstancedStruct& ChildProviderStruct : Provider.ChildFoundations)
		{
			if (const FFoundationProviderDefinition* ChildProvider = ChildProviderStruct.GetPtr<FFoundationProviderDefinition>())
			{
				Children.Add(ChildProvider);
			}
		}

		return Children;
	}

	FNodeLink BuildProviderEnvelopeDomain(
		UFastNoiseEditor* Editor,
		const FFoundationProviderDefinition& Provider,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		if (const FIslandFoundationShapePayload* IslandPayload = GetIslandFoundationPayload(Provider))
		{
			const float ExtraTopPaddingBlocks = FMath::Max(
				GetMaxPositiveSurfaceZLiftBlocks(Provider),
				GetMaxPositiveFoundationTerrainProfileHeightBlocks(Provider));
			return BuildIslandEnvelopeDomainForPayload(Editor, *IslandPayload, ScaleContext, ExtraTopPaddingBlocks);
		}
		if (const FInfinitePlaneFoundationPayload* PlanePayload = GetInfinitePlaneFoundationPayload(Provider))
		{
			const float ExtraTopPaddingBlocks = FMath::Max(
				GetMaxPositiveSurfaceZLiftBlocks(Provider),
				GetMaxPositiveFoundationTerrainProfileHeightBlocks(Provider));
			return BuildInfinitePlaneDomainForPayload(Editor, *PlanePayload, ScaleContext, ExtraTopPaddingBlocks);
		}
		if (const FVCutFoundationPayload* VCutPayload = GetVCutFoundationPayload(Provider))
		{
			return BuildVCutDomainForPayload(Editor, *VCutPayload, ScaleContext);
		}

		return MakeIslandZero(Editor);
	}

	FNodeLink BuildClippedProviderReservationDomain(
		UFastNoiseEditor* Editor,
		const FFoundationProviderDefinition& Provider,
		const FReservationDefinition& Reservation,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const bool bUseSpawnDimensions)
	{
		if (const FIslandFoundationShapePayload* IslandPayload = GetIslandFoundationPayload(Provider))
		{
			const FSurfaceAnchorReservationPayload* ReservationPayload = GetSurfaceAnchorReservationPayload(Reservation);
			if (ReservationPayload == nullptr)
			{
				return MakeIslandZero(Editor);
			}

			FReservationDefinition ResolvedReservation = Reservation;
			ResolvedReservation.ReservationPayload.InitializeAs<FSurfaceAnchorReservationPayload>(
				ResolveProviderLocalReservationPayload(*IslandPayload, *ReservationPayload));
			const FSurfaceAnchorReservationPayload* ResolvedPayload = GetSurfaceAnchorReservationPayload(ResolvedReservation);
			if (ResolvedPayload == nullptr)
			{
				return MakeIslandZero(Editor);
			}

			const FResolvedReservationSurfaceZNoise SurfaceZ = SolveIslandProviderReservationSurfaceZNoise(Editor, Provider, *IslandPayload, *ResolvedPayload, ScaleContext);
			const FVector Extent = bUseSpawnDimensions
				? (ResolvedPayload->Shape == ESurfaceAnchorReservationShape::Box ? GetSpawnBoxHalfExtent(*ResolvedPayload) : GetSpawnSphereRadii(*ResolvedPayload))
				: (ResolvedPayload->Shape == ESurfaceAnchorReservationShape::Box ? GetAnchorBoxHalfExtent(*ResolvedPayload) : GetAnchorSphereRadii(*ResolvedPayload));
			FNodeLink ReservationDomain = ResolvedPayload->Shape == ESurfaceAnchorReservationShape::Box
				? BuildSurfaceLocalBoxReservationMaskForSolvedSurface(Editor, *ResolvedPayload, ScaleContext, Extent, SurfaceZ, !bUseSpawnDimensions)
				: BuildSurfaceLocalSphereReservationMaskForSolvedSurface(Editor, *ResolvedPayload, ScaleContext, Extent, SurfaceZ, !bUseSpawnDimensions);
			FNodeLink ReservationClipDomain = BuildIslandReservationClipDomain(Editor, *IslandPayload, ScaleContext);
			return IsIslandNodeValid(ReservationDomain) && IsIslandNodeValid(ReservationClipDomain)
				? Editor->Min(ReservationDomain, ReservationClipDomain)
				: MakeIslandZero(Editor);
		}

		if (const FInfinitePlaneFoundationPayload* PlanePayload = GetInfinitePlaneFoundationPayload(Provider))
		{
			if (const FSurfaceAnchorReservationPayload* ReservationPayload = GetSurfaceAnchorReservationPayload(Reservation))
			{
				return BuildInfinitePlaneSurfaceLocalReservationMask(Editor, *PlanePayload, *ReservationPayload, ScaleContext, bUseSpawnDimensions);
			}
		}

		return MakeIslandZero(Editor);
	}

	FNodeLink BuildCarvedProviderDomainForOwnBiome(
		UFastNoiseEditor* Editor,
		const FFoundationProviderDefinition& Provider,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		FNodeLink ProviderDomain = BuildProviderEnvelopeDomain(Editor, Provider, ScaleContext);
		if (!IsIslandNodeValid(ProviderDomain))
		{
			return MakeIslandZero(Editor);
		}

		TArray<FNodeLink> CarveDomains;
		for (const FReservationDefinition& Reservation : Provider.Reservations)
		{
			if (!Reservation.BiomeTag.IsValid() || Reservation.BiomeTag == Provider.BiomeTag)
			{
				continue;
			}

			const FSurfaceAnchorReservationPayload* ReservationPayload = GetSurfaceAnchorReservationPayload(Reservation);
			if (ReservationPayload == nullptr)
			{
				continue;
			}

			FNodeLink ReservationDomain = BuildClippedProviderReservationDomain(Editor, Provider, Reservation, ScaleContext, false);
				if (IsIslandNodeValid(ReservationDomain))
				{
					// The reservation owns the full authored footprint. Lift support is
					// still generated by the foundation GenA outside that footprint.
					CarveDomains.Add(ReservationDomain);
				}
		}

		for (const FFoundationProviderDefinition* ChildProvider : GetChildFoundationProviders(Provider))
		{
			if (ChildProvider == nullptr)
			{
				continue;
			}

			const bool bChildCarvesParent =
				ChildProvider->ContributionType == EFoundationContributionType::SubtractiveVoid
				|| (ChildProvider->ContributionType == EFoundationContributionType::AdditiveBiome && ChildProvider->BiomeTag != Provider.BiomeTag);
			if (!bChildCarvesParent)
			{
				continue;
			}

			FNodeLink ChildEnvelope = BuildProviderEnvelopeDomain(Editor, *ChildProvider, ScaleContext);
			if (IsIslandNodeValid(ChildEnvelope))
			{
				CarveDomains.Add(ChildEnvelope);
			}
		}

		if (CarveDomains.IsEmpty())
		{
			return ProviderDomain;
		}

		FNodeLink Union = UReservationFastNoiseLibrary::UnionReservations(Editor, CarveDomains);
		FNodeLink CarveDomain = Editor->MultiplyFloat(Union, FoundationDomainCarveStrength);
		return UReservationFastNoiseLibrary::CarveReservationFromBase(Editor, ProviderDomain, CarveDomain);
	}

	FNodeLink BuildProviderDomainForBiomeTag(
		UFastNoiseEditor* Editor,
		const FFoundationProviderDefinition& Provider,
		const FGameplayTag BiomeTag,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		TArray<FNodeLink> Contributions;
		if (Provider.ContributionType == EFoundationContributionType::AdditiveBiome)
		{
			if (Provider.BiomeTag == BiomeTag)
			{
				FNodeLink ProviderDomain = BuildCarvedProviderDomainForOwnBiome(Editor, Provider, ScaleContext);
				if (IsIslandNodeValid(ProviderDomain))
				{
					Contributions.Add(ProviderDomain);
				}
			}

			for (const FReservationDefinition& Reservation : Provider.Reservations)
			{
				if (Reservation.BiomeTag != BiomeTag || GetSurfaceAnchorReservationPayload(Reservation) == nullptr)
				{
					continue;
				}

				FNodeLink ReservationDomain = BuildClippedProviderReservationDomain(Editor, Provider, Reservation, ScaleContext, false);
				if (IsIslandNodeValid(ReservationDomain))
				{
					Contributions.Add(ReservationDomain);
				}
			}
		}

		for (const FFoundationProviderDefinition* ChildProvider : GetChildFoundationProviders(Provider))
		{
			if (ChildProvider == nullptr)
			{
				continue;
			}

			FNodeLink ChildDomain = BuildProviderDomainForBiomeTag(Editor, *ChildProvider, BiomeTag, ScaleContext);
			if (IsIslandNodeValid(ChildDomain))
			{
				Contributions.Add(ChildDomain);
			}
		}

		return Contributions.IsEmpty()
			? FNodeLink()
			: UReservationFastNoiseLibrary::UnionReservations(Editor, Contributions);
	}

	FNodeLink BuildProviderReservationDomainForBiomeTag(
		UFastNoiseEditor* Editor,
		const FFoundationProviderDefinition& Provider,
		const FGameplayTag BiomeTag,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		TArray<FNodeLink> Contributions;
		if (Provider.ContributionType == EFoundationContributionType::AdditiveBiome)
		{
			for (const FReservationDefinition& Reservation : Provider.Reservations)
			{
				if (Reservation.BiomeTag != BiomeTag || GetSurfaceAnchorReservationPayload(Reservation) == nullptr)
				{
					continue;
				}

				FNodeLink ReservationDomain = BuildClippedProviderReservationDomain(Editor, Provider, Reservation, ScaleContext, false);
				if (IsIslandNodeValid(ReservationDomain))
				{
					Contributions.Add(ReservationDomain);
				}
			}
		}

		for (const FFoundationProviderDefinition* ChildProvider : GetChildFoundationProviders(Provider))
		{
			if (ChildProvider == nullptr)
			{
				continue;
			}

			FNodeLink ChildDomain = BuildProviderReservationDomainForBiomeTag(Editor, *ChildProvider, BiomeTag, ScaleContext);
			if (IsIslandNodeValid(ChildDomain))
			{
				Contributions.Add(ChildDomain);
			}
		}

		return Contributions.IsEmpty()
			? FNodeLink()
			: UReservationFastNoiseLibrary::UnionReservations(Editor, Contributions);
	}

	/** Builds flat terrain where authored surface Z is the visible top face, not the top solid cell center. */
	FNodeLink BuildTopFaceAlignedFlatTerrainDensity(
		UFastNoiseEditor* Editor,
		const FResolvedWorldGenScaleContext& ScaleContext,
		FNodeLink SurfaceHeight)
	{
		FNodeLink PositionZ = Editor->PositionOutput(FVector(0.0, 0.0, 1.0), FVector::ZeroVector);
		FNodeLink SolidSampleHeight = Editor->Add(
			SurfaceHeight,
			Editor->Constant(ConvertBlockZOffsetToNoise(AuthoredSurfaceTopFaceSolidSampleOffsetBlocks, ScaleContext)));
		FNodeLink TerrainDensity = Editor->Subtract(PositionZ, SolidSampleHeight);
		if (!FMath::IsNearlyEqual(InternalTerrainDensityScale, 1.0f))
		{
			TerrainDensity = Editor->MultiplyFloat(TerrainDensity, InternalTerrainDensityScale);
		}
		if (!FMath::IsNearlyZero(InternalSurfaceSolidBias))
		{
			TerrainDensity = Editor->Subtract(TerrainDensity, Editor->Constant(InternalSurfaceSolidBias));
		}
		return TerrainDensity;
	}

	FNodeLink BuildProviderFoundationGenA(
		UFastNoiseEditor* Editor,
		const FFoundationProviderDefinition& Provider,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		if (const FInfinitePlaneFoundationPayload* PlanePayload = GetInfinitePlaneFoundationPayload(Provider))
		{
			const float SurfaceZNoise = static_cast<float>(ScaleContext.AuthoredBlockPositionToNoise(FVector(0.0, 0.0, PlanePayload->SurfaceZ)).Z);
			FNodeLink SurfaceHeight = Editor->Constant(SurfaceZNoise);
			FNodeLink SurfaceOffset = BuildFoundationTerrainProfileSurfaceOffset(Editor, Provider, ScaleContext);
			if (IsIslandNodeValid(SurfaceOffset))
			{
				SurfaceHeight = Editor->Add(SurfaceHeight, SurfaceOffset);
			}
			return BuildTopFaceAlignedFlatTerrainDensity(Editor, ScaleContext, SurfaceHeight);
		}

		const FIslandFoundationShapePayload* IslandPayload = GetIslandFoundationPayload(Provider);
		if (IslandPayload == nullptr)
		{
			return MakeIslandZero(Editor);
		}

		FNodeLink PositiveInsideBody = BuildIslandPositiveInsideBody(Editor, Provider, *IslandPayload, ScaleContext);
		if (!IsIslandNodeValid(PositiveInsideBody))
		{
			return MakeIslandZero(Editor);
		}

		FNodeLink TerrainDensity = Editor->MultiplyFloat(PositiveInsideBody, -1.0f);
		if (!FMath::IsNearlyEqual(InternalTerrainDensityScale, 1.0f))
		{
			TerrainDensity = Editor->MultiplyFloat(TerrainDensity, InternalTerrainDensityScale);
		}
		if (!FMath::IsNearlyZero(InternalSurfaceSolidBias))
		{
			// Keep the authored foundation surface as a visible top face. If a
			// foundation remains weakly owned around a reservation edge, it should
			// not create a one-block bump above the reservation surface.
			TerrainDensity = Editor->Add(TerrainDensity, Editor->Constant(InternalSurfaceSolidBias));
		}

		for (const FReservationDefinition& Reservation : Provider.Reservations)
		{
			const FSurfaceAnchorReservationPayload* ReservationPayload = GetSurfaceAnchorReservationPayload(Reservation);
			if (ReservationPayload == nullptr || FMath::IsNearlyZero(GetSurfaceZLiftBlocks(*ReservationPayload), UE_KINDA_SMALL_NUMBER))
			{
				continue;
			}

			const FSurfaceAnchorReservationPayload ResolvedReservationPayload = ResolveProviderLocalReservationPayload(*IslandPayload, *ReservationPayload);
			FNodeLink SupportDensity = BuildIslandProviderSurfaceLiftSupportDensity(Editor, Provider, *IslandPayload, ResolvedReservationPayload, ScaleContext);
			if (IsIslandNodeValid(SupportDensity))
			{
				TerrainDensity = GetSurfaceZLiftBlocks(ResolvedReservationPayload) > 0.0f
					? Editor->Min(TerrainDensity, SupportDensity)
					: Editor->Max(TerrainDensity, SupportDensity);
			}
		}

		return TerrainDensity;
	}

	FNodeLink BuildSingleReservationGenA(
		UFastNoiseEditor* Editor,
		const FIslandFoundationShapePayload& IslandPayload,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		const FResolvedReservationSurfaceZNoise SurfaceZ = SolveReservationSurfaceZNoise(Editor, IslandPayload, ReservationPayload, ScaleContext);
		const FResolvedSurfaceAnchorTerrain TerrainSettings = ResolveSurfaceAnchorTerrain(ReservationPayload);
		FNodeLink SurfaceHeight = Editor->Constant(SurfaceZ.AnchorSurfaceZ);
		if (TerrainSettings.bUseSurfaceNoise)
		{
			FNodeLink SurfaceNoise = BuildIslandPlanarSimplexHeightNoise(
				Editor,
				ScaleContext,
				TerrainSettings.SurfaceNoiseAmplitude,
				TerrainSettings.SurfaceNoiseScale,
				TerrainSettings.SurfaceNoiseSeedOffset);
			if (IsIslandNodeValid(SurfaceNoise))
			{
				SurfaceNoise = ApplySurfaceTerraces(Editor, ScaleContext, SurfaceNoise, TerrainSettings);
				SurfaceHeight = Editor->Add(SurfaceHeight, SurfaceNoise);
			}
		}
		SurfaceHeight = ApplySurfaceAnchorTerrainEdgeBlend(Editor, ReservationPayload, ScaleContext, SurfaceZ, TerrainSettings, SurfaceHeight);

		return BuildTopFaceAlignedFlatTerrainDensity(Editor, ScaleContext, SurfaceHeight);
	}

	FNodeLink BuildInfinitePlaneSingleReservationGenA(
		UFastNoiseEditor* Editor,
		const FInfinitePlaneFoundationPayload& PlanePayload,
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		const FResolvedReservationSurfaceZNoise SurfaceZ = SolveInfinitePlaneReservationSurfaceZNoise(PlanePayload, ReservationPayload, ScaleContext);
		const FResolvedSurfaceAnchorTerrain TerrainSettings = ResolveSurfaceAnchorTerrain(ReservationPayload);
		FNodeLink SurfaceHeight = Editor->Constant(SurfaceZ.AnchorSurfaceZ);
		if (TerrainSettings.bUseSurfaceNoise)
		{
			FNodeLink SurfaceNoise = BuildIslandPlanarSimplexHeightNoise(
				Editor,
				ScaleContext,
				TerrainSettings.SurfaceNoiseAmplitude,
				TerrainSettings.SurfaceNoiseScale,
				TerrainSettings.SurfaceNoiseSeedOffset);
			if (IsIslandNodeValid(SurfaceNoise))
			{
				SurfaceNoise = ApplySurfaceTerraces(Editor, ScaleContext, SurfaceNoise, TerrainSettings);
				SurfaceHeight = Editor->Add(SurfaceHeight, SurfaceNoise);
			}
		}
		SurfaceHeight = ApplySurfaceAnchorTerrainEdgeBlend(Editor, ReservationPayload, ScaleContext, SurfaceZ, TerrainSettings, SurfaceHeight);

		return BuildTopFaceAlignedFlatTerrainDensity(Editor, ScaleContext, SurfaceHeight);
	}

	FNodeLink BuildClippedProviderReservationGenA(
		UFastNoiseEditor* Editor,
		const FFoundationProviderDefinition& Provider,
		const FReservationDefinition& Reservation,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		if (const FInfinitePlaneFoundationPayload* PlanePayload = GetInfinitePlaneFoundationPayload(Provider))
		{
			if (const FSurfaceAnchorReservationPayload* ReservationPayload = GetSurfaceAnchorReservationPayload(Reservation))
			{
				FNodeLink TerrainDensity = BuildInfinitePlaneSingleReservationGenA(Editor, *PlanePayload, *ReservationPayload, ScaleContext);
				FNodeLink ReservationDomain = BuildClippedProviderReservationDomain(Editor, Provider, Reservation, ScaleContext, false);
				if (!IsIslandNodeValid(TerrainDensity) || !IsIslandNodeValid(ReservationDomain))
				{
					return MakeIslandZero(Editor);
				}

				FNodeLink OutsideReservationAir = Editor->MinFloat(
					Editor->MultiplyFloat(ReservationDomain, -ReservationGenADomainGateStrength),
					ReservationGenAOutsideAirCeiling);
				return Editor->MinFloat(Editor->Max(TerrainDensity, OutsideReservationAir), ReservationGenAOutsideAirCeiling);
			}
		}

		const FIslandFoundationShapePayload* IslandPayload = GetIslandFoundationPayload(Provider);
		const FSurfaceAnchorReservationPayload* ReservationPayload = GetSurfaceAnchorReservationPayload(Reservation);
		if (IslandPayload == nullptr || ReservationPayload == nullptr)
		{
			return MakeIslandZero(Editor);
		}

		const FSurfaceAnchorReservationPayload ResolvedReservationPayload = ResolveProviderLocalReservationPayload(*IslandPayload, *ReservationPayload);
		const FResolvedReservationSurfaceZNoise SurfaceZ = SolveIslandProviderReservationSurfaceZNoise(Editor, Provider, *IslandPayload, ResolvedReservationPayload, ScaleContext);
		const FResolvedSurfaceAnchorTerrain TerrainSettings = ResolveSurfaceAnchorTerrain(ResolvedReservationPayload);
		FNodeLink SurfaceHeight = Editor->Constant(SurfaceZ.AnchorSurfaceZ);
		if (TerrainSettings.bUseSurfaceNoise)
		{
			FNodeLink SurfaceNoise = BuildIslandPlanarSimplexHeightNoise(
				Editor,
				ScaleContext,
				TerrainSettings.SurfaceNoiseAmplitude,
				TerrainSettings.SurfaceNoiseScale,
				TerrainSettings.SurfaceNoiseSeedOffset);
			if (IsIslandNodeValid(SurfaceNoise))
			{
				SurfaceNoise = ApplySurfaceTerraces(Editor, ScaleContext, SurfaceNoise, TerrainSettings);
				SurfaceHeight = Editor->Add(SurfaceHeight, SurfaceNoise);
			}
		}
		SurfaceHeight = ApplySurfaceAnchorTerrainEdgeBlend(Editor, ResolvedReservationPayload, ScaleContext, SurfaceZ, TerrainSettings, SurfaceHeight);
		FNodeLink TerrainDensity = BuildTopFaceAlignedFlatTerrainDensity(Editor, ScaleContext, SurfaceHeight);
		FNodeLink ReservationDomain = BuildClippedProviderReservationDomain(Editor, Provider, Reservation, ScaleContext, false);
		if (!IsIslandNodeValid(TerrainDensity) || !IsIslandNodeValid(ReservationDomain))
		{
			return MakeIslandZero(Editor);
		}

		// Reservation GenA is a height field, so gate it by its own domain before
		// combining same-tag reservations. Keep outside-domain air weak; Porism can
		// still evaluate rows inside DomainOver, and strong positive air there can
		// punch tiny holes into neighboring foundation terrain.
		FNodeLink OutsideReservationAir = Editor->MinFloat(
			Editor->MultiplyFloat(ReservationDomain, -ReservationGenADomainGateStrength),
			ReservationGenAOutsideAirCeiling);
		return Editor->MinFloat(Editor->Max(TerrainDensity, OutsideReservationAir), ReservationGenAOutsideAirCeiling);
	}

	FNodeLink BuildProviderGenAForBiomeTag(
		UFastNoiseEditor* Editor,
		const FFoundationProviderDefinition& Provider,
		const FGameplayTag BiomeTag,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		TArray<FNodeLink> TerrainDensities;
		if (Provider.ContributionType == EFoundationContributionType::AdditiveBiome)
		{
			if (Provider.BiomeTag == BiomeTag)
			{
				FNodeLink ProviderTerrain = BuildProviderFoundationGenA(Editor, Provider, ScaleContext);
				if (IsIslandNodeValid(ProviderTerrain))
				{
					TerrainDensities.Add(ProviderTerrain);
				}
			}

			for (const FReservationDefinition& Reservation : Provider.Reservations)
			{
				if (Reservation.BiomeTag != BiomeTag || GetSurfaceAnchorReservationPayload(Reservation) == nullptr)
				{
					continue;
				}

				FNodeLink ReservationTerrain = BuildClippedProviderReservationGenA(Editor, Provider, Reservation, ScaleContext);
				if (IsIslandNodeValid(ReservationTerrain))
				{
					TerrainDensities.Add(ReservationTerrain);
				}
			}
		}

		for (const FFoundationProviderDefinition* ChildProvider : GetChildFoundationProviders(Provider))
		{
			if (ChildProvider == nullptr)
			{
				continue;
			}

			FNodeLink ChildTerrain = BuildProviderGenAForBiomeTag(Editor, *ChildProvider, BiomeTag, ScaleContext);
			if (IsIslandNodeValid(ChildTerrain))
			{
				TerrainDensities.Add(ChildTerrain);
			}
		}

		if (TerrainDensities.IsEmpty())
		{
			return FNodeLink();
		}

		FNodeLink CombinedDensity = TerrainDensities[0];
		for (int32 Index = 1; Index < TerrainDensities.Num(); ++Index)
		{
			CombinedDensity = Editor->Min(CombinedDensity, TerrainDensities[Index]);
		}
		return CombinedDensity;
	}

	FNodeLink BuildProviderReservationGenAForBiomeTag(
		UFastNoiseEditor* Editor,
		const FFoundationProviderDefinition& Provider,
		const FGameplayTag BiomeTag,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		TArray<FNodeLink> TerrainDensities;
		if (Provider.ContributionType == EFoundationContributionType::AdditiveBiome)
		{
			for (const FReservationDefinition& Reservation : Provider.Reservations)
			{
				if (Reservation.BiomeTag != BiomeTag || GetSurfaceAnchorReservationPayload(Reservation) == nullptr)
				{
					continue;
				}

				FNodeLink ReservationTerrain = BuildClippedProviderReservationGenA(Editor, Provider, Reservation, ScaleContext);
				if (IsIslandNodeValid(ReservationTerrain))
				{
					TerrainDensities.Add(ReservationTerrain);
				}
			}
		}

		for (const FFoundationProviderDefinition* ChildProvider : GetChildFoundationProviders(Provider))
		{
			if (ChildProvider == nullptr)
			{
				continue;
			}

			FNodeLink ChildTerrain = BuildProviderReservationGenAForBiomeTag(Editor, *ChildProvider, BiomeTag, ScaleContext);
			if (IsIslandNodeValid(ChildTerrain))
			{
				TerrainDensities.Add(ChildTerrain);
			}
		}

		if (TerrainDensities.IsEmpty())
		{
			return FNodeLink();
		}

		FNodeLink CombinedDensity = TerrainDensities[0];
		for (int32 Index = 1; Index < TerrainDensities.Num(); ++Index)
		{
			CombinedDensity = Editor->Min(CombinedDensity, TerrainDensities[Index]);
		}
		return CombinedDensity;
	}

	TArray<const FResolvedFoundationProviderInstance*> GetTopLevelResolvedProviders(const FResolvedBiomeStrategy& ResolvedStrategy)
	{
		TArray<const FResolvedFoundationProviderInstance*> Providers;
		if (!ResolvedStrategy.bValid || ResolvedStrategy.ProviderInstances.IsEmpty())
		{
			return Providers;
		}

		int32 MinimumDepth = TNumericLimits<int32>::Max();
		for (const FResolvedFoundationProviderInstance& ProviderInstance : ResolvedStrategy.ProviderInstances)
		{
			MinimumDepth = FMath::Min(MinimumDepth, ProviderInstance.HierarchyDepth);
		}

		for (const FResolvedFoundationProviderInstance& ProviderInstance : ResolvedStrategy.ProviderInstances)
		{
			if (ProviderInstance.HierarchyDepth == MinimumDepth)
			{
				Providers.Add(&ProviderInstance);
			}
		}

		return Providers;
	}

	FNodeLink BuildResolvedProviderDomainForBiomeTag(
		UFastNoiseEditor* Editor,
		const FResolvedBiomeStrategy& ResolvedStrategy,
		const FGameplayTag BiomeTag)
	{
		TArray<FNodeLink> Contributions;
		for (const FResolvedFoundationProviderInstance* ProviderInstance : GetTopLevelResolvedProviders(ResolvedStrategy))
		{
			if (ProviderInstance == nullptr)
			{
				continue;
			}

			FNodeLink ProviderDomain = BuildProviderDomainForBiomeTag(
				Editor,
				ProviderInstance->ProviderSnapshot,
				BiomeTag,
				ResolvedStrategy.Context.ScaleContext);
			if (IsIslandNodeValid(ProviderDomain))
			{
				Contributions.Add(ProviderDomain);
			}
		}

		return Contributions.IsEmpty()
			? FNodeLink()
			: UReservationFastNoiseLibrary::UnionReservations(Editor, Contributions);
	}

	FNodeLink BuildResolvedProviderGenAForBiomeTag(
		UFastNoiseEditor* Editor,
		const FResolvedBiomeStrategy& ResolvedStrategy,
		const FGameplayTag BiomeTag)
	{
		TArray<FNodeLink> TerrainDensities;
		for (const FResolvedFoundationProviderInstance* ProviderInstance : GetTopLevelResolvedProviders(ResolvedStrategy))
		{
			if (ProviderInstance == nullptr)
			{
				continue;
			}

			FNodeLink ProviderTerrain = BuildProviderGenAForBiomeTag(
				Editor,
				ProviderInstance->ProviderSnapshot,
				BiomeTag,
				ResolvedStrategy.Context.ScaleContext);
			if (IsIslandNodeValid(ProviderTerrain))
			{
				TerrainDensities.Add(ProviderTerrain);
			}
		}

		if (TerrainDensities.IsEmpty())
		{
			return FNodeLink();
		}

		FNodeLink CombinedDensity = TerrainDensities[0];
		for (int32 Index = 1; Index < TerrainDensities.Num(); ++Index)
		{
			CombinedDensity = Editor->Min(CombinedDensity, TerrainDensities[Index]);
		}
		return CombinedDensity;
	}
}

FNodeLink UIslandBiomeFastNoiseLibrary::BuildIslandEnvelopeDomain(
	UFastNoiseEditor* Editor,
	const UBiomeStrategyData* Strategy)
{
	const FResolvedWorldGenScaleContext ScaleContext = ResolveFallbackScaleContext(Strategy);
	return BuildIslandEnvelopeDomain(Editor, Strategy, &ScaleContext);
}

FNodeLink UIslandBiomeFastNoiseLibrary::BuildIslandEnvelopeDomain(
	UFastNoiseEditor* Editor,
	const UBiomeStrategyData* Strategy,
	const FResolvedWorldGenScaleContext* ScaleContext)
{
	const FResolvedWorldGenScaleContext LocalScaleContext = ScaleContext != nullptr ? *ScaleContext : ResolveFallbackScaleContext(Strategy);
	return BuildIslandEnvelopeDomainInternal(Editor, Strategy, LocalScaleContext, 0.0f);
}

FNodeLink UIslandBiomeFastNoiseLibrary::BuildReservationDomain(
	UFastNoiseEditor* Editor,
	const UBiomeStrategyData* Strategy,
	const FGameplayTag BiomeTag)
{
	const FResolvedWorldGenScaleContext ScaleContext = ResolveFallbackScaleContext(Strategy);
	return BuildReservationDomain(Editor, Strategy, BiomeTag, &ScaleContext);
}

FNodeLink UIslandBiomeFastNoiseLibrary::BuildReservationDomain(
	UFastNoiseEditor* Editor,
	const UBiomeStrategyData* Strategy,
	const FGameplayTag BiomeTag,
	const FResolvedWorldGenScaleContext* ScaleContext)
{
	if (Editor == nullptr || Strategy == nullptr)
	{
		return MakeIslandZero(Editor);
	}

	const FResolvedWorldGenScaleContext LocalScaleContext = ScaleContext != nullptr ? *ScaleContext : ResolveFallbackScaleContext(Strategy);
	FNodeLink Domain = BuildProviderReservationDomainForBiomeTag(Editor, Strategy->RootFoundationProvider, BiomeTag, LocalScaleContext);
	return IsIslandNodeValid(Domain) ? Domain : MakeIslandZero(Editor);
}

FNodeLink UIslandBiomeFastNoiseLibrary::BuildBiomeDomain(
	UFastNoiseEditor* Editor,
	const UBiomeStrategyData* Strategy,
	const FGameplayTag BiomeTag,
	const FResolvedWorldGenScaleContext* ScaleContext)
{
	if (Editor == nullptr || Strategy == nullptr || !BiomeTag.IsValid())
	{
		return MakeIslandZero(Editor);
	}

	const FResolvedWorldGenScaleContext LocalScaleContext = ScaleContext != nullptr ? *ScaleContext : ResolveFallbackScaleContext(Strategy);
	FNodeLink Domain = BuildProviderDomainForBiomeTag(Editor, Strategy->RootFoundationProvider, BiomeTag, LocalScaleContext);
	return IsIslandNodeValid(Domain) ? Domain : MakeIslandZero(Editor);
}

FNodeLink UIslandBiomeFastNoiseLibrary::BuildResolvedBiomeDomain(
	UFastNoiseEditor* Editor,
	const FResolvedBiomeStrategy& ResolvedStrategy,
	const FGameplayTag BiomeTag)
{
	if (Editor == nullptr || !ResolvedStrategy.bValid || !BiomeTag.IsValid())
	{
		return MakeIslandZero(Editor);
	}

	FNodeLink Domain = BuildResolvedProviderDomainForBiomeTag(Editor, ResolvedStrategy, BiomeTag);
	return IsIslandNodeValid(Domain) ? Domain : MakeIslandZero(Editor);
}

FNodeLink UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(
	UFastNoiseEditor* Editor,
	const UBiomeStrategyData* Strategy)
{
	const FResolvedWorldGenScaleContext ScaleContext = ResolveFallbackScaleContext(Strategy);
	return BuildFoundationDomain(Editor, Strategy, &ScaleContext);
}

FNodeLink UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(
	UFastNoiseEditor* Editor,
	const UBiomeStrategyData* Strategy,
	const FResolvedWorldGenScaleContext* ScaleContext)
{
	if (Editor == nullptr || Strategy == nullptr)
	{
		return MakeIslandZero(Editor);
	}

	const FResolvedWorldGenScaleContext LocalScaleContext = ScaleContext != nullptr ? *ScaleContext : ResolveFallbackScaleContext(Strategy);
	FNodeLink Domain = BuildProviderDomainForBiomeTag(Editor, Strategy->RootFoundationProvider, Strategy->RootFoundationProvider.BiomeTag, LocalScaleContext);
	return IsIslandNodeValid(Domain) ? Domain : MakeIslandZero(Editor);
}

FNodeLink UIslandBiomeFastNoiseLibrary::BuildSpawnDomain(
	UFastNoiseEditor* Editor,
	const UBiomeStrategyData* Strategy,
	const FGameplayTag BiomeTag)
{
	const FResolvedWorldGenScaleContext ScaleContext = ResolveFallbackScaleContext(Strategy);
	return BuildSpawnDomain(Editor, Strategy, BiomeTag, &ScaleContext);
}

FNodeLink UIslandBiomeFastNoiseLibrary::BuildSpawnDomain(
	UFastNoiseEditor* Editor,
	const UBiomeStrategyData* Strategy,
	const FGameplayTag BiomeTag,
	const FResolvedWorldGenScaleContext* ScaleContext)
{
	if (Editor == nullptr || Strategy == nullptr)
	{
		return MakeIslandZero(Editor);
	}

	const FReservationDefinition* Reservation = Strategy->FindFirstReservationForBiomeTag(BiomeTag);
	if (Reservation == nullptr || !Reservation->bEnableSpawnReservation)
	{
		return MakeIslandZero(Editor);
	}

	const FResolvedWorldGenScaleContext LocalScaleContext = ScaleContext != nullptr ? *ScaleContext : ResolveFallbackScaleContext(Strategy);
	const FSurfaceAnchorReservationPayload* ReservationPayload = GetSurfaceAnchorReservationPayload(*Reservation);
	if (ReservationPayload == nullptr)
	{
		return MakeIslandZero(Editor);
	}

	FNodeLink SpawnDomain = BuildSingleReservationDomain(Editor, Strategy, *Reservation, LocalScaleContext, true);
	FNodeLink ReservationDomain = BuildReservationDomain(Editor, Strategy, BiomeTag, &LocalScaleContext);
	return IsIslandNodeValid(SpawnDomain) && IsIslandNodeValid(ReservationDomain)
		? Editor->Min(SpawnDomain, ReservationDomain)
		: MakeIslandZero(Editor);
}

FNodeLink UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(
	UFastNoiseEditor* Editor,
	const UBiomeStrategyData* Strategy)
{
	const FResolvedWorldGenScaleContext ScaleContext = ResolveFallbackScaleContext(Strategy);
	return BuildFoundationGenA(Editor, Strategy, &ScaleContext);
}

FNodeLink UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(
	UFastNoiseEditor* Editor,
	const UBiomeStrategyData* Strategy,
	const FResolvedWorldGenScaleContext* ScaleContext)
{
	if (Editor == nullptr || Strategy == nullptr)
	{
		return MakeIslandZero(Editor);
	}

	const FResolvedWorldGenScaleContext LocalScaleContext = ScaleContext != nullptr ? *ScaleContext : ResolveFallbackScaleContext(Strategy);
	FNodeLink Terrain = BuildProviderGenAForBiomeTag(Editor, Strategy->RootFoundationProvider, Strategy->RootFoundationProvider.BiomeTag, LocalScaleContext);
	return IsIslandNodeValid(Terrain) ? Terrain : MakeIslandZero(Editor);
}

FNodeLink UIslandBiomeFastNoiseLibrary::BuildBiomeGenA(
	UFastNoiseEditor* Editor,
	const UBiomeStrategyData* Strategy,
	const FGameplayTag BiomeTag,
	const FResolvedWorldGenScaleContext* ScaleContext)
{
	if (Editor == nullptr || Strategy == nullptr || !BiomeTag.IsValid())
	{
		return MakeIslandZero(Editor);
	}

	const FResolvedWorldGenScaleContext LocalScaleContext = ScaleContext != nullptr ? *ScaleContext : ResolveFallbackScaleContext(Strategy);
	FNodeLink Terrain = BuildProviderGenAForBiomeTag(Editor, Strategy->RootFoundationProvider, BiomeTag, LocalScaleContext);
	return IsIslandNodeValid(Terrain) ? Terrain : MakeIslandZero(Editor);
}

FNodeLink UIslandBiomeFastNoiseLibrary::BuildResolvedBiomeGenA(
	UFastNoiseEditor* Editor,
	const FResolvedBiomeStrategy& ResolvedStrategy,
	const FGameplayTag BiomeTag)
{
	if (Editor == nullptr || !ResolvedStrategy.bValid || !BiomeTag.IsValid())
	{
		return MakeIslandZero(Editor);
	}

	FNodeLink Terrain = BuildResolvedProviderGenAForBiomeTag(Editor, ResolvedStrategy, BiomeTag);
	return IsIslandNodeValid(Terrain) ? Terrain : MakeIslandZero(Editor);
}

FNodeLink UIslandBiomeFastNoiseLibrary::BuildReservationGenA(
	UFastNoiseEditor* Editor,
	const UBiomeStrategyData* Strategy,
	const FGameplayTag BiomeTag)
{
	const FResolvedWorldGenScaleContext ScaleContext = ResolveFallbackScaleContext(Strategy);
	return BuildReservationGenA(Editor, Strategy, BiomeTag, &ScaleContext);
}

FNodeLink UIslandBiomeFastNoiseLibrary::BuildReservationGenA(
	UFastNoiseEditor* Editor,
	const UBiomeStrategyData* Strategy,
	const FGameplayTag BiomeTag,
	const FResolvedWorldGenScaleContext* ScaleContext)
{
	if (Editor == nullptr || Strategy == nullptr)
	{
		return MakeIslandZero(Editor);
	}

	const FResolvedWorldGenScaleContext LocalScaleContext = ScaleContext != nullptr ? *ScaleContext : ResolveFallbackScaleContext(Strategy);
	FNodeLink Terrain = BuildProviderReservationGenAForBiomeTag(Editor, Strategy->RootFoundationProvider, BiomeTag, LocalScaleContext);
	return IsIslandNodeValid(Terrain) ? Terrain : MakeIslandZero(Editor);
}

bool UIslandBiomeFastNoiseLibrary::QueryIslandFoundationSurface(
	const UBiomeStrategyData* Strategy,
	const FFoundationSurfaceQuery& Query,
	const FResolvedWorldGenScaleContext& ScaleContext,
	FResolvedFoundationSurface& OutSurface)
{
	const FIslandFoundationShapePayload* IslandPayload = GetIslandFoundationPayload(Strategy);
	if (IslandPayload == nullptr)
	{
		OutSurface = FResolvedFoundationSurface();
		return false;
	}

	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = NewObject<UFastNoiseEditor>(GetTransientPackage());
	Editor->Nodes = &Nodes;
	if (!QueryIslandProviderSurfaceInternal(Editor, Strategy->RootFoundationProvider, *IslandPayload, Query, ScaleContext, OutSurface))
	{
		return false;
	}

	OutSurface.DebugPath = FString::Printf(TEXT("%s.Foundation[Island].Surface"), *Strategy->GetPathName());
	return true;
}

bool UIslandBiomeFastNoiseLibrary::QueryIslandProviderSurface(
	const FFoundationProviderDefinition& Provider,
	const FString& DebugPath,
	const FFoundationSurfaceQuery& Query,
	const FResolvedWorldGenScaleContext& ScaleContext,
	FResolvedFoundationSurface& OutSurface)
{
	const FIslandFoundationShapePayload* IslandPayload = GetIslandFoundationPayload(Provider);
	if (IslandPayload == nullptr)
	{
		OutSurface = FResolvedFoundationSurface();
		return false;
	}

	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = NewObject<UFastNoiseEditor>(GetTransientPackage());
	Editor->Nodes = &Nodes;
	if (!QueryIslandProviderSurfaceInternal(Editor, Provider, *IslandPayload, Query, ScaleContext, OutSurface))
	{
		return false;
	}

	OutSurface.DebugPath = FString::Printf(TEXT("%s.Surface"), *DebugPath);
	return true;
}
