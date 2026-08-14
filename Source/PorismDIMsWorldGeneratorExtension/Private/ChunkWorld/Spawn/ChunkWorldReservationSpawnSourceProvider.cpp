// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Spawn/ChunkWorldReservationSpawnSourceProvider.h"

#include "Biome/Noise/Strategy/BiomeStrategyBindingLibrary.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "Biome/Noise/WorldGenScaleContext.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Spawn/ChunkWorldSpawnComponent.h"

#include <vector>

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
	FieldBindings.Sort([](const FBiomeStrategyReservationFieldBinding& Left, const FBiomeStrategyReservationFieldBinding& Right)
	{
		return Left.Field.DebugPath < Right.Field.DebugPath;
	});

	const int32 CandidateLimit = FMath::Min(FMath::Max(1, MaximumCandidatesPerQuery), FMath::Max(0, QueryContext.MaximumCandidates));
	const int32 InitialCandidateCount = OutCandidates.Num();
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

		FRandomStream RandomStream(static_cast<int32>(
			Request.RequestId.A ^ Request.RequestId.B ^ Request.RequestId.C ^ Request.RequestId.D
			^ GetTypeHash(Binding.Field.DebugPath)));
		const FVector2D AuthoredXY(
			FMath::Lerp(SampleBounds.Min.X, SampleBounds.Max.X, RandomStream.FRand()),
			FMath::Lerp(SampleBounds.Min.Y, SampleBounds.Max.Y, RandomStream.FRand()));
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
		Candidate.TheoreticalSurfaceZ = TheoreticalLocation.Z;
		Candidate.SpawnTags = Binding.Field.FieldTags;
		Candidate.BiomeTags = MoveTemp(BiomeTags);
		Candidate.DebugLabel = Binding.Field.DebugPath;
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

	std::vector<FNodeLink> Nodes;
	UBiomeFastNoiseEditor* const Editor = NewObject<UBiomeFastNoiseEditor>(GetTransientPackage());
	if (Editor == nullptr || Binding.RowBinding.Strategy == nullptr)
	{
		return false;
	}

	Editor->Nodes = &Nodes;
	Editor->Strategy = const_cast<UBiomeStrategyData*>(Binding.RowBinding.Strategy.Get());
	Editor->NoiseSlot = EBiomeNoiseSlot::GenA;
	Editor->BiomeTag = Binding.RowBinding.BiomeTag;
	const FNodeLink Terrain = Editor->GetNoiseRef(&ChunkWorld);
	if (Terrain.Node == nullptr || !Terrain.Node->BaseNode)
	{
		return false;
	}

	// Scan only this authored field from top down: first solid GenA sample excludes lower cave pockets.
	for (double Z = MaximumZ; Z >= MinimumZ; Z -= 1.0)
	{
		const FVector NoisePosition = ScaleContext.AuthoredBlockPositionToNoise(FVector(AuthoredXY.X, AuthoredXY.Y, Z));
		const float Density = Terrain.Node->BaseNode->GenSingle3D(
			static_cast<float>(NoisePosition.X),
			static_cast<float>(NoisePosition.Y),
			static_cast<float>(NoisePosition.Z),
			0);
		if (FMath::IsFinite(Density) && Density <= 0.0f)
		{
			OutSurfaceZBlock = Z;
			return true;
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
