// Copyright 2026 Spotted Loaf Studio

#include "Biome/Noise/Foundation/FoundationTerrainProfileOps.h"

#include "Biome/Noise/Foundation/FoundationTerrainProfilePayloads.h"
#include "Biome/Noise/Island/IslandBiomeStrategyPayloads.h"
#include "Biome/Noise/Strategy/BiomeStrategyData.h"
#include "Biome/Noise/WorldGenScaleContext.h"

namespace
{
	enum class EFoundationTerrainProfileProviderSupport : uint8
	{
		None = 0,
		FiniteSurface = 1 << 0,
		InfiniteSurface = 1 << 1,
		AnySurface = FiniteSurface | InfiniteSurface
	};

	ENUM_CLASS_FLAGS(EFoundationTerrainProfileProviderSupport);

	using FBuildSurfaceOffsetFn = FNodeLink(*)(const void* Payload, const FFoundationTerrainProfileBuildContext& Context);
	using FProfileFloatFn = float(*)(const void* Payload);
	using FScaleProfileFn = void(*)(void* Payload, float UniformScale);
	using FValidateProfileFn = void(*)(const void* Payload, const FFoundationProviderDefinition& Provider, const FString& Path, TArray<FFoundationTerrainProfileValidationIssue>& OutIssues);

	struct FFoundationTerrainProfileOps
	{
		const UScriptStruct* PayloadStruct = nullptr;
		const TCHAR* DisplayName = TEXT("");
		EFoundationTerrainProfileProviderSupport ProviderSupport = EFoundationTerrainProfileProviderSupport::None;
		FBuildSurfaceOffsetFn BuildSurfaceOffset = nullptr;
		FProfileFloatFn GetMaxPositiveHeightBlocks = nullptr;
		FProfileFloatFn GetSurfaceSearchAmplitudeBlocks = nullptr;
		FScaleProfileFn ApplyUniformScale = nullptr;
		FValidateProfileFn AppendValidationIssues = nullptr;
	};

	FNodeLink MakeTerrainProfileZero(UFastNoiseEditor* Editor)
	{
		return Editor != nullptr ? Editor->Constant(0.0f) : FNodeLink();
	}

	bool IsTerrainProfileNodeValid(const FNodeLink Link)
	{
		return Link.Node != nullptr;
	}

	void AddIssue(
		TArray<FFoundationTerrainProfileValidationIssue>& OutIssues,
		const FString& Path,
		const TCHAR* Message,
		const TCHAR* Fix)
	{
		FFoundationTerrainProfileValidationIssue Issue;
		Issue.Path = Path;
		Issue.Message = Message;
		Issue.Fix = Fix;
		OutIssues.Add(MoveTemp(Issue));
	}

	FNodeLink BuildTerrainProfilePlanarDistanceSquaredXY(UFastNoiseEditor* Editor, const FVector2D CenterXY)
	{
		FNodeLink Distance = Editor->DistanceToPoint(
			FastNoiseDistanceFunction::EuclideanSquared,
			FVector(CenterXY.X, CenterXY.Y, 0.0));
		return Editor->RemoveDimension(Distance, FastNoiseDim::Z);
	}

	FVector2D ConvertTerrainProfileAuthoredCenterXYToNoise(
		const FVector2D AuthoredCenterXY,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		const FVector NoiseCenter = ScaleContext.AuthoredBlockPositionToNoise(FVector(AuthoredCenterXY.X, AuthoredCenterXY.Y, 0.0));
		return FVector2D(NoiseCenter.X, NoiseCenter.Y);
	}

	float ConvertTerrainProfileBlockZDistanceToNoise(
		const float BlockDistance,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		return static_cast<float>(FMath::Abs(ScaleContext.BlockDistanceToNoise(FVector(0.0, 0.0, BlockDistance)).Z));
	}

	FNodeLink BuildPlanarSimplexHeightNoise(
		UFastNoiseEditor* Editor,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const float AmplitudeBlocks,
		const float DomainScale,
		const int32 SeedOffset)
	{
		if (Editor == nullptr || AmplitudeBlocks <= 0.0f || DomainScale <= 0.0f)
		{
			return MakeTerrainProfileZero(Editor);
		}

		FNodeLink Source = Editor->OpenSimplex2();
		FNodeLink SeededSource = Editor->SeedOffset(Source, SeedOffset);
		FNodeLink PlanarSource = Editor->DomainAxisScale(SeededSource, FVector(DomainScale, DomainScale, 0.0));
		FNodeLink Gain = Editor->Constant(0.5f);
		FNodeLink WeightedStrength = Editor->Constant(0.25f);
		FNodeLink Fractal = Editor->FractalFBm(PlanarSource, Gain, WeightedStrength, 4, 2.0f);
		return Editor->MultiplyFloat(Fractal, ConvertTerrainProfileBlockZDistanceToNoise(AmplitudeBlocks, ScaleContext));
	}

	float ResolveTerrainProfileProviderRadiusBlocks(const FFoundationProviderDefinition& Provider, const float RadiusOverrideBlocks)
	{
		if (RadiusOverrideBlocks > 0.0f)
		{
			return RadiusOverrideBlocks;
		}

		if (const FIslandFoundationShapePayload* IslandPayload = Provider.ProviderPayload.GetPtr<FIslandFoundationShapePayload>())
		{
			return IslandPayload->IslandBody.TopRadius;
		}

		return 0.0f;
	}

	FNodeLink BuildFlatSurfaceOffset(const void*, const FFoundationTerrainProfileBuildContext&)
	{
		return FNodeLink();
	}

	float GetZeroProfileHeight(const void*)
	{
		return 0.0f;
	}

	float GetNoisyMaxPositiveHeight(const void* Payload)
	{
		const FNoisyFoundationTerrainProfilePayload& Profile = *static_cast<const FNoisyFoundationTerrainProfilePayload*>(Payload);
		return FMath::Max(0.0f, Profile.SurfaceNoiseAmplitude);
	}

	float GetNoisySearchAmplitude(const void* Payload)
	{
		const FNoisyFoundationTerrainProfilePayload& Profile = *static_cast<const FNoisyFoundationTerrainProfilePayload*>(Payload);
		return FMath::Abs(Profile.SurfaceNoiseAmplitude);
	}

	FNodeLink BuildNoisySurfaceOffset(const void* Payload, const FFoundationTerrainProfileBuildContext& Context)
	{
		const FNoisyFoundationTerrainProfilePayload& Profile = *static_cast<const FNoisyFoundationTerrainProfilePayload*>(Payload);
		if (Context.Editor == nullptr || Context.ScaleContext == nullptr)
		{
			return FNodeLink();
		}

		return BuildPlanarSimplexHeightNoise(
			Context.Editor,
			*Context.ScaleContext,
			Profile.SurfaceNoiseAmplitude,
			Profile.SurfaceNoiseScale,
			Profile.SeedOffset + 11);
	}

	void ScaleNoisyProfile(void* Payload, const float UniformScale)
	{
		FNoisyFoundationTerrainProfilePayload& Profile = *static_cast<FNoisyFoundationTerrainProfilePayload*>(Payload);
		Profile.SurfaceNoiseAmplitude *= UniformScale;
	}

	void ValidateNoisyProfile(const void* Payload, const FFoundationProviderDefinition&, const FString& Path, TArray<FFoundationTerrainProfileValidationIssue>& OutIssues)
	{
		const FNoisyFoundationTerrainProfilePayload& Profile = *static_cast<const FNoisyFoundationTerrainProfilePayload*>(Payload);
		if (Profile.SurfaceNoiseAmplitude < 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Noisy terrain profile has a negative amplitude."), TEXT("Set the surface amplitude to zero or a positive authored-block value."));
		}
	}

	float GetRollingMaxPositiveHeight(const void* Payload)
	{
		const FRollingHillsFoundationTerrainProfilePayload& Profile = *static_cast<const FRollingHillsFoundationTerrainProfilePayload*>(Payload);
		return FMath::Max(0.0f, Profile.HillHeightBlocks + Profile.DetailAmplitude);
	}

	float GetRollingSearchAmplitude(const void* Payload)
	{
		const FRollingHillsFoundationTerrainProfilePayload& Profile = *static_cast<const FRollingHillsFoundationTerrainProfilePayload*>(Payload);
		return FMath::Abs(Profile.HillHeightBlocks) + FMath::Abs(Profile.ValleyDepthBlocks) + FMath::Abs(Profile.DetailAmplitude);
	}

	FNodeLink BuildRollingSurfaceOffset(const void* Payload, const FFoundationTerrainProfileBuildContext& Context)
	{
		const FRollingHillsFoundationTerrainProfilePayload& Profile = *static_cast<const FRollingHillsFoundationTerrainProfilePayload*>(Payload);
		if (Context.Editor == nullptr || Context.ScaleContext == nullptr || Profile.HillScale <= 0.0f || (Profile.HillHeightBlocks <= 0.0f && Profile.ValleyDepthBlocks <= 0.0f))
		{
			return FNodeLink();
		}

		FNodeLink Source = Context.Editor->OpenSimplex2();
		FNodeLink SeededSource = Context.Editor->SeedOffset(Source, Profile.SeedOffset);
		FNodeLink PlanarSource = Context.Editor->DomainAxisScale(SeededSource, FVector(Profile.HillScale, Profile.HillScale, 0.0));
		FNodeLink Gain = Context.Editor->Constant(0.45f);
		FNodeLink WeightedStrength = Context.Editor->Constant(0.2f);
		FNodeLink Fractal = Context.Editor->FractalFBm(PlanarSource, Gain, WeightedStrength, 4, 2.0f);
		const float HillNoise = ConvertTerrainProfileBlockZDistanceToNoise(FMath::Max(0.0f, Profile.HillHeightBlocks), *Context.ScaleContext);
		const float ValleyNoise = ConvertTerrainProfileBlockZDistanceToNoise(FMath::Max(0.0f, Profile.ValleyDepthBlocks), *Context.ScaleContext);
		FNodeLink Offset = Context.Editor->Remap(Fractal, -0.45f, 0.45f, -ValleyNoise, HillNoise);
		Offset = Context.Editor->MaxFloat(Context.Editor->MinFloat(Offset, HillNoise), -ValleyNoise);

		if (Profile.DetailAmplitude > 0.0f && Profile.DetailScale > 0.0f)
		{
			FNodeLink Detail = BuildPlanarSimplexHeightNoise(Context.Editor, *Context.ScaleContext, Profile.DetailAmplitude, Profile.DetailScale, Profile.SeedOffset + 17);
			if (IsTerrainProfileNodeValid(Detail))
			{
				Offset = Context.Editor->Add(Offset, Detail);
			}
		}

		return Offset;
	}

	void ScaleRollingProfile(void* Payload, const float UniformScale)
	{
		FRollingHillsFoundationTerrainProfilePayload& Profile = *static_cast<FRollingHillsFoundationTerrainProfilePayload*>(Payload);
		Profile.HillHeightBlocks *= UniformScale;
		Profile.ValleyDepthBlocks *= UniformScale;
		Profile.DetailAmplitude *= UniformScale;
	}

	void ValidateRollingProfile(const void* Payload, const FFoundationProviderDefinition&, const FString& Path, TArray<FFoundationTerrainProfileValidationIssue>& OutIssues)
	{
		const FRollingHillsFoundationTerrainProfilePayload& Profile = *static_cast<const FRollingHillsFoundationTerrainProfilePayload*>(Payload);
		if (Profile.HillHeightBlocks < 0.0f || Profile.ValleyDepthBlocks < 0.0f || Profile.DetailAmplitude < 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Rolling Hills terrain profile has negative height settings."), TEXT("Set Hill Height Blocks, Valley Depth Blocks, and Detail Amplitude to zero or positive authored-block values."));
		}
		if (Profile.HillScale < 0.0f || Profile.DetailScale < 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Rolling Hills terrain profile has negative scale settings."), TEXT("Set Hill Scale and Detail Scale to zero or positive values."));
		}
	}

	float GetErodedMaxPositiveHeight(const void* Payload)
	{
		const FErodedEdgeFoundationTerrainProfilePayload& Profile = *static_cast<const FErodedEdgeFoundationTerrainProfilePayload*>(Payload);
		return FMath::Max(0.0f, Profile.EdgeNoiseAmplitude);
	}

	float GetErodedSearchAmplitude(const void* Payload)
	{
		const FErodedEdgeFoundationTerrainProfilePayload& Profile = *static_cast<const FErodedEdgeFoundationTerrainProfilePayload*>(Payload);
		return FMath::Abs(Profile.EdgeDepthBlocks) + FMath::Abs(Profile.EdgeNoiseAmplitude);
	}

	FNodeLink BuildErodedSurfaceOffset(const void* Payload, const FFoundationTerrainProfileBuildContext& Context)
	{
		const FErodedEdgeFoundationTerrainProfilePayload& Profile = *static_cast<const FErodedEdgeFoundationTerrainProfilePayload*>(Payload);
		if (Context.Editor == nullptr || Context.Provider == nullptr || Context.ScaleContext == nullptr)
		{
			return FNodeLink();
		}

		const float RadiusBlocks = ResolveTerrainProfileProviderRadiusBlocks(*Context.Provider, Profile.RadiusOverrideBlocks);
		if (RadiusBlocks <= 0.0f || Profile.EdgeWidthBlocks <= 0.0f)
		{
			return FNodeLink();
		}

		const FVector2D CenterXYNoise = ConvertTerrainProfileAuthoredCenterXYToNoise(Context.ProviderOriginXYBlocks + Profile.CenterXY, *Context.ScaleContext);
		const float RadiusNoise = FMath::Max(
			UE_KINDA_SMALL_NUMBER,
			static_cast<float>(FMath::Abs(Context.ScaleContext->BlockDistanceToNoise(FVector(RadiusBlocks, 0.0, 0.0)).X)));
		const float EdgeWidthNoise = FMath::Max(
			UE_KINDA_SMALL_NUMBER,
			static_cast<float>(FMath::Abs(Context.ScaleContext->BlockDistanceToNoise(FVector(Profile.EdgeWidthBlocks, 0.0, 0.0)).X)));
		const float InnerRadiusNoise = FMath::Max(0.0f, RadiusNoise - EdgeWidthNoise);
		const float Denominator = FMath::Max(UE_KINDA_SMALL_NUMBER, (RadiusNoise * RadiusNoise) - (InnerRadiusNoise * InnerRadiusNoise));
		FNodeLink DistanceSquared = BuildTerrainProfilePlanarDistanceSquaredXY(Context.Editor, CenterXYNoise);
		FNodeLink EdgeMask = Context.Editor->MaxFloat(
			Context.Editor->MinFloat(
				Context.Editor->DivideFloat(Context.Editor->SubtractFloat(DistanceSquared, InnerRadiusNoise * InnerRadiusNoise), Denominator),
				1.0f),
			0.0f);
		FNodeLink Offset = Context.Editor->MultiplyFloat(
			EdgeMask,
			-ConvertTerrainProfileBlockZDistanceToNoise(FMath::Max(0.0f, Profile.EdgeDepthBlocks), *Context.ScaleContext));

		if (Profile.EdgeNoiseAmplitude > 0.0f && Profile.EdgeNoiseScale > 0.0f)
		{
			FNodeLink EdgeDetail = BuildPlanarSimplexHeightNoise(Context.Editor, *Context.ScaleContext, Profile.EdgeNoiseAmplitude, Profile.EdgeNoiseScale, Profile.SeedOffset);
			if (IsTerrainProfileNodeValid(EdgeDetail))
			{
				Offset = Context.Editor->Add(Offset, Context.Editor->Multiply(EdgeDetail, EdgeMask));
			}
		}

		return Offset;
	}

	void ScaleErodedProfile(void* Payload, const float UniformScale)
	{
		FErodedEdgeFoundationTerrainProfilePayload& Profile = *static_cast<FErodedEdgeFoundationTerrainProfilePayload*>(Payload);
		Profile.CenterXY *= UniformScale;
		Profile.RadiusOverrideBlocks *= UniformScale;
		Profile.EdgeWidthBlocks *= UniformScale;
		Profile.EdgeDepthBlocks *= UniformScale;
		Profile.EdgeNoiseAmplitude *= UniformScale;
	}

	void ValidateErodedProfile(const void* Payload, const FFoundationProviderDefinition& Provider, const FString& Path, TArray<FFoundationTerrainProfileValidationIssue>& OutIssues)
	{
		const FErodedEdgeFoundationTerrainProfilePayload& Profile = *static_cast<const FErodedEdgeFoundationTerrainProfilePayload*>(Payload);
		if (Profile.RadiusOverrideBlocks < 0.0f || Profile.EdgeWidthBlocks <= 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Eroded Edge terrain profile has invalid radius settings."), TEXT("Use zero or positive Radius Override Blocks and set Edge Width Blocks above zero."));
		}
		if (Provider.ProviderType != EFoundationProviderType::Island && Profile.RadiusOverrideBlocks <= 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Eroded Edge terrain profile needs a radius source."), TEXT("Set Radius Override Blocks when the provider is not an island."));
		}
		if (Profile.EdgeDepthBlocks < 0.0f || Profile.EdgeNoiseAmplitude < 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Eroded Edge terrain profile has negative height settings."), TEXT("Set Edge Depth Blocks and Edge Noise Amplitude to zero or positive authored-block values."));
		}
		if (Profile.EdgeNoiseScale < 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Eroded Edge terrain profile has a negative noise scale."), TEXT("Set Edge Noise Scale to zero or a positive value."));
		}
	}

	float GetCreviceMaxPositiveHeight(const void* Payload)
	{
		const FCreviceFoundationTerrainProfilePayload& Profile = *static_cast<const FCreviceFoundationTerrainProfilePayload*>(Payload);
		return FMath::Max(0.0f, Profile.SurfaceNoiseAmplitude);
	}

	float GetCreviceSearchAmplitude(const void* Payload)
	{
		const FCreviceFoundationTerrainProfilePayload& Profile = *static_cast<const FCreviceFoundationTerrainProfilePayload*>(Payload);
		return FMath::Abs(Profile.CreviceDepthBlocks) + FMath::Abs(Profile.SurfaceNoiseAmplitude);
	}

	FNodeLink BuildCreviceSurfaceOffset(const void* Payload, const FFoundationTerrainProfileBuildContext& Context)
	{
		const FCreviceFoundationTerrainProfilePayload& Profile = *static_cast<const FCreviceFoundationTerrainProfilePayload*>(Payload);
		if (Context.Editor == nullptr || Context.ScaleContext == nullptr || Profile.CreviceDepthBlocks <= 0.0f || Profile.CreviceScale <= 0.0f)
		{
			return FNodeLink();
		}

		FNodeLink Source = Context.Editor->OpenSimplex2();
		FNodeLink SeededSource = Context.Editor->SeedOffset(Source, Profile.SeedOffset);
		FNodeLink PlanarSource = Context.Editor->DomainAxisScale(SeededSource, FVector(Profile.CreviceScale, Profile.CreviceScale, 0.0));
		FNodeLink Gain = Context.Editor->Constant(0.5f);
		FNodeLink WeightedStrength = Context.Editor->Constant(0.3f);
		FNodeLink Ridged = Context.Editor->FractalRidged(PlanarSource, Gain, WeightedStrength, 4, 2.0f);
		FNodeLink CreviceMask = Context.Editor->Remap(Ridged, 0.15f, 0.45f, 0.0f, 1.0f);
		CreviceMask = Context.Editor->MaxFloat(Context.Editor->MinFloat(CreviceMask, 1.0f), 0.0f);
		CreviceMask = Context.Editor->PowFloatFloat(CreviceMask, FMath::Max(0.1f, Profile.CreviceSharpness));
		FNodeLink Offset = Context.Editor->MultiplyFloat(
			CreviceMask,
			-ConvertTerrainProfileBlockZDistanceToNoise(FMath::Max(0.0f, Profile.CreviceDepthBlocks), *Context.ScaleContext));

		if (Profile.SurfaceNoiseAmplitude > 0.0f && Profile.SurfaceNoiseScale > 0.0f)
		{
			FNodeLink SurfaceNoise = BuildPlanarSimplexHeightNoise(Context.Editor, *Context.ScaleContext, Profile.SurfaceNoiseAmplitude, Profile.SurfaceNoiseScale, Profile.SeedOffset + 13);
			if (IsTerrainProfileNodeValid(SurfaceNoise))
			{
				Offset = Context.Editor->Add(Offset, SurfaceNoise);
			}
		}

		return Offset;
	}

	void ScaleCreviceProfile(void* Payload, const float UniformScale)
	{
		FCreviceFoundationTerrainProfilePayload& Profile = *static_cast<FCreviceFoundationTerrainProfilePayload*>(Payload);
		Profile.CreviceDepthBlocks *= UniformScale;
		Profile.SurfaceNoiseAmplitude *= UniformScale;
	}

	void ValidateCreviceProfile(const void* Payload, const FFoundationProviderDefinition&, const FString& Path, TArray<FFoundationTerrainProfileValidationIssue>& OutIssues)
	{
		const FCreviceFoundationTerrainProfilePayload& Profile = *static_cast<const FCreviceFoundationTerrainProfilePayload*>(Payload);
		if (Profile.CreviceDepthBlocks < 0.0f || Profile.SurfaceNoiseAmplitude < 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Crevice terrain profile has negative height settings."), TEXT("Set Crevice Depth Blocks and Surface Noise Amplitude to zero or positive authored-block values."));
		}
		if (Profile.CreviceScale < 0.0f || Profile.SurfaceNoiseScale < 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Crevice terrain profile has negative scale settings."), TEXT("Set Crevice Scale and Surface Noise Scale to zero or positive values."));
		}
		if (Profile.CreviceSharpness < 0.1f)
		{
			AddIssue(OutIssues, Path, TEXT("Crevice terrain profile has too little sharpness."), TEXT("Set Crevice Sharpness to at least 0.1."));
		}
	}

	float GetBowlMaxPositiveHeight(const void* Payload)
	{
		const FBowlFoundationTerrainProfilePayload& Profile = *static_cast<const FBowlFoundationTerrainProfilePayload*>(Payload);
		const float RimHeight = Profile.bEnableRaisedRim ? FMath::Max(0.0f, Profile.RimHeightBlocks) : 0.0f;
		return FMath::Max(RimHeight, FMath::Max(0.0f, Profile.SurfaceNoiseAmplitude));
	}

	float GetBowlSearchAmplitude(const void* Payload)
	{
		const FBowlFoundationTerrainProfilePayload& Profile = *static_cast<const FBowlFoundationTerrainProfilePayload*>(Payload);
		const float RimHeight = Profile.bEnableRaisedRim ? FMath::Abs(Profile.RimHeightBlocks) : 0.0f;
		return FMath::Abs(Profile.DepthBlocks) + FMath::Abs(Profile.SurfaceNoiseAmplitude) + RimHeight;
	}

	FNodeLink BuildBowlSurfaceOffset(const void* Payload, const FFoundationTerrainProfileBuildContext& Context)
	{
		const FBowlFoundationTerrainProfilePayload& Profile = *static_cast<const FBowlFoundationTerrainProfilePayload*>(Payload);
		if (Context.Editor == nullptr || Context.ScaleContext == nullptr || Profile.RadiusBlocks <= 0.0f)
		{
			return FNodeLink();
		}

		const FVector2D CenterXYNoise = ConvertTerrainProfileAuthoredCenterXYToNoise(Context.ProviderOriginXYBlocks + Profile.CenterXY, *Context.ScaleContext);
		const float RadiusNoise = FMath::Max(
			UE_KINDA_SMALL_NUMBER,
			static_cast<float>(FMath::Abs(Context.ScaleContext->BlockDistanceToNoise(FVector(Profile.RadiusBlocks, 0.0, 0.0)).X)));
		FNodeLink DistanceSquared = BuildTerrainProfilePlanarDistanceSquaredXY(Context.Editor, CenterXYNoise);
		FNodeLink NormalizedDistance = Context.Editor->MinFloat(Context.Editor->DivideFloat(DistanceSquared, RadiusNoise * RadiusNoise), 1.0f);
		FNodeLink CenterWeight = Context.Editor->Subtract(Context.Editor->Constant(1.0f), NormalizedDistance);
		FNodeLink Offset = Context.Editor->MultiplyFloat(CenterWeight, -ConvertTerrainProfileBlockZDistanceToNoise(FMath::Max(0.0f, Profile.DepthBlocks), *Context.ScaleContext));

		if (Profile.bEnableRaisedRim && Profile.RimWidthBlocks > 0.0f && Profile.RimHeightBlocks > 0.0f)
		{
			const float RimHalfWidthNoise = FMath::Max(
				UE_KINDA_SMALL_NUMBER,
				static_cast<float>(FMath::Abs(Context.ScaleContext->BlockDistanceToNoise(FVector(Profile.RimWidthBlocks * 0.5f, 0.0, 0.0)).X)));
			const float InnerRadiusNoise = FMath::Max(0.0f, RadiusNoise - RimHalfWidthNoise);
			const float OuterRadiusNoise = RadiusNoise + RimHalfWidthNoise;
			const float InnerRadiusSquared = InnerRadiusNoise * InnerRadiusNoise;
			const float OuterRadiusSquared = OuterRadiusNoise * OuterRadiusNoise;
			const float RingFalloffDenominator = FMath::Max(UE_KINDA_SMALL_NUMBER, (OuterRadiusSquared - InnerRadiusSquared) * 0.5f);
			FNodeLink AboveInner = Context.Editor->SubtractFloat(DistanceSquared, InnerRadiusSquared);
			FNodeLink BelowOuter = Context.Editor->Subtract(Context.Editor->Constant(OuterRadiusSquared), DistanceSquared);
			FNodeLink RingMask = Context.Editor->MaxFloat(Context.Editor->MinFloat(Context.Editor->DivideFloat(Context.Editor->Min(AboveInner, BelowOuter), RingFalloffDenominator), 1.0f), 0.0f);
			Offset = Context.Editor->Add(Offset, Context.Editor->MultiplyFloat(RingMask, ConvertTerrainProfileBlockZDistanceToNoise(Profile.RimHeightBlocks, *Context.ScaleContext)));
		}

		if (Profile.SurfaceNoiseAmplitude > 0.0f && Profile.SurfaceNoiseScale > 0.0f)
		{
			FNodeLink Detail = BuildPlanarSimplexHeightNoise(Context.Editor, *Context.ScaleContext, Profile.SurfaceNoiseAmplitude, Profile.SurfaceNoiseScale, Profile.SeedOffset);
			if (IsTerrainProfileNodeValid(Detail))
			{
				Offset = Context.Editor->Add(Offset, Detail);
			}
		}

		return Offset;
	}

	void ScaleBowlProfile(void* Payload, const float UniformScale)
	{
		FBowlFoundationTerrainProfilePayload& Profile = *static_cast<FBowlFoundationTerrainProfilePayload*>(Payload);
		Profile.CenterXY *= UniformScale;
		Profile.RadiusBlocks *= UniformScale;
		Profile.DepthBlocks *= UniformScale;
		Profile.SurfaceNoiseAmplitude *= UniformScale;
		Profile.RimWidthBlocks *= UniformScale;
		Profile.RimHeightBlocks *= UniformScale;
	}

	void ValidateBowlProfile(const void* Payload, const FFoundationProviderDefinition&, const FString& Path, TArray<FFoundationTerrainProfileValidationIssue>& OutIssues)
	{
		const FBowlFoundationTerrainProfilePayload& Profile = *static_cast<const FBowlFoundationTerrainProfilePayload*>(Payload);
		if (Profile.RadiusBlocks <= 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Bowl terrain profile has a non-positive radius."), TEXT("Set Radius Blocks above zero."));
		}
		if (Profile.DepthBlocks < 0.0f || Profile.SurfaceNoiseAmplitude < 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Bowl terrain profile has negative height settings."), TEXT("Set Depth Blocks and Surface Noise Amplitude to zero or positive authored-block values."));
		}
		if (Profile.bEnableRaisedRim && Profile.RimWidthBlocks <= 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Bowl terrain profile has a non-positive raised rim width."), TEXT("Disable Raised Rim or set Rim Width Blocks above zero."));
		}
		if (Profile.bEnableRaisedRim && Profile.RimHeightBlocks < 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Bowl terrain profile has a negative raised rim height."), TEXT("Set Rim Height Blocks to zero or a positive authored-block value."));
		}
	}

	float GetRidgedMaxPositiveHeight(const void* Payload)
	{
		const FRidgedFoundationTerrainProfilePayload& Profile = *static_cast<const FRidgedFoundationTerrainProfilePayload*>(Payload);
		return FMath::Max(0.0f, Profile.RidgeHeightBlocks);
	}

	float GetRidgedSearchAmplitude(const void* Payload)
	{
		const FRidgedFoundationTerrainProfilePayload& Profile = *static_cast<const FRidgedFoundationTerrainProfilePayload*>(Payload);
		return FMath::Abs(Profile.RidgeHeightBlocks) + FMath::Abs(Profile.ValleyDepthBlocks);
	}

	FNodeLink BuildRidgedSurfaceOffset(const void* Payload, const FFoundationTerrainProfileBuildContext& Context)
	{
		const FRidgedFoundationTerrainProfilePayload& Profile = *static_cast<const FRidgedFoundationTerrainProfilePayload*>(Payload);
		if (Context.Editor == nullptr || Context.ScaleContext == nullptr || Profile.RidgeHeightBlocks <= 0.0f || Profile.RidgeScale <= 0.0f)
		{
			return FNodeLink();
		}

		FNodeLink Source = Context.Editor->OpenSimplex2();
		FNodeLink SeededSource = Context.Editor->SeedOffset(Source, Profile.SeedOffset);
		FNodeLink PlanarSource = Context.Editor->DomainAxisScale(SeededSource, FVector(Profile.RidgeScale, Profile.RidgeScale, 0.0));
		FNodeLink Gain = Context.Editor->Constant(0.55f);
		FNodeLink WeightedStrength = Context.Editor->Constant(0.35f);
		FNodeLink Ridged = Context.Editor->FractalRidged(PlanarSource, Gain, WeightedStrength, 4, 2.0f);
		FNodeLink NormalizedRidges = Context.Editor->Remap(Ridged, 0.05f, 0.35f, 0.0f, 1.0f);
		NormalizedRidges = Context.Editor->MaxFloat(Context.Editor->MinFloat(NormalizedRidges, 1.0f), 0.0f);
		FNodeLink SharpenedRidges = Context.Editor->PowFloatFloat(NormalizedRidges, FMath::Max(0.1f, Profile.RidgeSharpness));
		FNodeLink Relief = Context.Editor->MultiplyFloat(
			SharpenedRidges,
			ConvertTerrainProfileBlockZDistanceToNoise(Profile.RidgeHeightBlocks + Profile.ValleyDepthBlocks, *Context.ScaleContext));
		return Context.Editor->SubtractFloat(Relief, ConvertTerrainProfileBlockZDistanceToNoise(Profile.ValleyDepthBlocks, *Context.ScaleContext));
	}

	void ScaleRidgedProfile(void* Payload, const float UniformScale)
	{
		FRidgedFoundationTerrainProfilePayload& Profile = *static_cast<FRidgedFoundationTerrainProfilePayload*>(Payload);
		Profile.RidgeHeightBlocks *= UniformScale;
		Profile.ValleyDepthBlocks *= UniformScale;
	}

	void ValidateRidgedProfile(const void* Payload, const FFoundationProviderDefinition&, const FString& Path, TArray<FFoundationTerrainProfileValidationIssue>& OutIssues)
	{
		const FRidgedFoundationTerrainProfilePayload& Profile = *static_cast<const FRidgedFoundationTerrainProfilePayload*>(Payload);
		if (Profile.RidgeHeightBlocks < 0.0f || Profile.ValleyDepthBlocks < 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Ridged terrain profile has negative height settings."), TEXT("Set Ridge Height Blocks and Valley Depth Blocks to zero or positive authored-block values."));
		}
		if (Profile.RidgeScale < 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Ridged terrain profile has a negative ridge scale."), TEXT("Set Ridge Scale to zero or a positive value."));
		}
	}

	float GetTerracedMaxPositiveHeight(const void* Payload)
	{
		const FTerracedFoundationTerrainProfilePayload& Profile = *static_cast<const FTerracedFoundationTerrainProfilePayload*>(Payload);
		return FMath::Max(0.0f, Profile.SurfaceNoiseAmplitude);
	}

	float GetTerracedSearchAmplitude(const void* Payload)
	{
		const FTerracedFoundationTerrainProfilePayload& Profile = *static_cast<const FTerracedFoundationTerrainProfilePayload*>(Payload);
		return FMath::Abs(Profile.SurfaceNoiseAmplitude);
	}

	FNodeLink BuildTerracedSurfaceOffset(const void* Payload, const FFoundationTerrainProfileBuildContext& Context)
	{
		const FTerracedFoundationTerrainProfilePayload& Profile = *static_cast<const FTerracedFoundationTerrainProfilePayload*>(Payload);
		if (Context.Editor == nullptr || Context.ScaleContext == nullptr || Profile.SurfaceNoiseAmplitude <= 0.0f || Profile.SurfaceNoiseScale <= 0.0f)
		{
			return FNodeLink();
		}

		FNodeLink SurfaceVariation = BuildPlanarSimplexHeightNoise(Context.Editor, *Context.ScaleContext, Profile.SurfaceNoiseAmplitude, Profile.SurfaceNoiseScale, Profile.SeedOffset);
		if (!IsTerrainProfileNodeValid(SurfaceVariation) || Profile.TerraceStepHeight <= 0.0f)
		{
			return SurfaceVariation;
		}

		const float StepNoise = ConvertTerrainProfileBlockZDistanceToNoise(Profile.TerraceStepHeight, *Context.ScaleContext);
		return StepNoise > UE_KINDA_SMALL_NUMBER
			? Context.Editor->Terrace(SurfaceVariation, 1.0f / StepNoise, FMath::Max(0.0f, Profile.TerraceSmoothness))
			: SurfaceVariation;
	}

	void ScaleTerracedProfile(void* Payload, const float UniformScale)
	{
		FTerracedFoundationTerrainProfilePayload& Profile = *static_cast<FTerracedFoundationTerrainProfilePayload*>(Payload);
		Profile.SurfaceNoiseAmplitude *= UniformScale;
		Profile.TerraceStepHeight *= UniformScale;
	}

	void ValidateTerracedProfile(const void* Payload, const FFoundationProviderDefinition&, const FString& Path, TArray<FFoundationTerrainProfileValidationIssue>& OutIssues)
	{
		const FTerracedFoundationTerrainProfilePayload& Profile = *static_cast<const FTerracedFoundationTerrainProfilePayload*>(Payload);
		if (Profile.SurfaceNoiseAmplitude < 0.0f || Profile.SurfaceNoiseScale < 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Terraced terrain profile has negative surface noise settings."), TEXT("Set Surface Noise Amplitude and Surface Noise Scale to zero or positive values."));
		}
		if (Profile.TerraceStepHeight <= 0.0f)
		{
			AddIssue(OutIssues, Path, TEXT("Terraced terrain profile has a non-positive step height."), TEXT("Set Terrace Step Height above zero."));
		}
	}

	const FFoundationTerrainProfileOps* GetFoundationTerrainProfileOpsRegistry()
	{
		static const FFoundationTerrainProfileOps Registry[] =
		{
			{ FFlatFoundationTerrainProfilePayload::StaticStruct(), TEXT("Flat"), EFoundationTerrainProfileProviderSupport::AnySurface, BuildFlatSurfaceOffset, GetZeroProfileHeight, GetZeroProfileHeight, nullptr, nullptr },
			{ FNoisyFoundationTerrainProfilePayload::StaticStruct(), TEXT("Noisy"), EFoundationTerrainProfileProviderSupport::AnySurface, BuildNoisySurfaceOffset, GetNoisyMaxPositiveHeight, GetNoisySearchAmplitude, ScaleNoisyProfile, ValidateNoisyProfile },
			{ FRollingHillsFoundationTerrainProfilePayload::StaticStruct(), TEXT("Rolling Hills"), EFoundationTerrainProfileProviderSupport::AnySurface, BuildRollingSurfaceOffset, GetRollingMaxPositiveHeight, GetRollingSearchAmplitude, ScaleRollingProfile, ValidateRollingProfile },
			{ FErodedEdgeFoundationTerrainProfilePayload::StaticStruct(), TEXT("Eroded Edge"), EFoundationTerrainProfileProviderSupport::FiniteSurface, BuildErodedSurfaceOffset, GetErodedMaxPositiveHeight, GetErodedSearchAmplitude, ScaleErodedProfile, ValidateErodedProfile },
			{ FCreviceFoundationTerrainProfilePayload::StaticStruct(), TEXT("Crevice"), EFoundationTerrainProfileProviderSupport::AnySurface, BuildCreviceSurfaceOffset, GetCreviceMaxPositiveHeight, GetCreviceSearchAmplitude, ScaleCreviceProfile, ValidateCreviceProfile },
			{ FBowlFoundationTerrainProfilePayload::StaticStruct(), TEXT("Bowl"), EFoundationTerrainProfileProviderSupport::AnySurface, BuildBowlSurfaceOffset, GetBowlMaxPositiveHeight, GetBowlSearchAmplitude, ScaleBowlProfile, ValidateBowlProfile },
			{ FRidgedFoundationTerrainProfilePayload::StaticStruct(), TEXT("Ridged"), EFoundationTerrainProfileProviderSupport::AnySurface, BuildRidgedSurfaceOffset, GetRidgedMaxPositiveHeight, GetRidgedSearchAmplitude, ScaleRidgedProfile, ValidateRidgedProfile },
			{ FTerracedFoundationTerrainProfilePayload::StaticStruct(), TEXT("Terraced"), EFoundationTerrainProfileProviderSupport::AnySurface, BuildTerracedSurfaceOffset, GetTerracedMaxPositiveHeight, GetTerracedSearchAmplitude, ScaleTerracedProfile, ValidateTerracedProfile },
			{ nullptr, nullptr, EFoundationTerrainProfileProviderSupport::None, nullptr, nullptr, nullptr, nullptr, nullptr }
		};

		return Registry;
	}

	EFoundationTerrainProfileProviderSupport GetProviderSupportForType(const EFoundationProviderType ProviderType)
	{
		switch (ProviderType)
		{
		case EFoundationProviderType::Island:
			return EFoundationTerrainProfileProviderSupport::FiniteSurface;
		case EFoundationProviderType::InfinitePlane:
			return EFoundationTerrainProfileProviderSupport::InfiniteSurface;
		default:
			return EFoundationTerrainProfileProviderSupport::None;
		}
	}

	bool IsProfileSupportedForProvider(const FFoundationTerrainProfileOps& Ops, const EFoundationProviderType ProviderType)
	{
		return EnumHasAnyFlags(Ops.ProviderSupport, GetProviderSupportForType(ProviderType));
	}

	const FFoundationTerrainProfileOps* FindFoundationTerrainProfileOps(const FInstancedStruct& TerrainProfile)
	{
		const UScriptStruct* PayloadStruct = TerrainProfile.GetScriptStruct();
		if (PayloadStruct == nullptr)
		{
			return nullptr;
		}

		for (const FFoundationTerrainProfileOps* Ops = GetFoundationTerrainProfileOpsRegistry(); Ops->PayloadStruct != nullptr; ++Ops)
		{
			if (Ops->PayloadStruct == PayloadStruct)
			{
				return Ops;
			}
		}

		return nullptr;
	}
}

FNodeLink BuildFoundationTerrainProfileSurfaceOffsetNode(
	const FInstancedStruct& TerrainProfile,
	const FFoundationTerrainProfileBuildContext& Context)
{
	const FFoundationTerrainProfileOps* Ops = FindFoundationTerrainProfileOps(TerrainProfile);
	return Ops != nullptr && Ops->BuildSurfaceOffset != nullptr
		? Ops->BuildSurfaceOffset(TerrainProfile.GetMemory(), Context)
		: FNodeLink();
}

float GetFoundationTerrainProfileMaxPositiveHeightBlocks(const FInstancedStruct& TerrainProfile)
{
	const FFoundationTerrainProfileOps* Ops = FindFoundationTerrainProfileOps(TerrainProfile);
	return Ops != nullptr && Ops->GetMaxPositiveHeightBlocks != nullptr
		? Ops->GetMaxPositiveHeightBlocks(TerrainProfile.GetMemory())
		: 0.0f;
}

float GetFoundationTerrainProfileSurfaceSearchAmplitudeBlocks(const FInstancedStruct& TerrainProfile)
{
	const FFoundationTerrainProfileOps* Ops = FindFoundationTerrainProfileOps(TerrainProfile);
	return Ops != nullptr && Ops->GetSurfaceSearchAmplitudeBlocks != nullptr
		? Ops->GetSurfaceSearchAmplitudeBlocks(TerrainProfile.GetMemory())
		: 0.0f;
}

bool ApplyFoundationTerrainProfileUniformScale(FInstancedStruct& TerrainProfile, const float UniformScale)
{
	const FFoundationTerrainProfileOps* Ops = FindFoundationTerrainProfileOps(TerrainProfile);
	if (Ops == nullptr)
	{
		return false;
	}

	if (Ops->ApplyUniformScale != nullptr)
	{
		Ops->ApplyUniformScale(TerrainProfile.GetMutableMemory(), UniformScale);
	}
	return true;
}

void AppendFoundationTerrainProfileValidationIssues(
	const FFoundationProviderDefinition& Provider,
	const FString& Path,
	TArray<FFoundationTerrainProfileValidationIssue>& OutIssues)
{
	const FFoundationTerrainProfileOps* Ops = FindFoundationTerrainProfileOps(Provider.TerrainProfile);
	if (Ops == nullptr)
	{
		AddIssue(
			OutIssues,
			Path,
			TEXT("Terrain Profile is not a supported foundation terrain profile payload."),
			*FString::Printf(TEXT("Choose one of the supported foundation terrain profile payloads: %s."), *GetSupportedFoundationTerrainProfileNames()));
		return;
	}

	if (!IsProfileSupportedForProvider(*Ops, Provider.ProviderType))
	{
		AddIssue(
			OutIssues,
			Path,
			TEXT("Terrain Profile is not compatible with this foundation provider type."),
			*FString::Printf(
				TEXT("Choose one of the supported terrain profiles for this provider: %s."),
				*GetSupportedFoundationTerrainProfileNamesForProvider(Provider.ProviderType)));
		return;
	}

	if (Ops->AppendValidationIssues != nullptr)
	{
		Ops->AppendValidationIssues(Provider.TerrainProfile.GetMemory(), Provider, Path, OutIssues);
	}
}

bool IsFoundationTerrainProfilePayloadSupported(const FInstancedStruct& TerrainProfile)
{
	return FindFoundationTerrainProfileOps(TerrainProfile) != nullptr;
}

bool IsFoundationTerrainProfilePayloadSupportedByProvider(
	const FInstancedStruct& TerrainProfile,
	const EFoundationProviderType ProviderType)
{
	const FFoundationTerrainProfileOps* Ops = FindFoundationTerrainProfileOps(TerrainProfile);
	return Ops != nullptr && IsProfileSupportedForProvider(*Ops, ProviderType);
}

FString GetSupportedFoundationTerrainProfileNames()
{
	TArray<FString> Names;
	for (const FFoundationTerrainProfileOps* Ops = GetFoundationTerrainProfileOpsRegistry(); Ops->PayloadStruct != nullptr; ++Ops)
	{
		Names.Add(Ops->DisplayName);
	}
	return FString::Join(Names, TEXT(", "));
}

FString GetSupportedFoundationTerrainProfileNamesForProvider(const EFoundationProviderType ProviderType)
{
	TArray<FString> Names;
	for (const FFoundationTerrainProfileOps* Ops = GetFoundationTerrainProfileOpsRegistry(); Ops->PayloadStruct != nullptr; ++Ops)
	{
		if (IsProfileSupportedForProvider(*Ops, ProviderType))
		{
			Names.Add(Ops->DisplayName);
		}
	}
	return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : FString(TEXT("None"));
}
