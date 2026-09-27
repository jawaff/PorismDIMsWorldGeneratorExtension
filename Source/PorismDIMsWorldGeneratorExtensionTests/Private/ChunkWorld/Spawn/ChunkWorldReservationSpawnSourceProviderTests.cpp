// Copyright 2026 Spotted Loaf Studio

#include "Biome/Island/IslandBiomeFastNoiseTestSupport.h"
#include "Biome/Noise/Strategy/BiomeStrategyBindingLibrary.h"
#include "Biome/Noise/WorldGenScaleContext.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Spawn/ChunkWorldReservationSpawnSourceProvider.h"
#include "ChunkWorld/Spawn/Support/ChunkWorldSpawnTestWorld.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "ChunkWorldStructs/ChunkWorldRuntimeStructs.h"
#include "FastNoise/Generators/BasicGenerators.h"
#include "Misc/AutomationTest.h"
#include <limits>

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
		FBiomeDualData Row = CreateStrategyBiomeRow(Strategy, TEXT("Reservation"), TestReservationBiomeTag());
		Row.DomainOver = 1.0f;
		Row.GenU_Mat1.AddDefaulted(); // An owning terrain row, not a density-only contribution.
		WorldGenDef->WorldBiomes.Add(Row);
		WorldGenDef->WorldGen = TEXT("AAAAAAAA"); // Constant zero: no additional world density.
		return WorldGenDef;
	}

	/** Owns compiled generation nodes without starting workers or creating voxel save files. */
	struct FScopedCompiledSpawnWorld
	{
		AChunkWorldExtended& World;
		TUniquePtr<FRuntimeWorldConfig> Config;

		explicit FScopedCompiledSpawnWorld(AChunkWorldExtended& InWorld) : World(InWorld)
		{
			SCacheKey Hash;
			Config.Reset(World.WorldGenDef->CompileRuntimeConfig(&World, World.Seed, true, Hash));
			World.RuntimeConfig = Config.Get();
		}

		~FScopedCompiledSpawnWorld() { World.RuntimeConfig = nullptr; }
	};

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

	FScopedCompiledSpawnWorld CompiledWorld(*ChunkWorld);
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
		TestTrue(TEXT("Reservation placement begins at its center"), FVector2D(Candidates[0].TheoreticalLocation).Equals(FVector2D(ExpectedBounds.GetCenter()), KINDA_SMALL_NUMBER));
		FChunkWorldSpawnRequest AnotherRequest = Request;
		AnotherRequest.RequestId = FGuid::NewGuid();
		TArray<FChunkWorldSpawnCandidate> AnotherCandidates;
		Provider->GatherSpawnCandidates(AnotherRequest, QueryContext, AnotherCandidates);
		TestTrue(TEXT("Another participant starts at the same area center before occupancy settlement"), AnotherCandidates.Num() == 1 && AnotherCandidates[0].TheoreticalLocation.Equals(Candidates[0].TheoreticalLocation));
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
		const FVector SolidCell(FMath::FloorToDouble(RawLocation.X), FMath::FloorToDouble(RawLocation.Y), RawLocation.Z - 1.0);
		const FVector AuthoredSolidCell = SolidCell - ScaleContext.AuthoredOriginToRawBlockOffset;
		TestTrue(TEXT("Cell below the published top face is solid"), EvaluateNoiseAtAuthoredBlock(Terrain, ScaleContext, AuthoredSolidCell, DensityAtCandidate) && DensityAtCandidate <= 0.0f);
		for (double Z = AuthoredLocation.Z; Z <= SpawnBinding->Field.AuthoredMaxBlock.Z; Z += 1.0)
		{
			float AboveDensity = 0.0f;
			TestTrue(TEXT("No higher solid GenA pocket exists inside bounded reservation field"), EvaluateNoiseAtAuthoredBlock(Terrain, ScaleContext, FVector(AuthoredSolidCell.X, AuthoredSolidCell.Y, Z), AboveDensity) && AboveDensity > 0.0f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldReservationSpawnCompiledTerrainTest,
	"PorismExtension.ChunkWorld.Spawn.ReservationProvider.CompiledTerrainSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The authored reservation chooses the region; the compiled world chooses solid cells and their top face. */
bool FChunkWorldReservationSpawnCompiledTerrainTest::RunTest(const FString& Parameters)
{
	FWorldContext* WorldContext = nullptr;
	UWorld* World = ChunkWorldSpawnTest::CreateWorld(WorldContext);
	if (!TestNotNull(TEXT("Authority world initializes"), World)) return false;
	AChunkWorldExtended* ChunkWorld = World->SpawnActor<AChunkWorldExtended>();
	ChunkWorld->WorldGenDef = CreateReservationSpawnWorldGenDef(*ChunkWorld);
	const FResolvedWorldGenScaleContext Scale = FWorldGenScaleContextResolver::Resolve(ChunkWorld, nullptr, false);
	TArray<FBiomeStrategyReservationFieldBinding> Bindings;
	UBiomeStrategyBindingLibrary::QueryWorldGenReservationFields(ChunkWorld, ChunkWorld->WorldGenDef, FBox(ForceInit), Bindings);
	const auto* Binding = Bindings.FindByPredicate([](const auto& Value) { return Value.Field.FieldKind == EReservationFieldKind::Spawn; });
	if (!TestNotNull(TEXT("Spawn field exists"), Binding))
	{
		ChunkWorldSpawnTest::DestroyWorld(World, WorldContext);
		return false;
	}

	// Supply the same compiled nodes consumed by native ShapeBatch, without generating or touching save files.
	FRuntimeWorldConfig Config;
	Config.BaseBlockSize = 100;
	Config.NoiseScale = FVector::OneVector;
	Config.NoiseCoordinateOffset = ChunkWorld->WorldGenDef->NoiseCoordinateOffset;
	const double SolidZ = FMath::FloorToDouble((Binding->Field.AuthoredMinBlock.Z + Binding->Field.AuthoredMaxBlock.Z) * 0.5) - 3.0;
	const float NoiseThreshold = Scale.AuthoredBlockPositionToNoise(FVector(0.0, 0.0, SolidZ + 0.25)).Z;
	auto Plane = FastNoise::New<FastNoise::PositionOutput>();
	Plane->Set<FastNoise::Dim::Z>(1.0f, -NoiseThreshold);
	auto Positive = FastNoise::New<FastNoise::Constant>();
	Positive->SetValue(1.0f);
	auto Negative = FastNoise::New<FastNoise::Constant>();
	Negative->SetValue(-1.0f);
	auto Zero = FastNoise::New<FastNoise::Constant>();
	Zero->SetValue(0.0f);
	FRuntimeBiome& Biome = Config.Biomes.AddDefaulted_GetRef();
	Biome.DomainNode = Positive;
	Biome.GenANode = Positive; // No solid cells in A; this world selects B.
	Biome.GenBNode = Plane;
	Biome.DualSwitchNode = Negative;
	Biome.DomainOver = 1.0f;
	Biome.Overlap = 0.0f;
	Biome.NoiseOnly = false;
	Config.WorldGenNode = Zero;
	ChunkWorld->RuntimeConfig = &Config;
	UChunkWorldReservationSpawnSourceProvider* Provider = NewObject<UChunkWorldReservationSpawnSourceProvider>(ChunkWorld);
	FChunkWorldSpawnRequest Request;
	Request.RequestId = FGuid(81, 82, 83, 84);
	Request.AllowedSourceFamiliesInPriorityOrder.Add(EChunkWorldSpawnSourceFamily::ReservationField);
	FChunkWorldSpawnQueryContext Query;
	Query.ChunkWorld = ChunkWorld;
	Query.MaximumCandidates = 1;
	for (const bool bGenOnly : {false, true})
	{
		Config.GenOnlyTestNode = bGenOnly ? FastNoise::SmartNode<>(Plane) : FastNoise::SmartNode<>();
		TArray<FChunkWorldSpawnCandidate> Candidates;
		Provider->GatherSpawnCandidates(Request, Query, Candidates);
		if (TestEqual(TEXT("Compiled terrain supplies a surface"), Candidates.Num(), 1))
		{
			const double ExpectedZ = Scale.AuthoredBlockPositionToRawBlock(FVector(0.0, 0.0, SolidZ + 1.0)).Z * Config.BaseBlockSize;
			TestEqual(bGenOnly ? TEXT("Gen-only world surface replaces reservation GenA") : TEXT("Selected GenB surface replaces reservation GenA"), Candidates[0].TheoreticalSurfaceZ, ExpectedZ);
		}
	}
	Config.GenOnlyTestNode = nullptr;
	auto WorldBias = FastNoise::New<FastNoise::Constant>();
	WorldBias->SetValue(0.02f); // Native additive density lowers this plane by two base blocks.
	Config.WorldGenNode = WorldBias;
	TArray<FChunkWorldSpawnCandidate> BiasedCandidates;
	Provider->GatherSpawnCandidates(Request, Query, BiasedCandidates);
	if (TestEqual(TEXT("World density still supplies a surface"), BiasedCandidates.Num(), 1))
	{
		const double ExpectedZ = Scale.AuthoredBlockPositionToRawBlock(FVector(0.0, 0.0, SolidZ - 1.0)).Z * Config.BaseBlockSize;
		TestEqual(TEXT("World density changes the predicted voxel top"), BiasedCandidates[0].TheoreticalSurfaceZ, ExpectedZ);
	}
	Config.GenOnlyTestNode = Positive;
	TArray<FChunkWorldSpawnCandidate> EmptyCandidates;
	Provider->GatherSpawnCandidates(Request, Query, EmptyCandidates);
	TestTrue(TEXT("All-air generated world cannot approve solid reservation GenA"), EmptyCandidates.IsEmpty());
	Config.GenOnlyTestNode = Negative;
	TArray<FChunkWorldSpawnCandidate> BuriedCandidates;
	Provider->GatherSpawnCandidates(Request, Query, BuriedCandidates);
	TestTrue(TEXT("Solid upper boundary cannot invent a surface inside the field"), BuriedCandidates.IsEmpty());

	// White noise gives a seed-sensitive column with an independent single-node oracle.
	Config.GenOnlyTestNode = FastNoise::New<FastNoise::White>();
	Request.PreferredSearchOrigin = FVector(25.0, 25.0, Scale.AuthoredBlockPositionToRawBlock(FVector(0.0, 0.0, SolidZ)).Z * 100.0);
	Request.PreferredSearchRadius = 1.0f; // Both XY samples stay in base cell (0,0).
	const double RawTopZ = FMath::FloorToDouble(Binding->Field.AuthoredMaxBlock.Z + Scale.AuthoredOriginToRawBlockOffset.Z);
	const double RawBottomZ = FMath::CeilToDouble(Binding->Field.AuthoredMinBlock.Z + Scale.AuthoredOriginToRawBlockOffset.Z);
	bool bObservedDifferentSeeds = false;
	double FirstExpectedZ = 0.0;
	for (int32 Seed = 0; Seed < 16; ++Seed)
	{
		ChunkWorld->Seed = Seed;
		double ExpectedZ = 0.0;
		bool bHasSurface = false;
		for (double RawZ = RawTopZ; RawZ >= RawBottomZ; --RawZ)
		{
			if (Config.GenOnlyTestNode->GenSingle3D(0.0f, 0.0f, static_cast<float>(RawZ) * (0.0001f * Config.BaseBlockSize), Seed) <= 0.0f)
			{
				bHasSurface = RawZ < RawTopZ;
				ExpectedZ = bHasSurface ? (RawZ + 1.0) * 100.0 : 0.0;
				break;
			}
		}
		if (Seed == 0) FirstExpectedZ = ExpectedZ;
		else bObservedDifferentSeeds |= ExpectedZ != FirstExpectedZ;
		TArray<FChunkWorldSpawnCandidate> SeedCandidates;
		Provider->GatherSpawnCandidates(Request, Query, SeedCandidates);
		if (TestEqual(TEXT("Seeded column eligibility matches native node"), SeedCandidates.Num(), bHasSurface ? 1 : 0) && bHasSurface)
			TestEqual(TEXT("World seed determines top face"), SeedCandidates[0].TheoreticalSurfaceZ, ExpectedZ);
	}
	TestTrue(TEXT("Fixture distinguishes world seeds"), bObservedDifferentSeeds);
	ChunkWorld->RuntimeConfig = nullptr;
	ChunkWorldSpawnTest::DestroyWorld(World, WorldContext);
	FWorldGenScaleContextResolver::ClearCache();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldReservationSpawnFacingTest,
	"PorismExtension.ChunkWorld.Spawn.ReservationProvider.Facing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldReservationSpawnFacingTest::RunTest(const FString& Parameters)
{
	FWorldContext* WorldContext = nullptr;
	UWorld* World = ChunkWorldSpawnTest::CreateWorld(WorldContext);
	if (!TestNotNull(TEXT("Authority world initializes"), World))
	{
		return false;
	}
	AChunkWorldExtended* ChunkWorld = World->SpawnActor<AChunkWorldExtended>();
	ChunkWorld->WorldGenDef = CreateReservationSpawnWorldGenDef(*ChunkWorld);
	FWorldGenScaleContextResolver::ClearCache();
	TArray<FBiomeStrategyReservationFieldBinding> Bindings;
	UBiomeStrategyBindingLibrary::QueryWorldGenReservationFields(ChunkWorld, ChunkWorld->WorldGenDef, FBox(ForceInit), Bindings);
	if (!TestFalse(TEXT("Strategy bindings exist"), Bindings.IsEmpty()))
	{
		ChunkWorldSpawnTest::DestroyWorld(World, WorldContext);
		return false;
	}
	UBiomeStrategyData* Strategy = const_cast<UBiomeStrategyData*>(Bindings[0].RowBinding.Strategy.Get());
	Strategy->RootFoundationProvider.Reservations[0].SpawnFacingPayload.GetMutable<FReservationSpawnRotationPayload>().Yaw = 30.0f;
	FReservationDefinition Second = Strategy->RootFoundationProvider.Reservations[0];
	Second.DebugName = TEXT("SecondFacing");
	Second.SpawnFacingPayload.GetMutable<FReservationSpawnRotationPayload>().Yaw = -60.0f;
	Strategy->RootFoundationProvider.Reservations.Add(Second);
	UChunkWorldReservationSpawnSourceProvider* Provider = NewObject<UChunkWorldReservationSpawnSourceProvider>(ChunkWorld);
	FScopedCompiledSpawnWorld CompiledWorld(*ChunkWorld);
	FChunkWorldSpawnRequest Request;
	Request.RequestId = FGuid(91, 92, 93, 94);
	Request.AllowedSourceFamiliesInPriorityOrder.Add(EChunkWorldSpawnSourceFamily::ReservationField);
	FChunkWorldSpawnQueryContext Query;
	Query.ChunkWorld = ChunkWorld;
	Query.MaximumCandidates = 8;
	ChunkWorld->SetActorRotation(FRotator(0.0, 90.0, 0.0));
	TArray<FChunkWorldSpawnCandidate> Candidates;
	Provider->GatherSpawnCandidates(Request, Query, Candidates);
	TestEqual(TEXT("Same-tag reservations stay distinct"), Candidates.Num(), 2);
	for (const FChunkWorldSpawnCandidate& Candidate : Candidates)
	{
		const double Expected = Candidate.SourceId.ToString().Contains(TEXT("SecondFacing")) ? 30.0 : 120.0;
		TestTrue(TEXT("Each reservation carries its own rotated heading"), FMath::IsNearlyEqual(Candidate.Facing.Yaw, Expected, 0.001));
	}
	Strategy->RootFoundationProvider.Reservations[0].SpawnFacingPayload.GetMutable<FReservationSpawnRotationPayload>().Yaw = std::numeric_limits<float>::quiet_NaN();
	Strategy->RootFoundationProvider.Reservations[1].SpawnFacingPayload.GetMutable<FReservationSpawnRotationPayload>().Yaw = std::numeric_limits<float>::infinity();
	Candidates.Reset();
	Provider->GatherSpawnCandidates(Request, Query, Candidates);
	TestTrue(TEXT("Non-finite authored headings produce no candidates"), Candidates.IsEmpty());
	Strategy->RootFoundationProvider.Reservations.SetNum(1);
	Strategy->RootFoundationProvider.Reservations[0].SpawnFacingPayload.GetMutable<FReservationSpawnRotationPayload>().Yaw = 0.0f;
	ChunkWorld->SetActorRotation(FRotator(90.0, 0.0, 0.0));
	Provider->GatherSpawnCandidates(Request, Query, Candidates);
	TestTrue(TEXT("Vertical facing cannot manufacture an upright yaw"), Candidates.IsEmpty());

	ChunkWorld->SetActorTransform(FTransform(FRotator(0.0, 90.0, 0.0), FVector(120.0, -75.0, 300.0), FVector(2.0)));
	auto& Reservation = Strategy->RootFoundationProvider.Reservations[0];
	Reservation.SpawnFacingMode = EReservationSpawnFacing::FixedPoint;
	Provider->GatherSpawnCandidates(Request, Query, Candidates);
	TestTrue(TEXT("Mismatched runtime payload is rejected"), Candidates.IsEmpty());
	Reservation.SpawnFacingPayload.InitializeAs<FReservationSpawnPointPayload>();
	const FVector Target(17.0, -31.0, 9.0);
	Reservation.SpawnFacingPayload.GetMutable<FReservationSpawnPointPayload>().Target = Target;
	Provider->GatherSpawnCandidates(Request, Query, Candidates);
	TestEqual(TEXT("Fixed point produces candidate"), Candidates.Num(), 1);
	if (!Candidates.IsEmpty())
	{
		const auto Scale = FWorldGenScaleContextResolver::Resolve(ChunkWorld, nullptr, false);
		const FVector Expected = ChunkWorld->GetActorTransform().TransformPosition(Scale.AuthoredBlockPositionToRawBlock(Target) * Scale.BaseBlockSize);
		TestTrue(TEXT("Target uses world translation rotation and scale exactly once"), Candidates[0].Facing.Target.Equals(Expected));
		TestTrue(TEXT("Exact source retains target mode"), Candidates[0].Facing.bFaceTarget);
	}
	Reservation.SpawnFacingPayload.GetMutable<FReservationSpawnPointPayload>().Target.X = std::numeric_limits<double>::infinity();
	Candidates.Reset();
	Provider->GatherSpawnCandidates(Request, Query, Candidates);
	TestTrue(TEXT("Nonfinite target produces no candidates"), Candidates.IsEmpty());
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

	FScopedCompiledSpawnWorld CompiledWorld(*ChunkWorld);
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
