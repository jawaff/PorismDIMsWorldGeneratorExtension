// Copyright 2026 Spotted Loaf Studio

#include "Biome/Island/IslandBiomeFastNoiseTestSupport.h"
#include "Biome/Noise/Strategy/BiomeStrategyBindingLibrary.h"
#include "Biome/Noise/WorldGenScaleContext.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Spawn/ChunkWorldReservationSpawnSourceProvider.h"
#include "ChunkWorld/Spawn/Support/ChunkWorldSpawnTestWorld.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Misc/AutomationTest.h"

namespace
{
	/** Creates one minimal strategy-backed world definition with a finite vertical scale context. */
	UWorldGenDef* CreateReservationSpawnWorldGenDef(AChunkWorldExtended& ChunkWorld)
	{
		UWorldGenDef* const WorldGenDef = NewObject<UWorldGenDef>(&ChunkWorld);
		WorldGenDef->BaseBlockSize = 100;
		WorldGenDef->ChunkBlockSize = FIntVector(16, 16, 256);
		WorldGenDef->AxisBehaviorX = EAxisBehavior::Infinity;
		WorldGenDef->AxisBehaviorY = EAxisBehavior::Infinity;
		WorldGenDef->AxisBehaviorZ = EAxisBehavior::SingleChunk;

		FChunkDataParams FinestLayer;
		FinestLayer.BlockSizeMulti = 1.0;
		FinestLayer.ChunkSizeMulti = FVector::OneVector;
		WorldGenDef->WorldChunks.Add(FinestLayer);

		UBiomeStrategyData* const Strategy = CreateMainMenuIslandStrategy();
		Strategy->RootFoundationProvider.Reservations[0].SpawnFieldTags.AddTag(TestFoundationBiomeTag());
		WorldGenDef->WorldBiomes.Add(CreateStrategyBiomeRow(Strategy, TEXT("Reservation"), TestReservationBiomeTag()));
		return WorldGenDef;
	}

	/** Converts an authored field box through the same public world-scale contract expected from the provider. */
	FBox ConvertAuthoredBounds(const FBox& AuthoredBounds, const AChunkWorldExtended& ChunkWorld, const FResolvedWorldGenScaleContext& ScaleContext)
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
					WorldBounds += ChunkWorld.GetActorTransform().TransformPosition(ScaleContext.AuthoredBlockPositionToRawBlock(AuthoredCorner) * ScaleContext.BaseBlockSize);
				}
			}
		}
		return WorldBounds;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldReservationSpawnSourceProviderConvertsFieldsTest,
	"PorismExtension.ChunkWorld.Spawn.ReservationProvider.ConvertsSpawnFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies reservation spawn candidates use exact live scale-context and actor-transform conversion. */
bool FChunkWorldReservationSpawnSourceProviderConvertsFieldsTest::RunTest(const FString& Parameters)
{
	FWorldContext* WorldContext = nullptr;
	UWorld* const World = ChunkWorldSpawnTest::CreateWorld(WorldContext);
	if (!TestNotNull(TEXT("Transient authority world initializes"), World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FTransform ChunkWorldTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(250.0f, -400.0f, 75.0f));
	AChunkWorldExtended* const ChunkWorld = World->SpawnActor<AChunkWorldExtended>(AChunkWorldExtended::StaticClass(), ChunkWorldTransform, SpawnParameters);
	if (!TestNotNull(TEXT("Chunk world spawns"), ChunkWorld))
	{
		ChunkWorldSpawnTest::DestroyWorld(World, WorldContext);
		return false;
	}

	ChunkWorld->WorldGenDef = CreateReservationSpawnWorldGenDef(*ChunkWorld);
	FWorldGenScaleContextResolver::ClearCache();
	UChunkWorldReservationSpawnSourceProvider* const Provider = NewObject<UChunkWorldReservationSpawnSourceProvider>(ChunkWorld);
	if (!TestNotNull(TEXT("Reservation provider initializes"), Provider))
	{
		ChunkWorldSpawnTest::DestroyWorld(World, WorldContext);
		return false;
	}

	FChunkWorldSpawnRequest Request;
	Request.RequestId = FGuid(901, 902, 903, 904);
	Request.AllowedSourceFamiliesInPriorityOrder.Add(EChunkWorldSpawnSourceFamily::ReservationField);
	Request.RequiredSpawnTags.AddTag(TestFoundationBiomeTag());
	Request.RequiredBiomeTags.AddTag(TestReservationBiomeTag());

	FChunkWorldSpawnQueryContext QueryContext;
	QueryContext.ChunkWorld = ChunkWorld;
	QueryContext.MaximumCandidates = 8;
	TArray<FChunkWorldSpawnCandidate> Candidates;
	Provider->GatherSpawnCandidates(Request, QueryContext, Candidates);

	TArray<FChunkWorldSpawnCandidate> RepeatCandidates;
	Provider->GatherSpawnCandidates(Request, QueryContext, RepeatCandidates);
	TestEqual(TEXT("Same request produces one repeat candidate"), RepeatCandidates.Num(), 1);
	if (Candidates.Num() == 1 && RepeatCandidates.Num() == 1)
	{
		TestTrue(TEXT("Same request keeps deterministic theoretical location"), Candidates[0].TheoreticalLocation.Equals(RepeatCandidates[0].TheoreticalLocation, KINDA_SMALL_NUMBER));
	}

	TArray<FBiomeStrategyReservationFieldBinding> FieldBindings;
	UBiomeStrategyBindingLibrary::QueryWorldGenReservationFields(ChunkWorld, ChunkWorld->WorldGenDef, FBox(ForceInit), FieldBindings);
	const FBiomeStrategyReservationFieldBinding* const SpawnBinding = FieldBindings.FindByPredicate(
		[](const FBiomeStrategyReservationFieldBinding& Binding)
		{
			return Binding.Field.FieldKind == EReservationFieldKind::Spawn;
		});
	TestNotNull(TEXT("Strategy exposes one spawn-readable reservation field"), SpawnBinding);
	if (SpawnBinding == nullptr)
	{
		ChunkWorldSpawnTest::DestroyWorld(World, WorldContext);
		return false;
	}

	const FResolvedWorldGenScaleContext ScaleContext = FWorldGenScaleContextResolver::Resolve(ChunkWorld, nullptr, false);
	const FBox ExpectedBounds = ConvertAuthoredBounds(FBox(SpawnBinding->Field.AuthoredMinBlock, SpawnBinding->Field.AuthoredMaxBlock), *ChunkWorld, ScaleContext);
	TestEqual(TEXT("Provider returns the matching spawn field"), Candidates.Num(), 1);
	if (Candidates.Num() == 1)
	{
		TestEqual(TEXT("Candidate keeps reservation source family"), Candidates[0].SourceFamily, EChunkWorldSpawnSourceFamily::ReservationField);
		TestTrue(TEXT("Candidate minimum matches live scale and transform conversion"), Candidates[0].SearchBounds.Min.Equals(ExpectedBounds.Min, KINDA_SMALL_NUMBER));
		TestTrue(TEXT("Candidate maximum matches live scale and transform conversion"), Candidates[0].SearchBounds.Max.Equals(ExpectedBounds.Max, KINDA_SMALL_NUMBER));
		TestTrue(TEXT("Candidate theoretical location is finite"), FMath::IsFinite(Candidates[0].TheoreticalLocation.X) && FMath::IsFinite(Candidates[0].TheoreticalLocation.Y) && FMath::IsFinite(Candidates[0].TheoreticalLocation.Z));
		TestTrue(TEXT("Candidate theoretical location stays inside candidate XY bounds"), Candidates[0].TheoreticalLocation.X >= Candidates[0].SearchBounds.Min.X && Candidates[0].TheoreticalLocation.X <= Candidates[0].SearchBounds.Max.X && Candidates[0].TheoreticalLocation.Y >= Candidates[0].SearchBounds.Min.Y && Candidates[0].TheoreticalLocation.Y <= Candidates[0].SearchBounds.Max.Y);
		TestTrue(TEXT("Candidate compact theoretical surface diagnostic matches location Z"), FMath::IsNearlyEqual(Candidates[0].TheoreticalSurfaceZ, Candidates[0].TheoreticalLocation.Z));

		std::vector<FNodeLink> Nodes;
		UBiomeFastNoiseEditor* const Editor = CreateBiomeEditor(Nodes, const_cast<UBiomeStrategyData*>(SpawnBinding->RowBinding.Strategy.Get()));
		Editor->NoiseSlot = EBiomeNoiseSlot::GenA;
		Editor->BiomeTag = SpawnBinding->RowBinding.BiomeTag;
		const FNodeLink Terrain = Editor->GetNoiseRef(ChunkWorld);
		const FVector RawLocation = ChunkWorld->GetActorTransform().InverseTransformPosition(Candidates[0].TheoreticalLocation) / static_cast<double>(ScaleContext.BaseBlockSize);
		const FVector AuthoredLocation = RawLocation - ScaleContext.AuthoredOriginToRawBlockOffset;
		float DensityAtCandidate = 0.0f;
		TestTrue(TEXT("Reservation GenA resolves for theoretical surface verification"), Terrain.Node != nullptr);
		TestTrue(TEXT("Candidate theoretical location is solid in GenA"), EvaluateNoiseAtAuthoredBlock(Terrain, ScaleContext, AuthoredLocation, DensityAtCandidate) && DensityAtCandidate <= 0.0f);
		for (double Z = AuthoredLocation.Z + 1.0; Z <= SpawnBinding->Field.AuthoredMaxBlock.Z; Z += 1.0)
		{
			float AboveDensity = 0.0f;
			TestTrue(TEXT("No higher solid GenA pocket exists inside bounded reservation field"), EvaluateNoiseAtAuthoredBlock(Terrain, ScaleContext, FVector(AuthoredLocation.X, AuthoredLocation.Y, Z), AboveDensity) && AboveDensity > 0.0f);
		}
	}

	FChunkWorldSpawnRequest BoundedRequest = Request;
	BoundedRequest.PreferredSearchOrigin = ExpectedBounds.GetCenter();
	BoundedRequest.PreferredSearchRadius = 10.0f;
	TArray<FChunkWorldSpawnCandidate> BoundedCandidates;
	Provider->GatherSpawnCandidates(BoundedRequest, QueryContext, BoundedCandidates);
	const FBox RequestedWorldBounds(
		BoundedRequest.PreferredSearchOrigin - FVector(BoundedRequest.PreferredSearchRadius),
		BoundedRequest.PreferredSearchOrigin + FVector(BoundedRequest.PreferredSearchRadius));
	TestEqual(TEXT("Bounded request keeps its intersecting reservation field"), BoundedCandidates.Num(), 1);
	if (BoundedCandidates.Num() == 1)
	{
		const FBox& BoundedCandidateBounds = BoundedCandidates[0].SearchBounds;
		TestTrue(TEXT("Candidate minimum stays inside requested world XY bounds"), BoundedCandidateBounds.Min.X >= RequestedWorldBounds.Min.X && BoundedCandidateBounds.Min.Y >= RequestedWorldBounds.Min.Y);
		TestTrue(TEXT("Candidate maximum stays inside requested world XY bounds"), BoundedCandidateBounds.Max.X <= RequestedWorldBounds.Max.X && BoundedCandidateBounds.Max.Y <= RequestedWorldBounds.Max.Y);
	}

	ChunkWorldSpawnTest::DestroyWorld(World, WorldContext);
	FWorldGenScaleContextResolver::ClearCache();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldReservationSpawnSourceProviderRejectsOverflowedSearchBoundsTest,
	"PorismExtension.ChunkWorld.Spawn.ReservationProvider.RejectsOverflowedSearchBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies finite request fields whose corner addition overflows cannot reach authored-block conversion. */
bool FChunkWorldReservationSpawnSourceProviderRejectsOverflowedSearchBoundsTest::RunTest(const FString& Parameters)
{
	FWorldContext* WorldContext = nullptr;
	UWorld* const World = ChunkWorldSpawnTest::CreateWorld(WorldContext);
	if (!TestNotNull(TEXT("Transient authority world initializes"), World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AChunkWorldExtended* const ChunkWorld = World->SpawnActor<AChunkWorldExtended>(AChunkWorldExtended::StaticClass(), FTransform::Identity, SpawnParameters);
	if (!TestNotNull(TEXT("Chunk world spawns"), ChunkWorld))
	{
		ChunkWorldSpawnTest::DestroyWorld(World, WorldContext);
		return false;
	}

	ChunkWorld->WorldGenDef = CreateReservationSpawnWorldGenDef(*ChunkWorld);
	FWorldGenScaleContextResolver::ClearCache();
	UChunkWorldReservationSpawnSourceProvider* const Provider = NewObject<UChunkWorldReservationSpawnSourceProvider>(ChunkWorld);
	if (!TestNotNull(TEXT("Reservation provider initializes"), Provider))
	{
		ChunkWorldSpawnTest::DestroyWorld(World, WorldContext);
		return false;
	}

	using FVectorScalar = decltype(FVector::ZeroVector.X);
	FChunkWorldSpawnRequest Request;
	Request.AllowedSourceFamiliesInPriorityOrder.Add(EChunkWorldSpawnSourceFamily::ReservationField);
	Request.PreferredSearchOrigin = FVector(TNumericLimits<FVectorScalar>::Max(), 0.0, 0.0);
	Request.PreferredSearchRadius = TNumericLimits<float>::Max();

	FChunkWorldSpawnQueryContext QueryContext;
	QueryContext.ChunkWorld = ChunkWorld;
	QueryContext.MaximumCandidates = 8;
	TArray<FChunkWorldSpawnCandidate> Candidates;
	AddExpectedError(TEXT("Rejected reservation spawn query because request bounds could not convert"));
	Provider->GatherSpawnCandidates(Request, QueryContext, Candidates);
	TestTrue(TEXT("Overflowed finite request bounds produce no reservation query candidates"), Candidates.IsEmpty());

	ChunkWorldSpawnTest::DestroyWorld(World, WorldContext);
	FWorldGenScaleContextResolver::ClearCache();
	return true;
}
