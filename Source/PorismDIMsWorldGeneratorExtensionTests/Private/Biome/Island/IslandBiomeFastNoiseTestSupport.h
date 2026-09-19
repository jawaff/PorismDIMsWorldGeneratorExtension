// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Biome/Noise/Island/IslandBiomeFastNoiseLibrary.h"
#include "Biome/Noise/Foundation/FoundationTerrainProfilePayloads.h"
#include "Biome/Noise/Foundation/InfinitePlaneFoundationPayloads.h"
#include "Biome/Noise/Foundation/VoidFoundationPayloads.h"
#include "Biome/Noise/Island/IslandBiomeStrategyPayloads.h"
#include "Biome/Noise/WorldGenScaleContext.h"
#include "Biome/Noise/Reservation/SurfaceAnchorReservationPayloads.h"
#include "Biome/Noise/Strategy/BiomeStrategyBindingLibrary.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "Biome/Noise/Strategy/BiomeStrategyData.h"
#include "Biome/Types/BiomeGameplayTags.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Misc/AutomationTest.h"
#include "Templates/UnrealTypeTraits.h"

#include <vector>

namespace
{
	UFastNoiseEditor* CreateTestFastNoiseEditor(std::vector<FNodeLink>& Nodes)
	{
		UFastNoiseEditor* const Editor = NewObject<UFastNoiseEditor>(GetTransientPackage());
		Editor->Nodes = &Nodes;
		return Editor;
	}

	UBiomeFastNoiseEditor* CreateBiomeEditor(std::vector<FNodeLink>& Nodes, UBiomeStrategyData* Strategy)
	{
		UBiomeFastNoiseEditor* const Editor = NewObject<UBiomeFastNoiseEditor>(GetTransientPackage());
		Editor->Nodes = &Nodes;
		Editor->Strategy = Strategy;
		return Editor;
	}

	UBiomeFastNoiseEditor* CreateBiomeSlotEditor(
		UBiomeStrategyData* Strategy,
		const EBiomeNoiseSlot NoiseSlot,
		const FGameplayTag BiomeTag)
	{
		UBiomeFastNoiseEditor* const Editor = NewObject<UBiomeFastNoiseEditor>(GetTransientPackage());
		Editor->Strategy = Strategy;
		Editor->NoiseSlot = NoiseSlot;
		Editor->BiomeTag = BiomeTag;
		return Editor;
	}

	FGameplayTag TestFoundationBiomeTag()
	{
		return BiomeGameplayTags::Foundation.GetTag();
	}

	FGameplayTag TestReservationBiomeTag()
	{
		return BiomeGameplayTags::Reservation.GetTag();
	}

	FBiomeDualData CreateStrategyBiomeRow(
		UBiomeStrategyData* Strategy,
		const FString& BiomeName,
		const FGameplayTag BiomeTag)
	{
		FBiomeDualData Row;
		Row.BiomeName = BiomeName;
		Row.DomainRun = CreateBiomeSlotEditor(Strategy, EBiomeNoiseSlot::DomainNoise, BiomeTag);
		Row.GenARun = CreateBiomeSlotEditor(Strategy, EBiomeNoiseSlot::GenA, BiomeTag);
		return Row;
	}

	UBiomeStrategyData* CreateMainMenuIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = NewObject<UBiomeStrategyData>(GetTransientPackage());
		Strategy->bUseScaleOverride = true;

		FIslandFoundationShapePayload IslandPayload;
		IslandPayload.IslandBody.Center = FVector::ZeroVector;
		IslandPayload.IslandBody.TopRadius = 120.0f;
		IslandPayload.IslandBody.TopHeight = 30.0f;
		IslandPayload.IslandBody.BottomDepth = 95.0f;
		IslandPayload.IslandBody.RimThickness = 20.0f;
		IslandPayload.BodyDetail.RimNoiseAmplitude = 8.0f;
		IslandPayload.BodyDetail.UndersideNoiseAmplitude = 6.0f;
		IslandPayload.DomainRadiusPadding = 12.0f;
		IslandPayload.DomainVerticalPadding = 12.0f;
		Strategy->RootFoundationProvider.DebugName = TEXT("Root");
		Strategy->RootFoundationProvider.ContributionType = EFoundationContributionType::AdditiveBiome;
		Strategy->RootFoundationProvider.ProviderType = EFoundationProviderType::Island;
		Strategy->RootFoundationProvider.BiomeTag = TestFoundationBiomeTag();
		FNoisyFoundationTerrainProfilePayload NoisyProfile;
		NoisyProfile.SurfaceNoiseAmplitude = 3.0f;
		Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FNoisyFoundationTerrainProfilePayload>(NoisyProfile);
		Strategy->RootFoundationProvider.ProviderPayload.InitializeAs<FIslandFoundationShapePayload>(IslandPayload);

		FReservationDefinition CenterReservation;
		CenterReservation.DebugName = TEXT("Center");
		CenterReservation.BiomeTag = TestReservationBiomeTag();
		CenterReservation.bEnableSpawnReservation = true;

		FSurfaceAnchorReservationPayload ReservationPayload;
		ReservationPayload.CenterXY = FVector2D::ZeroVector;
		ReservationPayload.Shape = ESurfaceAnchorReservationShape::Sphere;
		ReservationPayload.SphereRadii = FVector(35.0, 35.0, 18.0);
		ReservationPayload.SpawnSphereRadii = FVector(18.0, 18.0, 10.0);
		CenterReservation.ReservationPayload.InitializeAs<FSurfaceAnchorReservationPayload>(ReservationPayload);

		Strategy->RootFoundationProvider.Reservations.Add(CenterReservation);

		return Strategy;
	}

	UBiomeStrategyData* CreateNoDetailMainMenuIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateMainMenuIslandStrategy();
		Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FFlatFoundationTerrainProfilePayload>();
		if (FIslandFoundationShapePayload* IslandPayload = Strategy->RootFoundationProvider.ProviderPayload.GetMutablePtr<FIslandFoundationShapePayload>())
		{
			IslandPayload->BodyDetail.RimNoiseAmplitude = 0.0f;
			IslandPayload->BodyDetail.UndersideNoiseAmplitude = 0.0f;
		}

		return Strategy;
	}

	FFoundationProviderDefinition CreateNoDetailIslandProvider(
		const FName DebugName,
		const FGameplayTag BiomeTag,
		const FVector Center,
		const float Radius = 35.0f)
	{
		FIslandFoundationShapePayload IslandPayload;
		IslandPayload.IslandBody.Center = Center;
		IslandPayload.IslandBody.TopRadius = Radius;
		IslandPayload.IslandBody.TopHeight = 12.0f;
		IslandPayload.IslandBody.BottomDepth = 24.0f;
		IslandPayload.IslandBody.RimThickness = 8.0f;
		IslandPayload.BodyDetail.RimNoiseAmplitude = 0.0f;
		IslandPayload.BodyDetail.UndersideNoiseAmplitude = 0.0f;
		IslandPayload.DomainRadiusPadding = 4.0f;
		IslandPayload.DomainVerticalPadding = 4.0f;

		FFoundationProviderDefinition Provider;
		Provider.DebugName = DebugName;
		Provider.ContributionType = EFoundationContributionType::AdditiveBiome;
		Provider.BiomeTag = BiomeTag;
		Provider.ProviderType = EFoundationProviderType::Island;
		Provider.TerrainProfile.InitializeAs<FFlatFoundationTerrainProfilePayload>();
		Provider.ProviderPayload.InitializeAs<FIslandFoundationShapePayload>(IslandPayload);
		return Provider;
	}

	FFoundationProviderPrototypeDefinition CreateNoDetailIslandPrototype(
		const FVector Center,
		const float Radius = 35.0f)
	{
		const FFoundationProviderDefinition Provider = CreateNoDetailIslandProvider(TEXT("Prototype"), TestFoundationBiomeTag(), Center, Radius);

		FFoundationProviderPrototypeDefinition Prototype;
		Prototype.ProviderType = Provider.ProviderType;
		Prototype.ProviderPayload = Provider.ProviderPayload;
		Prototype.TerrainProfile = Provider.TerrainProfile;
		Prototype.Reservations = Provider.Reservations;
		Prototype.ChildFoundations = Provider.ChildFoundations;
		return Prototype;
	}

	FReservationDefinition CreateProviderLocalBoxReservation(const FName DebugName, const FGameplayTag BiomeTag)
	{
		FSurfaceAnchorReservationPayload Payload;
		Payload.CenterXY = FVector2D::ZeroVector;
		Payload.Shape = ESurfaceAnchorReservationShape::Box;
		Payload.BoxHalfExtent = FVector(10.0f, 10.0f, 6.0f);

		FReservationDefinition Reservation;
		Reservation.DebugName = DebugName;
		Reservation.BiomeTag = BiomeTag;
		Reservation.ReservationType = EReservationType::SurfaceAnchor;
		Reservation.ReservationPayload.InitializeAs<FSurfaceAnchorReservationPayload>(Payload);
		return Reservation;
	}

	UBiomeStrategyData* CreateNoReservationHierarchyStrategy()
	{
		UBiomeStrategyData* const Strategy = NewObject<UBiomeStrategyData>(GetTransientPackage());
		Strategy->bUseScaleOverride = true;
		Strategy->RootFoundationProvider = CreateNoDetailIslandProvider(TEXT("Root"), TestFoundationBiomeTag(), FVector::ZeroVector, 60.0f);
		return Strategy;
	}

	UBiomeStrategyData* CreateOffsetRootReservationStrategy(const bool bAddChildFoundation)
	{
		UBiomeStrategyData* const Strategy = NewObject<UBiomeStrategyData>(GetTransientPackage());
		Strategy->bUseScaleOverride = true;
		Strategy->RootFoundationProvider = CreateNoDetailIslandProvider(TEXT("Root"), TestFoundationBiomeTag(), FVector(40.0f, -25.0f, 12.0f), 60.0f);
		Strategy->RootFoundationProvider.Reservations.Add(CreateProviderLocalBoxReservation(TEXT("Center"), TestReservationBiomeTag()));

		if (bAddChildFoundation)
		{
			const FFoundationProviderDefinition ChildProvider = CreateNoDetailIslandProvider(TEXT("Child"), TestFoundationBiomeTag(), FVector(150.0f, 0.0f, 0.0f), 24.0f);
			Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(ChildProvider));
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateSharedReservationBiomeHierarchyStrategy()
	{
		UBiomeStrategyData* const Strategy = NewObject<UBiomeStrategyData>(GetTransientPackage());
		Strategy->bUseScaleOverride = true;
		Strategy->RootFoundationProvider = CreateNoDetailIslandProvider(TEXT("Root"), TestFoundationBiomeTag(), FVector::ZeroVector, 60.0f);
		Strategy->RootFoundationProvider.Reservations.Add(CreateProviderLocalBoxReservation(TEXT("RootCenter"), TestReservationBiomeTag()));

		FFoundationProviderDefinition ChildProvider = CreateNoDetailIslandProvider(TEXT("Child"), TestFoundationBiomeTag(), FVector(160.0f, 0.0f, 0.0f), 35.0f);
		FReservationDefinition ChildReservation = CreateProviderLocalBoxReservation(TEXT("ChildCenter"), TestReservationBiomeTag());
		if (FSurfaceAnchorReservationPayload* const Payload = ChildReservation.ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
		{
			Payload->SurfaceZLift = 5.0f;
		}
		ChildProvider.Reservations.Add(ChildReservation);
		Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(ChildProvider));

		return Strategy;
	}

	UBiomeStrategyData* CreateLineMultiInstanceFoundationStrategy(const float SpacingBlocks = 220.0f)
	{
		FFoundationProviderPrototypeDefinition PrototypeProvider = CreateNoDetailIslandPrototype(FVector::ZeroVector, 35.0f);
		PrototypeProvider.Reservations.Add(CreateProviderLocalBoxReservation(TEXT("Center"), TestReservationBiomeTag()));

		FMultiInstanceFoundationPayload MultiPayload;
		MultiPayload.PrototypeProvider.InitializeAs<FFoundationProviderPrototypeDefinition>(PrototypeProvider);
		MultiPayload.Arrangement = EMultiInstanceFoundationArrangement::Line;
		MultiPayload.InstanceCount = 2;
		MultiPayload.SpacingBlocks = SpacingBlocks;

		UBiomeStrategyData* const Strategy = NewObject<UBiomeStrategyData>(GetTransientPackage());
		Strategy->bUseScaleOverride = true;
		Strategy->RootFoundationProvider.DebugName = TEXT("IslandLine");
		Strategy->RootFoundationProvider.ContributionType = EFoundationContributionType::AdditiveBiome;
		Strategy->RootFoundationProvider.BiomeTag = TestFoundationBiomeTag();
		Strategy->RootFoundationProvider.ProviderType = EFoundationProviderType::MultiInstance;
		Strategy->RootFoundationProvider.ProviderPayload.InitializeAs<FMultiInstanceFoundationPayload>(MultiPayload);
		return Strategy;
	}

	UBiomeStrategyData* CreateLineMultiInstanceWithFixedCenterChildStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateLineMultiInstanceFoundationStrategy();
		FFoundationProviderDefinition CenterProvider = CreateNoDetailIslandProvider(TEXT("FixedCenter"), TestFoundationBiomeTag(), FVector::ZeroVector, 24.0f);
		CenterProvider.Reservations.Add(CreateProviderLocalBoxReservation(TEXT("FixedCenterReservation"), TestReservationBiomeTag()));
		Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(CenterProvider));
		return Strategy;
	}

	UBiomeStrategyData* CreateLineMultiInstanceSurfaceAnchorStrategy(const float SpacingBlocks = 80.0f)
	{
		UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();

		FSurfaceAnchorReservationPayload PrototypePayload;
		PrototypePayload.CenterXY = FVector2D::ZeroVector;
		PrototypePayload.Shape = ESurfaceAnchorReservationShape::Box;
		PrototypePayload.BoxHalfExtent = FVector(10.0f, 10.0f, 6.0f);

		FMultiInstanceSurfaceAnchorReservationPayload MultiPayload;
		MultiPayload.PrototypeSurfaceAnchor.InitializeAs<FSurfaceAnchorReservationPayload>(PrototypePayload);
		MultiPayload.Arrangement = EMultiInstanceFoundationArrangement::Line;
		MultiPayload.InstanceCount = 2;
		MultiPayload.SpacingBlocks = SpacingBlocks;

		FReservationDefinition Reservation;
		Reservation.DebugName = TEXT("AnchorLine");
		Reservation.BiomeTag = TestReservationBiomeTag();
		Reservation.ReservationType = EReservationType::MultiInstanceSurfaceAnchor;
		Reservation.ReservationPayload.InitializeAs<FMultiInstanceSurfaceAnchorReservationPayload>(MultiPayload);
		Strategy->RootFoundationProvider.Reservations.Add(Reservation);
		return Strategy;
	}

	UBiomeStrategyData* CreateInfinitePlaneSurfaceAnchorStrategy()
	{
		UBiomeStrategyData* const Strategy = NewObject<UBiomeStrategyData>(GetTransientPackage());
		Strategy->bUseScaleOverride = true;

		FInfinitePlaneFoundationPayload PlanePayload;
		PlanePayload.SurfaceZ = 0.0f;
		PlanePayload.DomainTopPadding = 32.0f;
		PlanePayload.DomainBottomDepth = 96.0f;

		Strategy->RootFoundationProvider.DebugName = TEXT("InfinitePlane");
		Strategy->RootFoundationProvider.ContributionType = EFoundationContributionType::AdditiveBiome;
		Strategy->RootFoundationProvider.BiomeTag = TestFoundationBiomeTag();
		Strategy->RootFoundationProvider.ProviderType = EFoundationProviderType::InfinitePlane;
		Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FFlatFoundationTerrainProfilePayload>();
		Strategy->RootFoundationProvider.ProviderPayload.InitializeAs<FInfinitePlaneFoundationPayload>(PlanePayload);

		FSurfaceAnchorReservationPayload PrototypePayload;
		PrototypePayload.CenterXY = FVector2D::ZeroVector;
		PrototypePayload.Shape = ESurfaceAnchorReservationShape::Box;
		PrototypePayload.BoxHalfExtent = FVector(12.0f, 12.0f, 6.0f);

		FReservationDefinition Reservation;
		Reservation.DebugName = TEXT("Fixed");
		Reservation.BiomeTag = TestReservationBiomeTag();
		Reservation.ReservationType = EReservationType::SurfaceAnchor;
		Reservation.ReservationPayload.InitializeAs<FSurfaceAnchorReservationPayload>(PrototypePayload);
		Strategy->RootFoundationProvider.Reservations.Add(Reservation);

		return Strategy;
	}

	FFoundationProviderDefinition CreateInfinitePlaneProvider(
		const FName DebugName,
		const FGameplayTag BiomeTag,
		const FInstancedStruct& DomainMask)
	{
		FInfinitePlaneFoundationPayload PlanePayload;
		PlanePayload.SurfaceZ = 0.0f;
		PlanePayload.DomainTopPadding = 32.0f;
		PlanePayload.DomainBottomDepth = 96.0f;
		if (DomainMask.IsValid())
		{
			PlanePayload.DomainMask = DomainMask;
		}

		FFoundationProviderDefinition Provider;
		Provider.DebugName = DebugName;
		Provider.ContributionType = EFoundationContributionType::AdditiveBiome;
		Provider.BiomeTag = BiomeTag;
		Provider.ProviderType = EFoundationProviderType::InfinitePlane;
		Provider.TerrainProfile.InitializeAs<FFlatFoundationTerrainProfilePayload>();
		Provider.ProviderPayload.InitializeAs<FInfinitePlaneFoundationPayload>(PlanePayload);
		return Provider;
	}

	FInstancedStruct CreateRepeatedHolesDomainMask(const bool bInvert)
	{
		FInfiniteFoundationRepeatedHolesDomainMaskPayload RepeatedHoles;
		RepeatedHoles.CellSizeBlocks = 80.0f;
		RepeatedHoles.HoleRadiusBlocks = 24.0f;
		RepeatedHoles.EdgeSoftnessBlocks = 0.0f;
		RepeatedHoles.SeedOffset = 3;
		RepeatedHoles.bInvert = bInvert;
		return FInstancedStruct::Make(RepeatedHoles);
	}

	bool EvaluateNoiseAtAuthoredBlock(
		const FNodeLink Node,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const FVector& AuthoredBlockPosition,
		float& OutValue);

	bool FindPositiveAndNegativeDomainSamples(
		const FNodeLink Domain,
		const FResolvedWorldGenScaleContext& ScaleContext,
		FVector& OutPositiveBlock,
		FVector& OutNegativeBlock)
	{
		bool bFoundPositive = false;
		bool bFoundNegative = false;
		for (float X = -240.0f; X <= 240.0f && (!bFoundPositive || !bFoundNegative); X += 8.0f)
		{
			for (float Y = -240.0f; Y <= 240.0f && (!bFoundPositive || !bFoundNegative); Y += 8.0f)
			{
				const FVector Sample(X, Y, -10.0f);
				float Value = 0.0f;
				if (!EvaluateNoiseAtAuthoredBlock(Domain, ScaleContext, Sample, Value))
				{
					continue;
				}

				if (!bFoundPositive && Value > 0.0f)
				{
					OutPositiveBlock = Sample;
					bFoundPositive = true;
				}
				if (!bFoundNegative && Value < 0.0f)
				{
					OutNegativeBlock = Sample;
					bFoundNegative = true;
				}
			}
		}

		return bFoundPositive && bFoundNegative;
	}

	UBiomeStrategyData* CreateIslandWithChildMultiInstanceFoundationStrategy()
	{
		UBiomeStrategyData* const Strategy = NewObject<UBiomeStrategyData>(GetTransientPackage());
		Strategy->bUseScaleOverride = true;
		Strategy->RootFoundationProvider = CreateNoDetailIslandProvider(TEXT("Root"), TestFoundationBiomeTag(), FVector::ZeroVector, 45.0f);

		FFoundationProviderPrototypeDefinition PrototypeProvider = CreateNoDetailIslandPrototype(FVector::ZeroVector, 24.0f);
		PrototypeProvider.Reservations.Add(CreateProviderLocalBoxReservation(TEXT("Center"), TestReservationBiomeTag()));

		FMultiInstanceFoundationPayload MultiPayload;
		MultiPayload.PrototypeProvider.InitializeAs<FFoundationProviderPrototypeDefinition>(PrototypeProvider);
		MultiPayload.Arrangement = EMultiInstanceFoundationArrangement::Line;
		MultiPayload.ArrangementCenterXY = FVector2D(180.0f, 0.0f);
		MultiPayload.InstanceCount = 2;
		MultiPayload.SpacingBlocks = 100.0f;

		FFoundationProviderDefinition ChildMultiProvider;
		ChildMultiProvider.DebugName = TEXT("ChildLine");
		ChildMultiProvider.ContributionType = EFoundationContributionType::AdditiveBiome;
		ChildMultiProvider.BiomeTag = TestFoundationBiomeTag();
		ChildMultiProvider.ProviderType = EFoundationProviderType::MultiInstance;
		ChildMultiProvider.ProviderPayload.InitializeAs<FMultiInstanceFoundationPayload>(MultiPayload);
		Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(ChildMultiProvider));

		return Strategy;
	}

	UBiomeStrategyData* CreateOffsetSurfaceAnchorIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateMainMenuIslandStrategy();
		FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0
			? &Strategy->RootFoundationProvider.Reservations[0]
			: nullptr;
		if (Reservation != nullptr)
		{
			FSurfaceAnchorReservationPayload AnchorPayload;
			AnchorPayload.CenterXY = FVector2D::ZeroVector;
			AnchorPayload.Shape = ESurfaceAnchorReservationShape::Sphere;
			AnchorPayload.SphereRadii = FVector(35.0, 35.0, 12.0);
			AnchorPayload.SurfaceZOffset = -50.0f;
			Reservation->ReservationPayload.InitializeAs<FSurfaceAnchorReservationPayload>(AnchorPayload);
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateZeroSurfaceMenuIslandStrategy()
	{
		return CreateNoDetailMainMenuIslandStrategy();
	}

	UBiomeStrategyData* CreateBoxAnchorIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateMainMenuIslandStrategy();
		FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0
			? &Strategy->RootFoundationProvider.Reservations[0]
			: nullptr;
		if (Reservation != nullptr)
		{
			FSurfaceAnchorReservationPayload BoxPayload;
			BoxPayload.CenterXY = FVector2D(10.0f, -20.0f);
			BoxPayload.Shape = ESurfaceAnchorReservationShape::Box;
			BoxPayload.BoxHalfExtent = FVector(50.0, 30.0, 12.0);
			BoxPayload.SpawnBoxHalfExtent = FVector(25.0, 15.0, 8.0);
			Reservation->ReservationPayload.InitializeAs<FSurfaceAnchorReservationPayload>(BoxPayload);
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateDefaultBoxAnchorIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
		FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0
			? &Strategy->RootFoundationProvider.Reservations[0]
			: nullptr;
		if (Reservation != nullptr)
		{
			FSurfaceAnchorReservationPayload BoxPayload;
			BoxPayload.CenterXY = FVector2D::ZeroVector;
			BoxPayload.Shape = ESurfaceAnchorReservationShape::Box;
			BoxPayload.BoxHalfExtent = FVector(35.0, 35.0, 18.0);
			Reservation->ReservationPayload.InitializeAs<FSurfaceAnchorReservationPayload>(BoxPayload);
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateNoisyReservationSurfaceIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
		FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0
			? &Strategy->RootFoundationProvider.Reservations[0]
			: nullptr;
		if (Reservation != nullptr)
		{
			if (FSurfaceAnchorReservationPayload* const Payload = Reservation->ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
			{
				FSurfaceAnchorNoisyTerrainPayload NoisyTerrain;
				NoisyTerrain.SurfaceNoiseAmplitude = 6.0f;
				NoisyTerrain.SurfaceNoiseScale = 4.0f;
				Payload->TerrainPayload.InitializeAs<FSurfaceAnchorNoisyTerrainPayload>(NoisyTerrain);
			}
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateBlendedSupportReservationSurfaceIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateDefaultBoxAnchorIslandStrategy();
		FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0
			? &Strategy->RootFoundationProvider.Reservations[0]
			: nullptr;
		if (Reservation != nullptr)
		{
			if (FSurfaceAnchorReservationPayload* const Payload = Reservation->ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
			{
				Payload->SurfaceZOffset = 12.0f;
				FSurfaceAnchorBlendedSupportTerrainPayload BlendedTerrain;
				BlendedTerrain.EdgeBlendWidthBlocks = 10.0f;
				Payload->TerrainPayload.InitializeAs<FSurfaceAnchorBlendedSupportTerrainPayload>(BlendedTerrain);
			}
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateTerracedReservationSurfaceIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateNoisyReservationSurfaceIslandStrategy();
		FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0
			? &Strategy->RootFoundationProvider.Reservations[0]
			: nullptr;
		if (Reservation != nullptr)
		{
			if (FSurfaceAnchorReservationPayload* const Payload = Reservation->ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
			{
				FSurfaceAnchorNoisyTerrainPayload* const NoisyTerrain = Payload->TerrainPayload.GetMutablePtr<FSurfaceAnchorNoisyTerrainPayload>();
				if (NoisyTerrain != nullptr)
				{
					NoisyTerrain->bUseTerraces = true;
					NoisyTerrain->TerraceStepHeight = 2.0f;
				}
			}
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateThreeBlockRaisedReservationSurfaceIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
		FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0
			? &Strategy->RootFoundationProvider.Reservations[0]
			: nullptr;
		if (Reservation != nullptr)
		{
			if (FSurfaceAnchorReservationPayload* const Payload = Reservation->ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
			{
				Payload->SurfaceZOffset = 3.0f;
			}
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateNoisyFoundationSurfaceSolveStrategy(const ESurfaceAnchorHeightSolveMode SolveMode, const float SurfaceOffsetBlocks = 0.0f)
	{
		UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
		FNoisyFoundationTerrainProfilePayload NoisyProfile;
		NoisyProfile.SurfaceNoiseAmplitude = 10.0f;
		NoisyProfile.SurfaceNoiseScale = 4.0f;
		Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FNoisyFoundationTerrainProfilePayload>(NoisyProfile);

		if (FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0 ? &Strategy->RootFoundationProvider.Reservations[0] : nullptr)
		{
			FSurfaceAnchorReservationPayload BoxPayload;
			BoxPayload.CenterXY = FVector2D::ZeroVector;
			BoxPayload.Shape = ESurfaceAnchorReservationShape::Box;
			BoxPayload.BoxHalfExtent = FVector(35.0, 35.0, 18.0);
			BoxPayload.SurfaceHeightSolveMode = SolveMode;
			BoxPayload.SurfaceZOffset = SurfaceOffsetBlocks;
			Reservation->ReservationPayload.InitializeAs<FSurfaceAnchorReservationPayload>(BoxPayload);
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateRaisedReservationSurfaceIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
		FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0
			? &Strategy->RootFoundationProvider.Reservations[0]
			: nullptr;
		if (Reservation != nullptr)
		{
			if (FSurfaceAnchorReservationPayload* const Payload = Reservation->ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
			{
				Payload->SurfaceZOffset = 20.0f;
			}
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateLiftedAndOffsetReservationSurfaceIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
		FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0
			? &Strategy->RootFoundationProvider.Reservations[0]
			: nullptr;
		if (Reservation != nullptr)
		{
			if (FSurfaceAnchorReservationPayload* const Payload = Reservation->ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
			{
				Payload->SurfaceZLift = 2.0f;
				Payload->SurfaceZOffset = 3.0f;
			}
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateLiftedReservationSurfaceIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
		FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0
			? &Strategy->RootFoundationProvider.Reservations[0]
			: nullptr;
		if (Reservation != nullptr)
		{
			if (FSurfaceAnchorReservationPayload* const Payload = Reservation->ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
			{
				Payload->SurfaceZLift = 10.0f;
			}
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateOneBlockLiftedReservationSurfaceIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
		FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0
			? &Strategy->RootFoundationProvider.Reservations[0]
			: nullptr;
		if (Reservation != nullptr)
		{
			if (FSurfaceAnchorReservationPayload* const Payload = Reservation->ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
			{
				Payload->SurfaceZLift = 1.0f;
			}
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateLoweredReservationSurfaceIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
		FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0
			? &Strategy->RootFoundationProvider.Reservations[0]
			: nullptr;
		if (Reservation != nullptr)
		{
			if (FSurfaceAnchorReservationPayload* const Payload = Reservation->ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
			{
				Payload->SurfaceZLift = -10.0f;
			}
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateLoweredAndOffsetReservationSurfaceIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
		FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0
			? &Strategy->RootFoundationProvider.Reservations[0]
			: nullptr;
		if (Reservation != nullptr)
		{
			if (FSurfaceAnchorReservationPayload* const Payload = Reservation->ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
			{
				Payload->SurfaceZLift = -10.0f;
				Payload->SurfaceZOffset = 3.0f;
			}
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateTwentyBlockLiftedReservationSurfaceIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
		FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0
			? &Strategy->RootFoundationProvider.Reservations[0]
			: nullptr;
		if (Reservation != nullptr)
		{
			if (FSurfaceAnchorReservationPayload* const Payload = Reservation->ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
			{
				Payload->SurfaceZLift = 20.0f;
			}
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateTwoAnchorIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
		if (FReservationDefinition* const FirstReservation = Strategy->RootFoundationProvider.Reservations.Num() > 0 ? &Strategy->RootFoundationProvider.Reservations[0] : nullptr)
		{
			FirstReservation->DebugName = TEXT("Left");
			FirstReservation->BiomeTag = TestReservationBiomeTag();
			if (FSurfaceAnchorReservationPayload* const Payload = FirstReservation->ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
			{
				Payload->CenterXY = FVector2D(-45.0, 0.0);
				Payload->SphereRadii = FVector(20.0, 20.0, 10.0);
				Payload->SpawnSphereRadii = FVector(10.0, 10.0, 6.0);
			}
		}

		FReservationDefinition RightReservation;
		RightReservation.DebugName = TEXT("Right");
		RightReservation.BiomeTag = TestReservationBiomeTag();
		RightReservation.bEnableSpawnReservation = true;

		FSurfaceAnchorReservationPayload RightPayload;
		RightPayload.CenterXY = FVector2D(45.0, 0.0);
		RightPayload.Shape = ESurfaceAnchorReservationShape::Sphere;
		RightPayload.SphereRadii = FVector(20.0, 20.0, 10.0);
		RightPayload.SpawnSphereRadii = FVector(10.0, 10.0, 6.0);
		RightReservation.ReservationPayload.InitializeAs<FSurfaceAnchorReservationPayload>(RightPayload);
		Strategy->RootFoundationProvider.Reservations.Add(RightReservation);

		return Strategy;
	}

	UBiomeStrategyData* CreateOutOfEnvelopeAnchorIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
		if (FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0 ? &Strategy->RootFoundationProvider.Reservations[0] : nullptr)
		{
			if (FSurfaceAnchorReservationPayload* const Payload = Reservation->ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
			{
				Payload->CenterXY = FVector2D(300.0, 0.0);
				Payload->SphereRadii = FVector(35.0, 35.0, 12.0);
			}
		}

		return Strategy;
	}

	UBiomeStrategyData* CreateInvalidAnchorExtentIslandStrategy()
	{
		UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
		if (FReservationDefinition* const Reservation = Strategy->RootFoundationProvider.Reservations.Num() > 0 ? &Strategy->RootFoundationProvider.Reservations[0] : nullptr)
		{
			if (FSurfaceAnchorReservationPayload* const Payload = Reservation->ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
			{
				Payload->SphereRadii = FVector::ZeroVector;
			}
		}

		return Strategy;
	}

	bool EvaluateNoiseAt(const FNodeLink Node, const FVector& NoiseCoordinate, float& OutValue)
	{
		if (Node.Node == nullptr || !Node.Node->BaseNode)
		{
			OutValue = 0.0f;
			return false;
		}

		OutValue = Node.Node->BaseNode->GenSingle3D(
			static_cast<float>(NoiseCoordinate.X),
			static_cast<float>(NoiseCoordinate.Y),
			static_cast<float>(NoiseCoordinate.Z),
			0);
		return true;
	}

	FResolvedWorldGenScaleContext ResolveTestScaleContext(const UBiomeStrategyData* Strategy)
	{
		return FWorldGenScaleContextResolver::Resolve(
			nullptr,
			Strategy != nullptr ? &Strategy->ScaleOverride : nullptr,
			Strategy != nullptr && Strategy->bUseScaleOverride);
	}

	bool EvaluateNoiseAtAuthoredBlock(
		const FNodeLink Node,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const FVector& AuthoredBlockPosition,
		float& OutValue)
	{
		return EvaluateNoiseAt(Node, ScaleContext.AuthoredBlockPositionToNoise(AuthoredBlockPosition), OutValue);
	}

	bool InferFlatReservationSurfaceZBlocks(
		const FNodeLink BiomeGenA,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const FVector2D AuthoredXY,
		float& OutSurfaceZBlocks)
	{
		const float SearchMinZ = FMath::Max(static_cast<float>(ScaleContext.AuthoredMinBlock.Z), -512.0f);
		const float SearchMaxZ = FMath::Min(static_cast<float>(ScaleContext.AuthoredMaxBlock.Z), 512.0f);
		const float SearchStepBlocks = 0.25f;
		if (SearchMaxZ <= SearchMinZ)
		{
			return false;
		}

		float PreviousZ = SearchMaxZ;
		float PreviousDensity = 0.0f;
		if (!EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(AuthoredXY.X, AuthoredXY.Y, PreviousZ), PreviousDensity))
		{
			return false;
		}

		for (float Z = SearchMaxZ - SearchStepBlocks; Z >= SearchMinZ; Z -= SearchStepBlocks)
		{
			float Density = 0.0f;
			if (!EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(AuthoredXY.X, AuthoredXY.Y, Z), Density))
			{
				return false;
			}

			if (PreviousDensity > 0.0f && Density <= 0.0f)
			{
				const float Denominator = PreviousDensity - Density;
				const float Alpha = FMath::IsNearlyZero(Denominator) ? 0.0f : FMath::Clamp(PreviousDensity / Denominator, 0.0f, 1.0f);
				OutSurfaceZBlocks = FMath::Lerp(PreviousZ, Z, Alpha);
				return true;
			}

			PreviousZ = Z;
			PreviousDensity = Density;
		}

		return false;
	}

	bool FindTopSolidFoundationZBlocks(
		const FNodeLink FoundationGenA,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const FVector2D AuthoredXY,
		float& OutSurfaceZBlocks)
	{
		for (float Z = 256.0f; Z >= -256.0f; Z -= 1.0f)
		{
			float Density = 0.0f;
			if (EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(AuthoredXY.X, AuthoredXY.Y, Z), Density)
				&& Density <= 0.0f)
			{
				OutSurfaceZBlocks = Z;
				return true;
			}
		}

		return false;
	}

	bool FindTopSolidFoundationZRangeBlocks(
		const FNodeLink FoundationGenA,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const TArray<FVector2D>& Samples,
		float& OutMinSurfaceZBlocks,
		float& OutMaxSurfaceZBlocks)
	{
		OutMinSurfaceZBlocks = TNumericLimits<float>::Max();
		OutMaxSurfaceZBlocks = -TNumericLimits<float>::Max();
		bool bFoundAny = false;
		for (const FVector2D& Sample : Samples)
		{
			float SurfaceZ = 0.0f;
			if (FindTopSolidFoundationZBlocks(FoundationGenA, ScaleContext, Sample, SurfaceZ))
			{
				OutMinSurfaceZBlocks = FMath::Min(OutMinSurfaceZBlocks, SurfaceZ);
				OutMaxSurfaceZBlocks = FMath::Max(OutMaxSurfaceZBlocks, SurfaceZ);
				bFoundAny = true;
			}
		}

		return bFoundAny;
	}

	enum class EFoundationTerrainProfileInvariantCase : uint8
	{
		Flat,
		Noisy,
		RollingHills,
		ErodedEdge,
		Crevice,
		Bowl,
		Ridged,
		Terraced
	};

	const TCHAR* LexToString(const EFoundationTerrainProfileInvariantCase ProfileCase)
	{
		switch (ProfileCase)
		{
		case EFoundationTerrainProfileInvariantCase::Flat:
			return TEXT("Flat");
		case EFoundationTerrainProfileInvariantCase::Noisy:
			return TEXT("Noisy");
		case EFoundationTerrainProfileInvariantCase::RollingHills:
			return TEXT("Rolling Hills");
		case EFoundationTerrainProfileInvariantCase::ErodedEdge:
			return TEXT("Eroded Edge");
		case EFoundationTerrainProfileInvariantCase::Crevice:
			return TEXT("Crevice");
		case EFoundationTerrainProfileInvariantCase::Bowl:
			return TEXT("Bowl");
		case EFoundationTerrainProfileInvariantCase::Ridged:
			return TEXT("Ridged");
		case EFoundationTerrainProfileInvariantCase::Terraced:
			return TEXT("Terraced");
		default:
			return TEXT("Unknown");
		}
	}

	void ApplyFoundationTerrainProfileInvariantCase(
		UBiomeStrategyData* Strategy,
		const EFoundationTerrainProfileInvariantCase ProfileCase)
	{
		if (Strategy == nullptr)
		{
			return;
		}

		switch (ProfileCase)
		{
		case EFoundationTerrainProfileInvariantCase::Flat:
			Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FFlatFoundationTerrainProfilePayload>();
			break;

		case EFoundationTerrainProfileInvariantCase::Noisy:
			Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FNoisyFoundationTerrainProfilePayload>();
			break;

		case EFoundationTerrainProfileInvariantCase::RollingHills:
			Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FRollingHillsFoundationTerrainProfilePayload>();
			break;

		case EFoundationTerrainProfileInvariantCase::ErodedEdge:
			Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FErodedEdgeFoundationTerrainProfilePayload>();
			break;

		case EFoundationTerrainProfileInvariantCase::Crevice:
			Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FCreviceFoundationTerrainProfilePayload>();
			break;

		case EFoundationTerrainProfileInvariantCase::Bowl:
			Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FBowlFoundationTerrainProfilePayload>();
			break;

		case EFoundationTerrainProfileInvariantCase::Ridged:
			Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FRidgedFoundationTerrainProfilePayload>();
			break;

		case EFoundationTerrainProfileInvariantCase::Terraced:
			Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FTerracedFoundationTerrainProfilePayload>();
			break;
		}
	}

	UBiomeStrategyData* CreateFoundationTerrainProfileInvariantStrategy(
		const EFoundationTerrainProfileInvariantCase ProfileCase,
		const float CenterZBlocks)
	{
		UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
		Strategy->RootFoundationProvider.Reservations.Reset();
		if (FIslandFoundationShapePayload* const IslandPayload = Strategy->RootFoundationProvider.ProviderPayload.GetMutablePtr<FIslandFoundationShapePayload>())
		{
			IslandPayload->IslandBody.Center.Z = CenterZBlocks;
			IslandPayload->BodyDetail.bEnableRimNoise = false;
			IslandPayload->BodyDetail.bEnableUndersideNoise = false;
			IslandPayload->BodyDetail.bEnableBottomSpikeNoise = false;
		}
		ApplyFoundationTerrainProfileInvariantCase(Strategy, ProfileCase);
		return Strategy;
	}

	TArray<FVector2D> GetFoundationTerrainProfileInvariantSamples()
	{
		TArray<FVector2D> Samples;
		Samples.Add(FVector2D::ZeroVector);
		Samples.Add(FVector2D(30.0f, 0.0f));
		Samples.Add(FVector2D(-30.0f, 0.0f));
		Samples.Add(FVector2D(0.0f, 30.0f));
		Samples.Add(FVector2D(0.0f, -30.0f));
		Samples.Add(FVector2D(55.0f, 35.0f));
		Samples.Add(FVector2D(-55.0f, -35.0f));
		return Samples;
	}

	TArray<FVector2D> GetBoxSolveSamples()
	{
		TArray<FVector2D> Samples;
		Samples.Reserve(9);
		for (int32 YIndex = -1; YIndex <= 1; ++YIndex)
		{
			for (int32 XIndex = -1; XIndex <= 1; ++XIndex)
			{
				Samples.Add(FVector2D(35.0f * XIndex, 35.0f * YIndex));
			}
		}
		return Samples;
	}

	TArray<FVector2D> GetBoxCornerSamples()
	{
		TArray<FVector2D> Samples;
		Samples.Reserve(4);
		Samples.Add(FVector2D(-35.0f, -35.0f));
		Samples.Add(FVector2D(35.0f, -35.0f));
		Samples.Add(FVector2D(-35.0f, 35.0f));
		Samples.Add(FVector2D(35.0f, 35.0f));
		return Samples;
	}

	bool GetFoundationSurfaceStats(
		const FNodeLink FoundationGenA,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const TArray<FVector2D>& Samples,
		double& OutAverageZ,
		float& OutMaxZ)
	{
		if (Samples.IsEmpty())
		{
			return false;
		}

		double SumZ = 0.0;
		float MaxZ = -TNumericLimits<float>::Max();
		int32 Count = 0;
		for (const FVector2D& Sample : Samples)
		{
			float SurfaceZ = 0.0f;
			if (!FindTopSolidFoundationZBlocks(FoundationGenA, ScaleContext, Sample, SurfaceZ))
			{
				return false;
			}

			SumZ += SurfaceZ;
			MaxZ = FMath::Max(MaxZ, SurfaceZ);
			++Count;
		}

		OutAverageZ = SumZ / Count;
		OutMaxZ = MaxZ;
		return true;
	}

	FNodeLink BuildDirectBiomeSlot(
		UFastNoiseEditor* Editor,
		const UBiomeStrategyData* Strategy,
		const EBiomeNoiseSlot NoiseSlot,
		const FGameplayTag BiomeTag,
		const FResolvedWorldGenScaleContext& ScaleContext)
	{
		const bool bFoundationBiome = BiomeTag == TestFoundationBiomeTag();
		if (bFoundationBiome)
		{
			return NoiseSlot == EBiomeNoiseSlot::GenA
				? UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy, &ScaleContext)
				: UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(Editor, Strategy, &ScaleContext);
		}

		return NoiseSlot == EBiomeNoiseSlot::GenA
			? UIslandBiomeFastNoiseLibrary::BuildReservationGenA(Editor, Strategy, BiomeTag, &ScaleContext)
			: UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, BiomeTag, &ScaleContext);
	}

	bool TestBiomeSlotWrapperMatchesDirectBuilder(
		FAutomationTestBase& Test,
		const EBiomeNoiseSlot NoiseSlot,
		const FGameplayTag BiomeTag,
		const TCHAR* SlotName)
	{
		UBiomeStrategyData* const Strategy = CreateMainMenuIslandStrategy();
		const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

		std::vector<FNodeLink> DirectNodes;
		UFastNoiseEditor* const DirectEditor = CreateTestFastNoiseEditor(DirectNodes);
		const FNodeLink Direct = BuildDirectBiomeSlot(DirectEditor, Strategy, NoiseSlot, BiomeTag, ScaleContext);

		std::vector<FNodeLink> WrapperNodes;
		UBiomeFastNoiseEditor* const WrapperEditor = CreateBiomeEditor(WrapperNodes, Strategy);
		WrapperEditor->NoiseSlot = NoiseSlot;
		WrapperEditor->BiomeTag = BiomeTag;
		const FNodeLink Wrapped = WrapperEditor->GetNoiseRef(GetTransientPackage());

		Test.TestTrue(*FString::Printf(TEXT("%s direct slot is valid"), SlotName), Direct.Node != nullptr && static_cast<bool>(Direct.Node->BaseNode));
		Test.TestTrue(*FString::Printf(TEXT("%s wrapper slot is valid"), SlotName), Wrapped.Node != nullptr && static_cast<bool>(Wrapped.Node->BaseNode));

		const FVector Samples[] = {
			FVector(0.0, 0.0, 0.0),
			FVector(0.0, 0.0, -5.0),
			FVector(0.0, 0.0, 5.0),
			FVector(20.0, 0.0, 0.0),
			FVector(40.0, 0.0, 0.0)
		};
		for (const FVector& Sample : Samples)
		{
			float DirectValue = 0.0f;
			float WrappedValue = 0.0f;
			Test.TestTrue(
				*FString::Printf(TEXT("%s direct samples at %s"), SlotName, *Sample.ToString()),
				EvaluateNoiseAtAuthoredBlock(Direct, ScaleContext, Sample, DirectValue));
			Test.TestTrue(
				*FString::Printf(TEXT("%s wrapper samples at %s"), SlotName, *Sample.ToString()),
				EvaluateNoiseAtAuthoredBlock(Wrapped, ScaleContext, Sample, WrappedValue));
			Test.TestTrue(
				*FString::Printf(TEXT("%s wrapper matches direct builder at %s. Direct=%f Wrapped=%f"), SlotName, *Sample.ToString(), DirectValue, WrappedValue),
				FMath::IsNearlyEqual(DirectValue, WrappedValue, 0.0001f));
		}

		return true;
	}
}
