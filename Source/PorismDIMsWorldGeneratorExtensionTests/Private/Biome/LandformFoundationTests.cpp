// Copyright 2026 Spotted Loaf Studio

#include "Biome/Noise/Foundation/LandformFoundationPayloads.h"
#include "Biome/Noise/Foundation/FoundationTerrainProfilePayloads.h"
#include "Biome/Noise/Foundation/InfinitePlaneFoundationPayloads.h"
#include "Biome/Noise/Island/IslandBiomeStrategyPayloads.h"
#include "Biome/Types/BiomeGameplayTags.h"
#include "Misc/AutomationTest.h"
#include <limits>
#include <vector>

namespace
{
	UBiomeStrategyData* MakeStrategy(const EFoundationProviderType Type)
	{
		auto* Strategy = NewObject<UBiomeStrategyData>();
		Strategy->bUseScaleOverride = true;
		Strategy->ScaleOverride.AxisBehaviorZ = EAxisBehavior::Infinity;
		Strategy->RootFoundationProvider.ProviderType = Type;
		Strategy->RootFoundationProvider.TerrainProfile.Reset();
		Strategy->RootFoundationProvider.BiomeTag = BiomeGameplayTags::Foundation.GetTag();
		Strategy->NormalizeProviderPayloadsForEditor();
		return Strategy;
	}

	struct FFields
	{
		std::vector<FNodeLink> Nodes;
		FResolvedWorldGenScaleContext Scale;
		FNodeLink Domain, Terrain;

		explicit FFields(UBiomeStrategyData* Strategy)
		{
			auto* Editor = NewObject<UFastNoiseEditor>();
			Editor->Nodes = &Nodes;
			Scale = Strategy->ResolveStrategyContext(nullptr).ScaleContext;
			Domain = Strategy->BuildBiomeNoise(Editor, nullptr, EBiomeNoiseSlot::DomainNoise, BiomeGameplayTags::Foundation.GetTag());
			Terrain = Strategy->BuildBiomeNoise(Editor, nullptr, EBiomeNoiseSlot::GenA, BiomeGameplayTags::Foundation.GetTag());
		}

		float At(const FNodeLink& Link, const FVector& Block, const int32 Seed = 37) const
		{
			if (!Link.Node || !Link.Node->BaseNode) return std::numeric_limits<float>::quiet_NaN();
			const FVector P = Scale.AuthoredBlockPositionToNoise(Block);
			return Link.Node->BaseNode->GenSingle3D(P.X, P.Y, P.Z, Seed);
		}

		bool Solid(const FVector& P) const { return At(Domain, P) > 0 && At(Terrain, P) < 0; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandformBridgeTest, "PorismExtension.Biome.Landforms.Bridge", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLandformBridgeTest::RunTest(const FString& Parameters)
{
	auto* S = MakeStrategy(EFoundationProviderType::ArchBridge);
	auto& P = S->RootFoundationProvider.ProviderPayload.GetMutable<FArchBridgeFoundationPayload>();
	P.DeckPoints = { FVector(-80, 0, 20), FVector(0, 0, 20), FVector(60, 60, 40) };
	TestFalse(TEXT("Connected bridge validates"), S->ValidateStrategy().HasErrors());
	FFields F(S);
	TestTrue(TEXT("First deck is solid"), F.Solid(FVector(-40, 0, 18)));
	TestTrue(TEXT("Sloping second deck is solid"), F.Solid(FVector(30, 30, 28)));
	TestTrue(TEXT("Bend has joined deck"), F.Solid(FVector(0, 0, 18)));
	TestTrue(TEXT("Arch crown leaves air below"), F.At(F.Terrain, FVector(-40, 0, 0)) > 0);
	TestTrue(TEXT("Arch foot extends below crown"), F.Solid(FVector(-75, 0, 0)));
	TestTrue(TEXT("Top face is air biased"), F.At(F.Terrain, FVector(-40, 0, 20)) > 0);
	TestFalse(TEXT("Beyond width is air"), F.Solid(FVector(-40, 20, 18)));
	TestFalse(TEXT("Beyond span is air"), F.Solid(FVector(-110, 0, 18)));

	// Same-tag child island at an endpoint must union with the deck, not carve its connection.
	FFoundationProviderDefinition Island;
	Island.BiomeTag = S->RootFoundationProvider.BiomeTag;
	FIslandFoundationShapePayload Shape;
	Shape.IslandBody.Center = FVector(-80, 0, 20);
	Shape.IslandBody.TopRadius = 25;
	Island.ProviderPayload.InitializeAs<FIslandFoundationShapePayload>(Shape);
	Island.TerrainProfile.InitializeAs<FFlatFoundationTerrainProfilePayload>();
	S->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make<FFoundationProviderDefinition>(Island));
	FFields Joined(S);
	TestTrue(TEXT("Endpoint joins island terrain"), Joined.Solid(FVector(-75, 0, 17)));
	TestTrue(TEXT("Island extends beyond bridge width"), Joined.Solid(FVector(-80, 18, 15)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandformMountainPitTest, "PorismExtension.Biome.Landforms.MountainAndCaldera", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLandformMountainPitTest::RunTest(const FString& Parameters)
{
	auto* S = MakeStrategy(EFoundationProviderType::Mountain);
	auto& P = S->RootFoundationProvider.ProviderPayload.GetMutable<FMountainFoundationPayload>();
	P.BaseRadiiBlocks = FVector2D(100, 60);
	P.HeightBlocks = 80;
	P.SlopePower = 1;
	FFields Before(S);
	TestTrue(TEXT("Peak is solid below summit"), Before.Solid(FVector(0, 0, 75)));
	TestTrue(TEXT("Mountain shoulder is solid"), Before.Solid(FVector(50, 0, 30)));
	TestFalse(TEXT("Slope excludes high shoulder air"), Before.Solid(FVector(50, 0, 50)));
	TestFalse(TEXT("Finite base excludes underground"), Before.Solid(FVector(0, 0, -2)));
	TestFalse(TEXT("Elliptical footprint excludes beyond Y radius"), Before.Solid(FVector(0, 70, 5)));

	auto* PitStrategy = MakeStrategy(EFoundationProviderType::PitVoid);
	auto& Pit = PitStrategy->RootFoundationProvider.ProviderPayload.GetMutable<FPitFoundationPayload>();
	Pit.Center = FVector(0, 0, 82);
	Pit.OpeningRadiiBlocks = FVector2D(30, 30);
	Pit.DepthBlocks = 40;
	Pit.RimNoiseAmplitudeBlocks = 0;
	S->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make<FFoundationProviderDefinition>(PitStrategy->RootFoundationProvider));
	TestFalse(TEXT("Caldera hierarchy validates"), S->ValidateStrategy().HasErrors());
	FFields After(S);
	TestFalse(TEXT("Pit removes summit"), After.Solid(FVector(0, 0, 75)));
	TestTrue(TEXT("Mountain below pit floor remains"), After.Solid(FVector(0, 0, 35)));
	TestTrue(TEXT("Outer mountain remains"), After.Solid(FVector(50, 0, 30)));
	TestTrue(TEXT("Pit only changes parent ownership, not its surface density"), FMath::IsNearlyEqual(Before.At(Before.Terrain, FVector(50, 0, 30)), After.At(After.Terrain, FVector(50, 0, 30))));

	S = MakeStrategy(EFoundationProviderType::InfinitePlane);
	Pit.Center = FVector::ZeroVector;
	S->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make<FFoundationProviderDefinition>(PitStrategy->RootFoundationProvider));
	FFields GroundPit(S);
	TestFalse(TEXT("Grounded pit removes shallow center"), GroundPit.Solid(FVector(0, 0, -5)));
	TestTrue(TEXT("Grounded pit retains deep floor"), GroundPit.Solid(FVector(0, 0, -45)));
	TestTrue(TEXT("Grounded pit retains outside ground"), GroundPit.Solid(FVector(40, 0, -5)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandformColumnTest, "PorismExtension.Biome.Landforms.ColumnAndMesa", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLandformColumnTest::RunTest(const FString& Parameters)
{
	auto* S = MakeStrategy(EFoundationProviderType::Column);
	auto& P = S->RootFoundationProvider.ProviderPayload.GetMutable<FColumnFoundationPayload>();
	P.WallNoiseAmplitudeBlocks = 0;
	P.BaseRadiiBlocks = FVector2D(30, 20);
	P.TopRadiiBlocks = FVector2D(10, 8);
	P.HeightBlocks = 60;
	FFields Column(S);
	TestTrue(TEXT("Column contains center"), Column.Solid(FVector(0, 0, 30)));
	TestTrue(TEXT("Wide column base is solid"), Column.Solid(FVector(22, 0, 5)));
	TestFalse(TEXT("Taper removes upper outside"), Column.Solid(FVector(22, 0, 55)));
	TestFalse(TEXT("Column has finite bottom"), Column.Solid(FVector(0, 0, -5)));
	P.BaseRadiiBlocks = FVector2D(90, 65);
	P.TopRadiiBlocks = FVector2D(75, 50);
	FFields Mesa(S);
	TestTrue(TEXT("Mesa has broad top"), Mesa.Solid(FVector(65, 0, 58)));
	TestFalse(TEXT("Mesa top face is air"), Mesa.Solid(FVector(65, 0, 60)));
	P.WallNoiseAmplitudeBlocks = 6;
	FFields Noisy(S);
	int32 Changed = 0;
	for (int32 Y = -45; Y <= 45; Y += 5)
	{
		const FVector Point(70, Y, 50);
		Changed += !FMath::IsNearlyEqual(Mesa.At(Mesa.Terrain, Point), Noisy.At(Noisy.Terrain, Point), 0.0001f);
		TestEqual(TEXT("Wall noise leaves analytic ownership unchanged inside envelope"), Mesa.At(Mesa.Domain, FVector(0, 0, 30)) > 0, Noisy.At(Noisy.Domain, FVector(0, 0, 30)) > 0);
	}
	TestTrue(TEXT("Wall noise changes actual density"), Changed > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandformProfilesScaleTest, "PorismExtension.Biome.Landforms.ProfilesAndScale", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLandformProfilesScaleTest::RunTest(const FString& Parameters)
{
	const EFoundationProviderType Types[] = { EFoundationProviderType::ArchBridge, EFoundationProviderType::Mountain, EFoundationProviderType::Column, EFoundationProviderType::PitVoid };
	const FInstancedStruct Profiles[] = {
		FInstancedStruct::Make<FNoisyFoundationTerrainProfilePayload>(), FInstancedStruct::Make<FRollingHillsFoundationTerrainProfilePayload>(),
		FInstancedStruct::Make<FErodedEdgeFoundationTerrainProfilePayload>(), FInstancedStruct::Make<FCreviceFoundationTerrainProfilePayload>(),
		FInstancedStruct::Make<FBowlFoundationTerrainProfilePayload>(), FInstancedStruct::Make<FRidgedFoundationTerrainProfilePayload>(), FInstancedStruct::Make<FTerracedFoundationTerrainProfilePayload>() };
	for (const auto Type : Types)
	{
		auto* S = MakeStrategy(Type);
		auto& Provider = S->RootFoundationProvider;
		// Pit emits no biome; put it in a plane to exercise its production carve domain.
		auto* Host = MakeStrategy(EFoundationProviderType::InfinitePlane);
		for (const auto& Profile : Profiles)
		{
			Provider.TerrainProfile = Profile;
			TestFalse(TEXT("Reusable surface profile validates for landform"), S->ValidateStrategy().HasErrors());
			Host->RootFoundationProvider.ChildFoundations = { FInstancedStruct::Make<FFoundationProviderDefinition>(Provider) };
			FFields Fields(Type == EFoundationProviderType::PitVoid ? Host : S);
			for (int32 I = -4; I <= 4; ++I)
			{
				const FVector Point(I * 8, 3, 4);
				TestTrue(TEXT("Profile graph produces finite domain and density"), FMath::IsFinite(Fields.At(Fields.Domain, Point)) && FMath::IsFinite(Fields.At(Fields.Terrain, Point)));
			}
		}
	}
	for (const auto Type : { EFoundationProviderType::ArchBridge, EFoundationProviderType::Mountain, EFoundationProviderType::Column })
	{
		auto* S = MakeStrategy(Type);
		if (auto* P = S->RootFoundationProvider.ProviderPayload.GetMutablePtr<FColumnFoundationPayload>()) P->WallNoiseAmplitudeBlocks = 0;
		FFields Original(S);
		S->ScaleOverride.BaseBlockSize = 50;
		S->ScaleOverride.NoiseScale = FVector(2, 3, 2);
		S->ScaleOverride.NoiseCoordinateOffset = FIntVector(1300, -450, 750);
		FFields Scaled(S);
		for (const FVector Point : { FVector(0, 0, -3), FVector(0, 0, 20), FVector(90, 0, 20), FVector(0, 90, 3) })
		{
			TestEqual(TEXT("Anisotropic scale/offset preserves analytic shape density"), Scaled.At(Scaled.Terrain, Point), Original.At(Original.Terrain, Point), 0.005f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandformNoiseTest, "PorismExtension.Biome.Landforms.SurfaceAndFloorNoise", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLandformNoiseTest::RunTest(const FString& Parameters)
{
	for (const auto Type : { EFoundationProviderType::ArchBridge, EFoundationProviderType::Mountain, EFoundationProviderType::Column, EFoundationProviderType::PitVoid })
	{
		auto* S = MakeStrategy(Type);
		auto* Host = MakeStrategy(EFoundationProviderType::InfinitePlane);
		auto BuildHost = [&]()
		{
			Host->RootFoundationProvider.ChildFoundations = { FInstancedStruct::Make<FFoundationProviderDefinition>(S->RootFoundationProvider) };
			return Type == EFoundationProviderType::PitVoid ? Host : S;
		};
		FFields Flat(BuildHost());
		S->RootFoundationProvider.TerrainProfile.InitializeAs<FNoisyFoundationTerrainProfilePayload>();
		FFields Noisy(BuildHost());
		int32 Changed = 0, SeedChanged = 0;
		for (int32 X = -24; X <= 24; X += 4)
		{
			for (int32 Y = -8; Y <= 8; Y += 4)
			{
				double Z = 0;
				if (Type == EFoundationProviderType::Mountain) Z = 90 * FMath::Pow(1 - FVector2D(X, Y).Size() / 80, 1.3);
				if (Type == EFoundationProviderType::Column) Z = 75;
				if (Type == EFoundationProviderType::PitVoid) Z = -30;
				const FVector Point(X, Y, Z);
				const FNodeLink A = Type == EFoundationProviderType::PitVoid ? Flat.Domain : Flat.Terrain;
				const FNodeLink B = Type == EFoundationProviderType::PitVoid ? Noisy.Domain : Noisy.Terrain;
				Changed += !FMath::IsNearlyEqual(Flat.At(A, Point), Noisy.At(B, Point), 0.0001f);
				SeedChanged += !FMath::IsNearlyEqual(Noisy.At(B, Point, 37), Noisy.At(B, Point, 91), 0.0001f);
				TestEqual(TEXT("Same seed repeats exact terrain"), Noisy.At(B, Point, 37), Noisy.At(B, Point, 37));
			}
		}
		TestTrue(TEXT("Surface or floor profile changes geometry"), Changed > 0);
		TestTrue(TEXT("Noise uses supplied world seed"), SeedChanged > 0);
		if (Type != EFoundationProviderType::PitVoid)
		{
			TestFalse(TEXT("Additive ownership does not evaluate surface or wall noise"), Noisy.Domain.Node->Flow.Contains(TEXT("OpenSimplex2")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandformValidationTest, "PorismExtension.Biome.Landforms.ValidationAndBounds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLandformValidationTest::RunTest(const FString& Parameters)
{
	for (const auto Type : { EFoundationProviderType::ArchBridge, EFoundationProviderType::Mountain, EFoundationProviderType::Column, EFoundationProviderType::PitVoid })
	{
		auto* S = MakeStrategy(Type);
		TestFalse(TEXT("Default normalized provider validates"), S->ValidateStrategy().HasErrors());
		FFoundationProviderBounds B;
		TestTrue(TEXT("Finite bounds are exposed"), UBiomeStrategyData::QueryProviderBounds(S->RootFoundationProvider, TEXT("Test"), B));
		TestTrue(TEXT("Bounds are finite"), !B.AuthoredMinBlock.ContainsNaN() && !B.AuthoredMaxBlock.ContainsNaN());
		if (Type != EFoundationProviderType::PitVoid)
		{
			FFields Fields(S);
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				FVector Outside = (B.AuthoredMinBlock + B.AuthoredMaxBlock) * 0.5;
				Outside[Axis] = B.AuthoredMaxBlock[Axis] + 1;
				TestTrue(TEXT("Domain ends within advertised upper bounds"), Fields.At(Fields.Domain, Outside) <= 0);
				Outside[Axis] = B.AuthoredMinBlock[Axis] - 1;
				TestTrue(TEXT("Domain ends within advertised lower bounds"), Fields.At(Fields.Domain, Outside) <= 0);
			}
		}
		TestEqual(TEXT("Unsupported surface queries are explicit"), UBiomeStrategyData::GetProviderQueryCapabilities(S->RootFoundationProvider).SurfaceQueryMode, EFoundationProviderSurfaceQueryMode::Unsupported);
		S->RootFoundationProvider.Reservations.AddDefaulted();
		TestTrue(TEXT("Unsupported reservations fail closed"), S->ValidateStrategy().HasErrors());
		S->RootFoundationProvider.Reservations.Reset();
		S->RootFoundationProvider.ProviderPayload.InitializeAs<FIslandFoundationShapePayload>();
		TestTrue(TEXT("Mismatched payload fails closed"), S->ValidateStrategy().HasErrors());
		S->NormalizeProviderPayloadsForEditor();
		TestFalse(TEXT("Editor repairs mismatched payload"), S->ValidateStrategy().HasErrors());
	}
	auto* S = MakeStrategy(EFoundationProviderType::ArchBridge);
	auto& Bridge = S->RootFoundationProvider.ProviderPayload.GetMutable<FArchBridgeFoundationPayload>();
	Bridge.DeckPoints[1] = Bridge.DeckPoints[0];
	TestTrue(TEXT("Zero horizontal span rejected"), S->ValidateStrategy().HasErrors());
	Bridge.DeckPoints[1] = FVector(100, 0, 0);
	Bridge.ArchRiseBlocks = std::numeric_limits<float>::quiet_NaN();
	TestTrue(TEXT("NaN dimensions rejected"), S->ValidateStrategy().HasErrors());
	S = MakeStrategy(EFoundationProviderType::PitVoid);
	TestEqual(TEXT("Editor forces pit to subtractive"), S->RootFoundationProvider.ContributionType, EFoundationContributionType::SubtractiveVoid);
	S->RootFoundationProvider.ContributionType = EFoundationContributionType::AdditiveBiome;
	TestTrue(TEXT("Manually malformed additive pit rejected"), S->ValidateStrategy().HasErrors());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandformMultiInstanceTest, "PorismExtension.Biome.Landforms.MultiInstance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLandformMultiInstanceTest::RunTest(const FString& Parameters)
{
	for (const auto Type : { EFoundationProviderType::ArchBridge, EFoundationProviderType::Mountain, EFoundationProviderType::Column, EFoundationProviderType::PitVoid })
	{
		auto* S = MakeStrategy(EFoundationProviderType::MultiInstance);
		auto* Single = MakeStrategy(Type);
		S->RootFoundationProvider.ContributionType = Single->RootFoundationProvider.ContributionType;
		auto& Multi = S->RootFoundationProvider.ProviderPayload.GetMutable<FMultiInstanceFoundationPayload>();
		FFoundationProviderPrototypeDefinition Prototype;
		Prototype.ProviderType = Type;
		Prototype.ProviderPayload = Single->RootFoundationProvider.ProviderPayload;
		Prototype.TerrainProfile = Single->RootFoundationProvider.TerrainProfile;
		Multi.PrototypeProvider.InitializeAs<FFoundationProviderPrototypeDefinition>(Prototype);
		Multi.InstanceCount = 3;
		Multi.SpacingBlocks = 500;
		Multi.MinUniformScale = Multi.MaxUniformScale = 1.5f;
		Multi.MinZOffsetBlocks = Multi.MaxZOffsetBlocks = 12;
		FResolvedBiomeStrategy Resolved, Again;
		TestTrue(TEXT("Repeated landforms resolve"), S->ResolveBiomeStrategy(nullptr, Resolved));
		TestTrue(TEXT("Repeated snapshot resolves again"), S->ResolveBiomeStrategy(nullptr, Again));
		TestEqual(TEXT("Three instances"), Resolved.ProviderInstances.Num(), 3);
		for (int32 I = 0; I < Resolved.ProviderInstances.Num(); ++I)
		{
			const auto& Instance = Resolved.ProviderInstances[I];
			TestTrue(TEXT("Repeated finite bounds remain valid"), Instance.Bounds.bValid);
			TestEqual(TEXT("Seeded bounds deterministic"), Instance.Bounds.AuthoredMinBlock, Again.ProviderInstances[I].Bounds.AuthoredMinBlock);
			if (const auto* P = Instance.ProviderSnapshot.ProviderPayload.GetPtr<FMountainFoundationPayload>())
			{
				TestEqual(TEXT("Height scaled"), P->HeightBlocks, 135.0f);
				TestEqual(TEXT("Vertical offset applied"), P->BaseCenter.Z, 12.0);
			}
		}
		Multi.SpacingBlocks = 10;
		TestTrue(TEXT("Requested no-overlap rejects intersecting repeated landforms"), S->ValidateStrategy().HasErrors());
		Multi.bRequireNoOverlap = false;
		TestFalse(TEXT("Intentional overlapping ranges are allowed"), S->ValidateStrategy().HasErrors());
		if (Type == EFoundationProviderType::Column)
		{
			auto& Repeated = Multi.PrototypeProvider.GetMutable<FFoundationProviderPrototypeDefinition>();
			Repeated.ProviderPayload.GetMutable<FColumnFoundationPayload>().BaseCenter.X = 500;
			Multi.bRequireNoOverlap = true;
			Multi.SpacingBlocks = 200;
			Multi.MinUniformScale = 1;
			Multi.MaxUniformScale = 2;
			TestTrue(TEXT("Worst-case overlap includes scaled off-origin centers"), S->ValidateStrategy().HasErrors());
			Multi.bRequireNoOverlap = false;
		}
		Multi.MinUniformScale = Multi.MaxUniformScale = 0.25f;
		TestTrue(TEXT("Small repeated landforms resolve"), S->ResolveBiomeStrategy(nullptr, Resolved));
		for (const auto& Instance : Resolved.ProviderInstances)
		{
			TestTrue(TEXT("Sub-block padding survives uniform downscale"), Instance.Bounds.bValid);
		}
	}
	return true;
}
