// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Spawn/ChunkWorldReservationSpawnSourceProvider.h"

#include "Biome/Noise/Strategy/BiomeStrategyBindingLibrary.h"
#include "Biome/Noise/WorldGenScaleContext.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Spawn/ChunkWorldSpawnComponent.h"

#include "ChunkWorldStructs/ChunkWorldRuntimeStructs.h"

namespace
{
	/** Builds finite world-space query bounds so an intersecting reservation field cannot warm or sample beyond a request radius. */
	bool TryBuildWorldSearchBounds(const FChunkWorldSpawnRequest& Request, FBox& OutWorldBounds)
	{
		OutWorldBounds = FBox(ForceInit);
		if (Request.PreferredSearchRadius <= 0.0f)
		{
			return true;
		}

		const FVector Extent(Request.PreferredSearchRadius);
		const FVector Minimum = Request.PreferredSearchOrigin - Extent;
		const FVector Maximum = Request.PreferredSearchOrigin + Extent;
		if (!FMath::IsFinite(Minimum.X) || !FMath::IsFinite(Minimum.Y) || !FMath::IsFinite(Minimum.Z)
			|| !FMath::IsFinite(Maximum.X) || !FMath::IsFinite(Maximum.Y) || !FMath::IsFinite(Maximum.Z))
		{
			return false;
		}

		OutWorldBounds = FBox(Minimum, Maximum);
		return OutWorldBounds.IsValid != 0;
	}
}

UChunkWorldReservationSpawnSourceProvider::UChunkWorldReservationSpawnSourceProvider()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UChunkWorldReservationSpawnSourceProvider::BeginPlay()
{
	Super::BeginPlay();

	if (AChunkWorldExtended* ChunkWorld = GetChunkWorld(); ChunkWorld != nullptr && ChunkWorld->HasAuthority())
	{
		if (UChunkWorldSpawnComponent* SpawnComponent = ChunkWorld->GetSpawnComponent())
		{
			SpawnComponent->RegisterSpawnSourceProvider(this);
		}
	}
}

void UChunkWorldReservationSpawnSourceProvider::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AChunkWorldExtended* ChunkWorld = GetChunkWorld(); ChunkWorld != nullptr && ChunkWorld->HasAuthority())
	{
		if (UChunkWorldSpawnComponent* SpawnComponent = ChunkWorld->GetSpawnComponent())
		{
			SpawnComponent->UnregisterSpawnSourceProvider(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

EChunkWorldSpawnSourceFamily UChunkWorldReservationSpawnSourceProvider::GetSpawnSourceFamily() const
{
	return EChunkWorldSpawnSourceFamily::ReservationField;
}

bool UChunkWorldReservationSpawnSourceProvider::CanServeSpawnRequest(const FChunkWorldSpawnRequest& Request) const
{
	return bEnabled
		&& Request.AllowedSourceFamiliesInPriorityOrder.Contains(EChunkWorldSpawnSourceFamily::ReservationField)
		&& GetChunkWorld() != nullptr
		&& GetChunkWorld()->WorldGenDef != nullptr;
}

bool UChunkWorldReservationSpawnSourceProvider::IsSpawnSourceProviderEnabled() const
{
	return bEnabled;
}

void UChunkWorldReservationSpawnSourceProvider::GatherSpawnCandidates(
	const FChunkWorldSpawnRequest& Request,
	const FChunkWorldSpawnQueryContext& QueryContext,
	TArray<FChunkWorldSpawnCandidate>& OutCandidates) const
{
	AChunkWorldExtended* ChunkWorld = QueryContext.ChunkWorld.Get();
	if (ChunkWorld == nullptr || ChunkWorld != GetChunkWorld() || !CanServeSpawnRequest(Request))
	{
		return;
	}

	const FResolvedWorldGenScaleContext ScaleContext = FWorldGenScaleContextResolver::Resolve(ChunkWorld, nullptr, false);
	if (!ScaleContext.bResolvedFromChunkWorld)
	{
		UE_LOG(LogChunkWorldSpawn, Warning, TEXT("Rejected reservation spawn query without a live chunk-world scale context. Provider=%s ChunkWorld=%s"), *GetNameSafe(this), *GetNameSafe(ChunkWorld));
		return;
	}

	FBox AuthoredQueryBounds(ForceInit);
	FBox WorldQueryBounds(ForceInit);
	if (!BuildAuthoredQueryBounds(Request, *ChunkWorld, ScaleContext, AuthoredQueryBounds)
		|| !TryBuildWorldSearchBounds(Request, WorldQueryBounds))
	{
		UE_LOG(LogChunkWorldSpawn, Warning, TEXT("Rejected reservation spawn query because request bounds could not convert to finite authored or world bounds. Provider=%s"), *GetNameSafe(this));
		return;
	}

	TArray<FBiomeStrategyReservationFieldBinding> FieldBindings;
	UBiomeStrategyBindingLibrary::QueryWorldGenReservationFields(ChunkWorld, ChunkWorld->WorldGenDef, AuthoredQueryBounds, FieldBindings);
	FieldBindings.RemoveAll([&Request](const FBiomeStrategyReservationFieldBinding& Binding)
	{
		return Binding.Field.FieldKind != EReservationFieldKind::Spawn
			|| !Binding.Field.FieldTags.HasAll(Request.RequiredSpawnTags)
			|| !FGameplayTagContainer(Binding.RowBinding.BiomeTag).HasAll(Request.RequiredBiomeTags);
	});
	FieldBindings.Sort([](const FBiomeStrategyReservationFieldBinding& Left, const FBiomeStrategyReservationFieldBinding& Right)
	{
		return Left.Field.DebugPath < Right.Field.DebugPath;
	});

	const int32 CandidateLimit = FMath::Min(FMath::Max(1, MaximumCandidatesPerQuery), FMath::Max(0, QueryContext.MaximumCandidates));
	const int32 InitialCandidateCount = OutCandidates.Num();
	const int32 SamplesPerField = FMath::Min(FMath::Clamp(Request.CandidatesPerReservation, 1, 16),
		FMath::Max(1, CandidateLimit / FMath::Max(1, FieldBindings.Num())));
	for (const FBiomeStrategyReservationFieldBinding& Binding : FieldBindings)
	{
		if (OutCandidates.Num() - InitialCandidateCount >= CandidateLimit)
		{
			break;
		}
		if (Binding.Field.FieldKind != EReservationFieldKind::Spawn)
		{
			continue;
		}

		FGameplayTagContainer BiomeTags;
		BiomeTags.AddTag(Binding.RowBinding.BiomeTag);
		if (!Binding.Field.FieldTags.HasAll(Request.RequiredSpawnTags) || !BiomeTags.HasAll(Request.RequiredBiomeTags))
		{
			continue;
		}

		FChunkWorldSpawnFacing Facing;
		float AuthoredYaw = 0.0f;
		if (Binding.Field.SpawnFacingMode == EReservationSpawnFacing::FixedRotation)
		{
			const auto* Rotation = Binding.Field.SpawnFacingPayload.GetPtr<FReservationSpawnRotationPayload>();
			if (!Rotation || !FMath::IsFinite(Rotation->Yaw)) continue;
			AuthoredYaw = Rotation->Yaw;
		}
		else if (Binding.Field.SpawnFacingMode == EReservationSpawnFacing::FixedPoint)
		{
			const auto* Point = Binding.Field.SpawnFacingPayload.GetPtr<FReservationSpawnPointPayload>();
			if (!Point || Point->Target.ContainsNaN()) continue;
			Facing.bFaceTarget = true;
			// The target belongs to the shared strategy frame, not a repeated field's local frame.
			Facing.Target = ChunkWorld->GetActorTransform().TransformPosition(
				ScaleContext.AuthoredBlockPositionToRawBlock(Point->Target) * ScaleContext.BaseBlockSize);
			if (Facing.Target.ContainsNaN()) continue;
		}
		else continue;
		const FVector Heading = ChunkWorld->GetActorTransform().TransformVectorNoScale(
			FRotator(0.0, FRotator::NormalizeAxis(AuthoredYaw), 0.0).Vector());
		// A vertical heading has no upright facing; reject it instead of manufacturing an angle.
		if (Heading.ContainsNaN() || Heading.SizeSquared2D() <= UE_SMALL_NUMBER)
		{
			continue;
		}
		Facing.Yaw = FMath::RadiansToDegrees(FMath::Atan2(Heading.Y, Heading.X));

		FBox CandidateBounds = ConvertAuthoredFieldBoundsToWorld(
			FBox(Binding.Field.AuthoredMinBlock, Binding.Field.AuthoredMaxBlock),
			*ChunkWorld,
			ScaleContext);
		FBox SampleBounds(Binding.Field.AuthoredMinBlock, Binding.Field.AuthoredMaxBlock);
		if (AuthoredQueryBounds.IsValid != 0)
		{
			SampleBounds.Min.X = FMath::Max(SampleBounds.Min.X, AuthoredQueryBounds.Min.X);
			SampleBounds.Min.Y = FMath::Max(SampleBounds.Min.Y, AuthoredQueryBounds.Min.Y);
			SampleBounds.Max.X = FMath::Min(SampleBounds.Max.X, AuthoredQueryBounds.Max.X);
			SampleBounds.Max.Y = FMath::Min(SampleBounds.Max.Y, AuthoredQueryBounds.Max.Y);
		}
		if (SampleBounds.Min.X > SampleBounds.Max.X || SampleBounds.Min.Y > SampleBounds.Max.Y)
		{
			continue;
		}

		FRandomStream Random(HashCombine(GetTypeHash(Request.RequestId), GetTypeHash(Binding.Field.DebugPath)));
		const double Phase = Random.FRand() * 2.0 * PI;
		for (int32 Sample = 0; Sample < SamplesPerField && OutCandidates.Num() - InitialCandidateCount < CandidateLimit; ++Sample)
		{
			FVector2D AuthoredXY(SampleBounds.GetCenter());
			if (Sample > 0)
			{
				const double Angle = Phase + 2.0 * PI * (Sample - 1) / (SamplesPerField - 1);
				const FVector Extent = SampleBounds.GetExtent();
				AuthoredXY += FVector2D(FMath::Cos(Angle) * Extent.X, FMath::Sin(Angle) * Extent.Y) * .75;
			}
			double SurfaceZBlock = 0.0;
			if (!TryResolveTopTheoreticalSurface(Binding, *ChunkWorld, ScaleContext, AuthoredXY, SurfaceZBlock))
			{
				continue;
			}

			const FVector RawSurfacePoint = ScaleContext.AuthoredBlockPositionToRawBlock(FVector(AuthoredXY.X, AuthoredXY.Y, SurfaceZBlock));
			const FVector TheoreticalLocation = ChunkWorld->GetActorTransform().TransformPosition(RawSurfacePoint * ScaleContext.BaseBlockSize);
			if (!FMath::IsFinite(TheoreticalLocation.X) || !FMath::IsFinite(TheoreticalLocation.Y) || !FMath::IsFinite(TheoreticalLocation.Z))
			{
				continue;
			}
			if (WorldQueryBounds.IsValid != 0)
			{
				if (CandidateBounds.Max.X < WorldQueryBounds.Min.X || CandidateBounds.Min.X > WorldQueryBounds.Max.X
					|| CandidateBounds.Max.Y < WorldQueryBounds.Min.Y || CandidateBounds.Min.Y > WorldQueryBounds.Max.Y)
				{
					continue;
				}

				CandidateBounds.Min.X = FMath::Max(CandidateBounds.Min.X, WorldQueryBounds.Min.X);
				CandidateBounds.Min.Y = FMath::Max(CandidateBounds.Min.Y, WorldQueryBounds.Min.Y);
				CandidateBounds.Max.X = FMath::Min(CandidateBounds.Max.X, WorldQueryBounds.Max.X);
				CandidateBounds.Max.Y = FMath::Min(CandidateBounds.Max.Y, WorldQueryBounds.Max.Y);
			}
			if (TheoreticalLocation.X < CandidateBounds.Min.X || TheoreticalLocation.X > CandidateBounds.Max.X
				|| TheoreticalLocation.Y < CandidateBounds.Min.Y || TheoreticalLocation.Y > CandidateBounds.Max.Y)
			{
				continue;
			}

			FChunkWorldSpawnCandidate& Candidate = OutCandidates.AddDefaulted_GetRef();
			Candidate.SourceFamily = EChunkWorldSpawnSourceFamily::ReservationField;
			Candidate.SourceKind = TEXT("SpawnReservationField");
			Candidate.SourceId = FName(*Binding.Field.DebugPath);
			Candidate.SearchBounds = CandidateBounds;
			Candidate.TheoreticalLocation = TheoreticalLocation;
			Candidate.Facing = Facing;
			Candidate.TheoreticalSurfaceZ = TheoreticalLocation.Z;
			Candidate.SpawnTags = Binding.Field.FieldTags;
			Candidate.BiomeTags = BiomeTags;
			Candidate.DebugLabel = Binding.Field.DebugPath;
		}
	}
}

AChunkWorldExtended* UChunkWorldReservationSpawnSourceProvider::GetChunkWorld() const
{
	return Cast<AChunkWorldExtended>(GetOwner());
}

bool UChunkWorldReservationSpawnSourceProvider::BuildAuthoredQueryBounds(
	const FChunkWorldSpawnRequest& Request,
	AChunkWorldExtended& ChunkWorld,
	const FResolvedWorldGenScaleContext& ScaleContext,
	FBox& OutAuthoredBounds) const
{
	OutAuthoredBounds = FBox(ForceInit);
	if (Request.PreferredSearchRadius <= 0.0f)
	{
		return true;
	}

	for (int32 XSign = -1; XSign <= 1; XSign += 2)
	{
		for (int32 YSign = -1; YSign <= 1; YSign += 2)
		{
			for (int32 ZSign = -1; ZSign <= 1; ZSign += 2)
			{
				const FVector WorldCorner = Request.PreferredSearchOrigin + FVector(
					Request.PreferredSearchRadius * static_cast<float>(XSign),
					Request.PreferredSearchRadius * static_cast<float>(YSign),
					Request.PreferredSearchRadius * static_cast<float>(ZSign));
				if (!FMath::IsFinite(WorldCorner.X) || !FMath::IsFinite(WorldCorner.Y) || !FMath::IsFinite(WorldCorner.Z)
					|| (Request.PreferredSearchRadius > 0.0f
						&& (WorldCorner.X == Request.PreferredSearchOrigin.X
							|| WorldCorner.Y == Request.PreferredSearchOrigin.Y
							|| WorldCorner.Z == Request.PreferredSearchOrigin.Z)))
				{
					return false;
				}

				const FVector RawBlock = ChunkWorld.GetActorTransform().InverseTransformPosition(WorldCorner) / static_cast<double>(ScaleContext.BaseBlockSize);
				const FVector AuthoredBlock = RawBlock - ScaleContext.AuthoredOriginToRawBlockOffset;
				if (!FMath::IsFinite(RawBlock.X) || !FMath::IsFinite(RawBlock.Y) || !FMath::IsFinite(RawBlock.Z)
					|| !FMath::IsFinite(AuthoredBlock.X) || !FMath::IsFinite(AuthoredBlock.Y) || !FMath::IsFinite(AuthoredBlock.Z))
				{
					return false;
				}
				OutAuthoredBounds += AuthoredBlock;
			}
		}
	}

	return OutAuthoredBounds.IsValid != 0;
}

bool UChunkWorldReservationSpawnSourceProvider::TryResolveTopTheoreticalSurface(
	const FBiomeStrategyReservationFieldBinding& Binding,
	AChunkWorldExtended& ChunkWorld,
	const FResolvedWorldGenScaleContext& ScaleContext,
	const FVector2D& AuthoredXY,
	double& OutSurfaceZBlock) const
{
	OutSurfaceZBlock = 0.0;
	const int32 MaximumSamples = FMath::Max(2, MaximumTheoreticalSurfaceSamples);
	const double MinimumZ = Binding.Field.AuthoredMinBlock.Z;
	const double MaximumZ = Binding.Field.AuthoredMaxBlock.Z;
	const double VerticalSpan = MaximumZ - MinimumZ;
	if (!FMath::IsFinite(AuthoredXY.X) || !FMath::IsFinite(AuthoredXY.Y)
		|| !FMath::IsFinite(MinimumZ) || !FMath::IsFinite(MaximumZ) || !FMath::IsFinite(VerticalSpan)
		|| VerticalSpan < 0.0 || VerticalSpan >= static_cast<double>(MaximumSamples))
	{
		return false;
	}

	const FRuntimeWorldConfig* Config = ChunkWorld.RuntimeConfig;
	if (Config == nullptr || Config->BaseBlockSize <= 0)
	{
		return false;
	}

	// Sample the base-block lattice used by ShapeBatch, not fractional field-bound coordinates.
	const FVector RawTop = ScaleContext.AuthoredBlockPositionToRawBlock(FVector(AuthoredXY.X, AuthoredXY.Y, MaximumZ));
	const double TopZ = FMath::FloorToDouble(RawTop.Z);
	const double BottomZ = FMath::CeilToDouble(MinimumZ + ScaleContext.AuthoredOriginToRawBlockOffset.Z);
	if (RawTop.ContainsNaN() || !FMath::IsFinite(BottomZ) || TopZ <= BottomZ
		|| TopZ - BottomZ >= FMath::Min(MaximumSamples, MAX_int32 - 16))
	{
		return false;
	}
	const int32 SampleCount = static_cast<int32>(TopZ - BottomZ) + 1;
	// Native FastNoise bulk kernels access whole SIMD lanes, including the final partial group.
	const int32 PaddedCount = Align(SampleCount, 16);
	TArray<float> X, Y, Z, OffsetX, OffsetY, OffsetZ, Density, Power;
	TArray<int32> Biomes, Switches;
	X.SetNumUninitialized(PaddedCount);
	Y.SetNumUninitialized(PaddedCount);
	Z.SetNumUninitialized(PaddedCount);
	OffsetX.SetNumUninitialized(PaddedCount);
	OffsetY.SetNumUninitialized(PaddedCount);
	OffsetZ.SetNumUninitialized(PaddedCount);
	Density.Init(0.0f, PaddedCount);
	Power.Init(0.0f, PaddedCount);
	Biomes.Init(EmptyBiome, PaddedCount);
	Switches.Init(0, PaddedCount);
	const FIntVector NoiseOffset = Config->NoiseCoordinateOffset / Config->BaseBlockSize;
	// Match native float coordinate arithmetic too; noise near a cell boundary can be seed-sensitive.
	const float FrequencyX = 0.0001f * Config->BaseBlockSize * Config->NoiseScale.X;
	const float FrequencyY = 0.0001f * Config->BaseBlockSize * Config->NoiseScale.Y;
	const float FrequencyZ = 0.0001f * Config->BaseBlockSize * Config->NoiseScale.Z;
	for (int32 Index = 0; Index < PaddedCount; ++Index)
	{
		X[Index] = static_cast<float>(FMath::FloorToDouble(RawTop.X) + NoiseOffset.X) * FrequencyX;
		Y[Index] = static_cast<float>(FMath::FloorToDouble(RawTop.Y) + NoiseOffset.Y) * FrequencyY;
		Z[Index] = static_cast<float>(TopZ - FMath::Min(Index, SampleCount - 1) + NoiseOffset.Z) * FrequencyZ;
		if (!FMath::IsFinite(X[Index]) || !FMath::IsFinite(Y[Index]) || !FMath::IsFinite(Z[Index])) return false;
	}
	const auto Warp = [&](const FastNoise::SmartNode<>& Node, const TArray<float>& Base, TArray<float>& Output)
	{
		if (Node)
		{
			Node->GenPositionArray3D(Output.GetData(), PaddedCount, X.GetData(), Y.GetData(), Z.GetData(), 0.0f, 0.0f, 0.0f, ChunkWorld.Seed);
			for (int32 Index = 0; Index < PaddedCount; ++Index) Output[Index] += Base[Index];
		}
		else Output = Base;
	};
	Warp(Config->BiomeOffsetXNode, X, OffsetX);
	Warp(Config->BiomeOffsetYNode, Y, OffsetY);
	Warp(Config->BiomeOffsetZNode, Z, OffsetZ);
	// Use the native composition kernels and compiled nodes, including domain/branch selection,
	// density-only contributions and world noise. A reservation's GenA alone is not the world surface.
	if (Config->GenOnlyTestNode)
	{
		Config->GenOnlyTestNode->Gen3DComplexAdd(Density.GetData(), X.GetData(), Y.GetData(), Z.GetData(), ChunkWorld.Seed, PaddedCount, 1.0f);
	}
	else
	{
		for (int32 Index = 0; Index < Config->Biomes.Num(); ++Index)
		{
			const FRuntimeBiome& Biome = Config->Biomes[Index];
			if (!Biome.DomainNode || !Biome.GenANode) return false;
			if (Biome.NoiseOnly)
			{
				Biome.GenANode->Gen3DAdd(Density.GetData(), X.GetData(), Y.GetData(), Z.GetData(), OffsetX.GetData(), OffsetY.GetData(), OffsetZ.GetData(), ChunkWorld.Seed, PaddedCount, Biome.DomainNode.get());
			}
			else
			{
				if (!Biome.GenBNode || !Biome.DualSwitchNode) return false;
				Biome.GenANode->Gen3DFullAdd(Density.GetData(), X.GetData(), Y.GetData(), Z.GetData(), OffsetX.GetData(), OffsetY.GetData(), OffsetZ.GetData(), ChunkWorld.Seed, PaddedCount,
					Biome.Overlap, Biome.DomainOver, Biome.GenBNode.get(), Biome.DualSwitchNode.get(), Biome.DomainNode.get(), Index, Power.GetData(), Switches.GetData(), Biomes.GetData());
			}
		}
		if (Config->WorldGenNode)
			Config->WorldGenNode->Gen3DCompAddWV(Density.GetData(), Power.GetData(), X.GetData(), Y.GetData(), Z.GetData(), ChunkWorld.Seed, PaddedCount, 1.0f);
	}

	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		if (!FMath::IsFinite(Density[Index])) return false;
		if (Density[Index] <= 0.0f)
		{
			// A solid upper boundary does not establish a surface inside the region. Voxel Z names
			// the cell; its top face is Z+1. Capsule-center placement remains character-owned.
			if (Index == 0) return false;
			OutSurfaceZBlock = TopZ - Index + 1.0 - ScaleContext.AuthoredOriginToRawBlockOffset.Z;
			return OutSurfaceZBlock >= MinimumZ && OutSurfaceZBlock <= MaximumZ;
		}
	}
	return false;
}

FBox UChunkWorldReservationSpawnSourceProvider::ConvertAuthoredFieldBoundsToWorld(
	const FBox& AuthoredBounds,
	const AChunkWorldExtended& ChunkWorld,
	const FResolvedWorldGenScaleContext& ScaleContext)
{
	FBox WorldBounds(ForceInit);
	for (int32 XSide = 0; XSide < 2; ++XSide)
	{
		for (int32 YSide = 0; YSide < 2; ++YSide)
		{
			for (int32 ZSide = 0; ZSide < 2; ++ZSide)
			{
				const FVector AuthoredCorner(
					XSide == 0 ? AuthoredBounds.Min.X : AuthoredBounds.Max.X,
					YSide == 0 ? AuthoredBounds.Min.Y : AuthoredBounds.Max.Y,
					ZSide == 0 ? AuthoredBounds.Min.Z : AuthoredBounds.Max.Z);
				const FVector RawBlock = ScaleContext.AuthoredBlockPositionToRawBlock(AuthoredCorner);
				WorldBounds += ChunkWorld.GetActorTransform().TransformPosition(RawBlock * ScaleContext.BaseBlockSize);
			}
		}
	}

	return WorldBounds;
}
