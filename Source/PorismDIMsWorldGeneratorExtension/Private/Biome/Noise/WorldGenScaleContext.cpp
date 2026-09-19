// Copyright 2026 Spotted Loaf Studio

#include "Biome/Noise/WorldGenScaleContext.h"

#include "ChunkWorld/ChunkWorldCore.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "Misc/ScopeLock.h"

namespace
{
	constexpr double LargeAuthoredBound = 1000000.0;

	int32 AxisIndexX()
	{
		return 0;
	}

	int32 AxisIndexY()
	{
		return 1;
	}

	int32 AxisIndexZ()
	{
		return 2;
	}

	double GetVectorComponent(const FVector& Value, const int32 Axis)
	{
		return Axis == AxisIndexX() ? Value.X : (Axis == AxisIndexY() ? Value.Y : Value.Z);
	}

	int32 GetIntVectorComponent(const FIntVector& Value, const int32 Axis)
	{
		return Axis == AxisIndexX() ? Value.X : (Axis == AxisIndexY() ? Value.Y : Value.Z);
	}

	void SetVectorComponent(FVector& Value, const int32 Axis, const double Component)
	{
		if (Axis == AxisIndexX())
		{
			Value.X = Component;
		}
		else if (Axis == AxisIndexY())
		{
			Value.Y = Component;
		}
		else
		{
			Value.Z = Component;
		}
	}

	EAxisBehavior GetAxisBehavior(const FWorldGenScaleSettings& Settings, const int32 Axis)
	{
		return Axis == AxisIndexX() ? Settings.AxisBehaviorX : (Axis == AxisIndexY() ? Settings.AxisBehaviorY : Settings.AxisBehaviorZ);
	}

	EAxisBehavior GetAxisBehavior(const UWorldGenDef& WorldGenDef, const int32 Axis)
	{
		return Axis == AxisIndexX() ? WorldGenDef.AxisBehaviorX : (Axis == AxisIndexY() ? WorldGenDef.AxisBehaviorY : WorldGenDef.AxisBehaviorZ);
	}

	bool IsFiniteAxis(const EAxisBehavior AxisBehavior)
	{
		return AxisBehavior != EAxisBehavior::Infinity;
	}

	double ResolveFiniteAxisOriginRawBlock(
		const FResolvedWorldGenScaleContext& Context,
		const int32 Axis,
		const double Span)
	{
		// Project-authored authored biome zero should preserve Porism's calibrated noise origin
		// when the active WorldGenDef offset places that origin inside the finite span.
		const double NoiseOffsetBlocks = GetVectorComponent(Context.NoiseCoordinateOffsetBlocks, Axis);
		const double NoiseOriginRawBlock = -NoiseOffsetBlocks;
		return !FMath::IsNearlyZero(NoiseOffsetBlocks) && NoiseOriginRawBlock >= 0.0 && NoiseOriginRawBlock <= Span
			? NoiseOriginRawBlock
			: Span * 0.5;
	}

	FVector MakeSafeNoiseScale(const FVector& NoiseScale)
	{
		return FVector(
			FMath::IsNearlyZero(NoiseScale.X) ? 1.0 : NoiseScale.X,
			FMath::IsNearlyZero(NoiseScale.Y) ? 1.0 : NoiseScale.Y,
			FMath::IsNearlyZero(NoiseScale.Z) ? 1.0 : NoiseScale.Z);
	}

	FIntVector MakeChunkSpanFromParams(const UWorldGenDef& WorldGenDef, const FChunkDataParams& Params)
	{
		const FVector RawSpan = Params.ChunkSizeMulti
			* FVector(WorldGenDef.ChunkBlockSize)
			* Params.BlockSizeMulti;

		return FIntVector(
			FMath::Max(1, FMath::RoundToInt(RawSpan.X)),
			FMath::Max(1, FMath::RoundToInt(RawSpan.Y)),
			FMath::Max(1, FMath::RoundToInt(RawSpan.Z)));
	}

	bool TryGetChunkParams(const UWorldGenDef& WorldGenDef, const int32 ReferenceDetailLevel, FChunkDataParams& OutParams)
	{
		TArray<const FChunkDataParams*> Params;
		if (WorldGenDef.WorldChunksDT != nullptr && WorldGenDef.WorldChunksDT->GetRowMap().Num() > 0)
		{
			for (const TPair<FName, uint8*>& Row : WorldGenDef.WorldChunksDT->GetRowMap())
			{
				if (Row.Value != nullptr)
				{
					Params.Add(reinterpret_cast<const FChunkDataParams*>(Row.Value));
				}
			}
		}
		else
		{
			for (const FChunkDataParams& ChunkParams : WorldGenDef.WorldChunks)
			{
				Params.Add(&ChunkParams);
			}
		}

		if (Params.IsEmpty())
		{
			return false;
		}

		const int32 Index = ReferenceDetailLevel >= 0
			? FMath::Clamp(ReferenceDetailLevel, 0, Params.Num() - 1)
			: Params.Num() - 1;
		OutParams = *Params[Index];
		return true;
	}

	AChunkWorldCore* ResolveScaleChunkWorldFromCreator(UObject* Creator)
	{
		for (UObject* Current = Creator; Current != nullptr; Current = Current->GetOuter())
		{
			if (AChunkWorldCore* ChunkWorld = Cast<AChunkWorldCore>(Current))
			{
				return ChunkWorld;
			}

			if (const UActorComponent* Component = Cast<UActorComponent>(Current))
			{
				if (AChunkWorldCore* OwnerChunkWorld = Cast<AChunkWorldCore>(Component->GetOwner()))
				{
					return OwnerChunkWorld;
				}
			}
		}

		return nullptr;
	}

	FWorldGenScaleSettings MakeSettingsFromWorldGenDef(const UWorldGenDef& WorldGenDef, const int32 ReferenceDetailLevel)
	{
		FWorldGenScaleSettings Settings;
		Settings.BaseBlockSize = FMath::Max(1, WorldGenDef.BaseBlockSize);
		Settings.NoiseScale = WorldGenDef.NoiseScale;
		Settings.NoiseCoordinateOffset = WorldGenDef.NoiseCoordinateOffset;
		Settings.AxisBehaviorX = WorldGenDef.AxisBehaviorX;
		Settings.AxisBehaviorY = WorldGenDef.AxisBehaviorY;
		Settings.AxisBehaviorZ = WorldGenDef.AxisBehaviorZ;
		Settings.ReferenceDetailLevel = ReferenceDetailLevel;

		FChunkDataParams ChunkParams;
		if (TryGetChunkParams(WorldGenDef, ReferenceDetailLevel, ChunkParams))
		{
			Settings.FallbackFiniteAxisBlockSpan = MakeChunkSpanFromParams(WorldGenDef, ChunkParams);
		}

		return Settings;
	}

	FString MakeCacheKey(
		const UObject* SourceObject,
		const FWorldGenScaleSettings& Settings,
		const bool bRuntime,
		const bool bOverride)
	{
		return FString::Printf(
			TEXT("%s|%d|%d|%d|%s|%s|%s|%d|%d|%d|%d"),
			SourceObject != nullptr ? *SourceObject->GetPathName() : TEXT("None"),
			bRuntime ? 1 : 0,
			bOverride ? 1 : 0,
			Settings.BaseBlockSize,
			*Settings.NoiseScale.ToString(),
			*Settings.NoiseCoordinateOffset.ToString(),
			*Settings.FallbackFiniteAxisBlockSpan.ToString(),
			static_cast<int32>(Settings.AxisBehaviorX),
			static_cast<int32>(Settings.AxisBehaviorY),
			static_cast<int32>(Settings.AxisBehaviorZ),
			Settings.ReferenceDetailLevel);
	}

	FResolvedWorldGenScaleContext BuildResolvedContext(
		const FWorldGenScaleSettings& Settings,
		const bool bFromChunkWorld,
		const bool bFromOverride,
		const bool bFallback)
	{
		FResolvedWorldGenScaleContext Context;
		Context.bResolvedFromChunkWorld = bFromChunkWorld;
		Context.bResolvedFromOverride = bFromOverride;
		Context.bUsingFallbackDefaults = bFallback;
		Context.BaseBlockSize = FMath::Max(1, Settings.BaseBlockSize);
		Context.NoiseScale = MakeSafeNoiseScale(Settings.NoiseScale);
		Context.NoiseCoordinateOffset = Settings.NoiseCoordinateOffset;
		Context.ReferenceChunkBlockSpan = FIntVector(
			FMath::Max(1, Settings.FallbackFiniteAxisBlockSpan.X),
			FMath::Max(1, Settings.FallbackFiniteAxisBlockSpan.Y),
			FMath::Max(1, Settings.FallbackFiniteAxisBlockSpan.Z));

		Context.BlocksToNoiseScale = FVector(
			0.0001 * Context.BaseBlockSize * Context.NoiseScale.X,
			0.0001 * Context.BaseBlockSize * Context.NoiseScale.Y,
			0.0001 * Context.BaseBlockSize * Context.NoiseScale.Z);
		Context.NoiseToBlocksScale = FVector(
			FMath::IsNearlyZero(Context.BlocksToNoiseScale.X) ? 0.0 : 1.0 / Context.BlocksToNoiseScale.X,
			FMath::IsNearlyZero(Context.BlocksToNoiseScale.Y) ? 0.0 : 1.0 / Context.BlocksToNoiseScale.Y,
			FMath::IsNearlyZero(Context.BlocksToNoiseScale.Z) ? 0.0 : 1.0 / Context.BlocksToNoiseScale.Z);
		Context.NoiseCoordinateOffsetBlocks = FVector(Context.NoiseCoordinateOffset) / static_cast<double>(Context.BaseBlockSize);
		Context.RawBlockToNoiseOffset = Context.NoiseCoordinateOffsetBlocks * Context.BlocksToNoiseScale;

		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const EAxisBehavior AxisBehavior = GetAxisBehavior(Settings, Axis);
			const double Span = FMath::Max(1, GetIntVectorComponent(Context.ReferenceChunkBlockSpan, Axis));
			SetVectorComponent(Context.FiniteAxisBlockSpan, Axis, Span);

			if (IsFiniteAxis(AxisBehavior))
			{
				const double OriginRawBlock = ResolveFiniteAxisOriginRawBlock(Context, Axis, Span);
				SetVectorComponent(Context.AuthoredOriginToRawBlockOffset, Axis, OriginRawBlock);
				SetVectorComponent(Context.AuthoredMinBlock, Axis, -OriginRawBlock);
				SetVectorComponent(Context.AuthoredMaxBlock, Axis, Span - OriginRawBlock);
			}
			else
			{
				SetVectorComponent(Context.AuthoredOriginToRawBlockOffset, Axis, 0.0);
				SetVectorComponent(Context.AuthoredMinBlock, Axis, -LargeAuthoredBound);
				SetVectorComponent(Context.AuthoredMaxBlock, Axis, LargeAuthoredBound);
			}
		}

		return Context;
	}

	TMap<FString, FResolvedWorldGenScaleContext>& GetContextCache()
	{
		static TMap<FString, FResolvedWorldGenScaleContext> Cache;
		return Cache;
	}

	FCriticalSection& GetContextCacheLock()
	{
		static FCriticalSection Lock;
		return Lock;
	}
}

FVector FResolvedWorldGenScaleContext::AuthoredBlockPositionToRawBlock(const FVector& AuthoredBlockPosition) const
{
	return AuthoredBlockPosition + AuthoredOriginToRawBlockOffset;
}

FVector FResolvedWorldGenScaleContext::AuthoredBlockPositionToNoise(const FVector& AuthoredBlockPosition) const
{
	const FVector RawBlockPosition = AuthoredBlockPositionToRawBlock(AuthoredBlockPosition);
	return (RawBlockPosition + NoiseCoordinateOffsetBlocks) * BlocksToNoiseScale;
}

FVector FResolvedWorldGenScaleContext::BlockDistanceToNoise(const FVector& BlockDistance) const
{
	return BlockDistance * BlocksToNoiseScale;
}

FVector FResolvedWorldGenScaleContext::NoisePositionToAuthoredBlock(const FVector& NoisePosition) const
{
	const FVector RawBlockPosition = (NoisePosition * NoiseToBlocksScale) - NoiseCoordinateOffsetBlocks;
	return RawBlockPosition - AuthoredOriginToRawBlockOffset;
}

float FResolvedWorldGenScaleContext::NoiseZToAuthoredBlockZ(const float NoiseZ) const
{
	return static_cast<float>((NoiseZ * NoiseToBlocksScale.Z) - NoiseCoordinateOffsetBlocks.Z - AuthoredOriginToRawBlockOffset.Z);
}

FResolvedWorldGenScaleContext FWorldGenScaleContextResolver::Resolve(
	UObject* Creator,
	const FWorldGenScaleSettings* ExplicitOverride,
	const bool bUseExplicitOverride)
{
	UObject* SourceObject = nullptr;
	bool bFromChunkWorld = false;
	bool bFromOverride = false;
	bool bFallback = false;

	FWorldGenScaleSettings Settings;
	if (AChunkWorldCore* ChunkWorld = ResolveScaleChunkWorldFromCreator(Creator))
	{
		SourceObject = ChunkWorld->WorldGenDef != nullptr ? static_cast<UObject*>(ChunkWorld->WorldGenDef) : static_cast<UObject*>(ChunkWorld);
		Settings = ChunkWorld->WorldGenDef != nullptr
			? MakeSettingsFromWorldGenDef(*ChunkWorld->WorldGenDef, ExplicitOverride != nullptr ? ExplicitOverride->ReferenceDetailLevel : -1)
			: Settings;
		bFromChunkWorld = true;
	}
	else if (bUseExplicitOverride && ExplicitOverride != nullptr)
	{
		SourceObject = Creator;
		Settings = *ExplicitOverride;
		bFromOverride = true;
	}
	else
	{
		SourceObject = Creator;
		bFallback = true;
	}

	const FString CacheKey = MakeCacheKey(SourceObject, Settings, bFromChunkWorld, bFromOverride);
	{
		FScopeLock ScopeLock(&GetContextCacheLock());
		if (const FResolvedWorldGenScaleContext* Cached = GetContextCache().Find(CacheKey))
		{
			return *Cached;
		}

		FResolvedWorldGenScaleContext Resolved = BuildResolvedContext(Settings, bFromChunkWorld, bFromOverride, bFallback);
		GetContextCache().Add(CacheKey, Resolved);
		return Resolved;
	}
}

void FWorldGenScaleContextResolver::ClearCache()
{
	FScopeLock ScopeLock(&GetContextCacheLock());
	GetContextCache().Reset();
}
