// Copyright 2026 Spotted Loaf Studio

#include "Biome/Noise/Strategy/BiomeStrategyData.h"

#include "Biome/Noise/Foundation/InfinitePlaneFoundationPayloads.h"
#include "Biome/Noise/Foundation/VoidFoundationPayloads.h"
#include "Biome/Noise/Foundation/FoundationTerrainProfilePayloads.h"
#include "Biome/Noise/Foundation/FoundationTerrainProfileOps.h"
#include "Biome/Noise/Island/IslandBiomeStrategyPayloads.h"
#include "Biome/Noise/Island/IslandBiomeFastNoiseLibrary.h"
#include "Biome/Noise/Reservation/SurfaceAnchorReservationPayloads.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "ChunkWorld/ChunkWorldCore.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "Misc/DataValidation.h"
#include "Misc/ScopeLock.h"
#include "PorismDIMsWorldGeneratorExtension.h"

namespace
{
	constexpr float StrategyMinimumDomainTopAirClearanceBlocks = 1.0f;

	const TCHAR* LexToString(const EBiomeNoiseSlot NoiseSlot);
	FVector2D GetMultiInstanceOffsetXY(const FMultiInstanceSurfaceAnchorReservationPayload& Payload, int32 InstanceIndex);

	FString MakeStrategyDiagnosticsKey(
		const UBiomeStrategyData& Strategy,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const EBiomeNoiseSlot NoiseSlot,
		const FGameplayTag BiomeTag)
	{
		const FIslandFoundationShapePayload* IslandPayload = Strategy.RootFoundationProvider.ProviderPayload.GetPtr<FIslandFoundationShapePayload>();
		const FIslandFoundationBodySettings* Body = IslandPayload != nullptr ? &IslandPayload->IslandBody : nullptr;
		return FString::Printf(
			TEXT("%s|Slot=%d|BiomeTag=%s|%s|%s|%s|Center=%s|Radius=%.3f|Top=%.3f|Bottom=%.3f"),
			*Strategy.GetPathName(),
			static_cast<int32>(NoiseSlot),
			*BiomeTag.ToString(),
			*ScaleContext.ReferenceChunkBlockSpan.ToString(),
			*ScaleContext.AuthoredOriginToRawBlockOffset.ToString(),
			*ScaleContext.BlocksToNoiseScale.ToString(),
			Body != nullptr ? *Body->Center.ToString() : TEXT("None"),
			Body != nullptr ? Body->TopRadius : 0.0f,
			Body != nullptr ? Body->TopHeight : 0.0f,
			Body != nullptr ? Body->BottomDepth : 0.0f);
	}


	bool ShouldLogStrategyDiagnostics(const FString& Key)
	{
		static TSet<FString> LoggedKeys;
		static FCriticalSection LoggedKeysLock;

		FScopeLock ScopeLock(&LoggedKeysLock);
		if (LoggedKeys.Contains(Key))
		{
			return false;
		}

		LoggedKeys.Add(Key);
		return true;
	}

	AChunkWorldCore* ResolveChunkWorldFromCreator(UObject* Creator)
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

	FString MakeChildProviderPath(const FString& ParentPath, const int32 ChildIndex, const FName DebugName)
	{
		return FString::Printf(
			TEXT("%s.ChildFoundations[%d:%s]"),
			*ParentPath,
			ChildIndex,
			*DebugName.ToString());
	}

	FString MakePrototypeProviderPath(const FString& ParentPath)
	{
		return FString::Printf(TEXT("%s.PrototypeProvider"), *ParentPath);
	}

	FFoundationProviderDefinition MaterializePrototypeProvider(
		const FFoundationProviderDefinition& OwnerProvider,
		const FFoundationProviderPrototypeDefinition& PrototypeProvider)
	{
		FFoundationProviderDefinition Provider;
		Provider.DebugName = OwnerProvider.DebugName;
		Provider.ContributionType = OwnerProvider.ContributionType;
		Provider.BiomeTag = OwnerProvider.BiomeTag;
		Provider.ProviderType = PrototypeProvider.ProviderType;
		Provider.ProviderPayload = PrototypeProvider.ProviderPayload;
		Provider.TerrainProfile = PrototypeProvider.TerrainProfile;
		Provider.Reservations = PrototypeProvider.Reservations;
		Provider.ChildFoundations = PrototypeProvider.ChildFoundations;
		return Provider;
	}

	void InitializeDefaultTerrainProfileForProvider(const EFoundationProviderType ProviderType, FInstancedStruct& TerrainProfile)
	{
		switch (ProviderType)
		{
		case EFoundationProviderType::InfinitePlane:
			TerrainProfile.InitializeAs<FFlatFoundationTerrainProfilePayload>();
			break;
		case EFoundationProviderType::VCutVoid:
			TerrainProfile.Reset();
			break;
		case EFoundationProviderType::Island:
		default:
			TerrainProfile.InitializeAs<FNoisyFoundationTerrainProfilePayload>();
			break;
		}
	}

	bool EnsureDefaultTerrainProfileForProvider(const EFoundationProviderType ProviderType, FInstancedStruct& TerrainProfile)
	{
		if (TerrainProfile.IsValid() && IsFoundationTerrainProfilePayloadSupportedByProvider(TerrainProfile, ProviderType))
		{
			return false;
		}

		InitializeDefaultTerrainProfileForProvider(ProviderType, TerrainProfile);
		return true;
	}

	bool EnsureInfinitePlaneDomainMask(FInfinitePlaneFoundationPayload& PlanePayload)
	{
		if (PlanePayload.DomainMask.GetPtr<FInfiniteFoundationNoDomainMaskPayload>() != nullptr
			|| PlanePayload.DomainMask.GetPtr<FInfiniteFoundationNoisePatchDomainMaskPayload>() != nullptr
			|| PlanePayload.DomainMask.GetPtr<FInfiniteFoundationRepeatedHolesDomainMaskPayload>() != nullptr)
		{
			return false;
		}

		PlanePayload.DomainMask.InitializeAs<FInfiniteFoundationNoDomainMaskPayload>();
		return true;
	}

	bool EnsureProviderPayloadForType(
		const EFoundationProviderType ProviderType,
		FInstancedStruct& ProviderPayload,
		FMultiInstanceFoundationPayload** OutMultiPayload = nullptr)
	{
		if (OutMultiPayload != nullptr)
		{
			*OutMultiPayload = nullptr;
		}

		switch (ProviderType)
		{
		case EFoundationProviderType::Island:
			if (ProviderPayload.GetPtr<FIslandFoundationShapePayload>() == nullptr)
			{
				ProviderPayload.InitializeAs<FIslandFoundationShapePayload>();
				return true;
			}
			return false;

		case EFoundationProviderType::InfinitePlane:
			if (FInfinitePlaneFoundationPayload* PlanePayload = ProviderPayload.GetMutablePtr<FInfinitePlaneFoundationPayload>())
			{
				return EnsureInfinitePlaneDomainMask(*PlanePayload);
			}
			else
			{
				ProviderPayload.InitializeAs<FInfinitePlaneFoundationPayload>();
				return true;
			}

		case EFoundationProviderType::VCutVoid:
			if (ProviderPayload.GetPtr<FVCutFoundationPayload>() == nullptr)
			{
				ProviderPayload.InitializeAs<FVCutFoundationPayload>();
				return true;
			}
			return false;

		case EFoundationProviderType::MultiInstance:
		{
			bool bChanged = false;
			if (ProviderPayload.GetPtr<FMultiInstanceFoundationPayload>() == nullptr)
			{
				ProviderPayload.InitializeAs<FMultiInstanceFoundationPayload>();
				bChanged = true;
			}

			if (OutMultiPayload != nullptr)
			{
				*OutMultiPayload = ProviderPayload.GetMutablePtr<FMultiInstanceFoundationPayload>();
			}
			return bChanged;
		}

		default:
			return false;
		}
	}

	bool EnsureSurfaceAnchorTerrainPayload(FSurfaceAnchorReservationPayload& SurfacePayload)
	{
		if (SurfacePayload.TerrainPayload.IsValid())
		{
			return false;
		}

		SurfacePayload.TerrainPayload.InitializeAs<FSurfaceAnchorFlatTerrainPayload>();
		return true;
	}

	bool EnsureSurfaceAnchorPrototypePayload(FInstancedStruct& PrototypeSurfaceAnchor)
	{
		bool bChanged = false;
		if (PrototypeSurfaceAnchor.GetPtr<FSurfaceAnchorReservationPayload>() == nullptr)
		{
			PrototypeSurfaceAnchor.InitializeAs<FSurfaceAnchorReservationPayload>();
			bChanged = true;
		}

		if (FSurfaceAnchorReservationPayload* SurfacePayload = PrototypeSurfaceAnchor.GetMutablePtr<FSurfaceAnchorReservationPayload>())
		{
			bChanged = EnsureSurfaceAnchorTerrainPayload(*SurfacePayload) || bChanged;
		}
		return bChanged;
	}

	bool EnsureReservationPayloadForType(
		const EReservationType ReservationType,
		FInstancedStruct& ReservationPayload)
	{
		bool bChanged = false;
		switch (ReservationType)
		{
		case EReservationType::SurfaceAnchor:
			if (ReservationPayload.GetPtr<FSurfaceAnchorReservationPayload>() == nullptr)
			{
				ReservationPayload.InitializeAs<FSurfaceAnchorReservationPayload>();
				bChanged = true;
			}
			if (FSurfaceAnchorReservationPayload* SurfacePayload = ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
			{
				bChanged = EnsureSurfaceAnchorTerrainPayload(*SurfacePayload) || bChanged;
			}
			return bChanged;

		case EReservationType::MultiInstanceSurfaceAnchor:
			if (ReservationPayload.GetPtr<FMultiInstanceSurfaceAnchorReservationPayload>() == nullptr)
			{
				ReservationPayload.InitializeAs<FMultiInstanceSurfaceAnchorReservationPayload>();
				bChanged = true;
			}
			if (FMultiInstanceSurfaceAnchorReservationPayload* MultiPayload = ReservationPayload.GetMutablePtr<FMultiInstanceSurfaceAnchorReservationPayload>())
			{
				bChanged = EnsureSurfaceAnchorPrototypePayload(MultiPayload->PrototypeSurfaceAnchor) || bChanged;
			}
			return bChanged;

		default:
			return false;
		}
	}

	bool NormalizeReservationsForEditor(TArray<FReservationDefinition>& Reservations)
	{
		bool bChanged = false;
		for (FReservationDefinition& Reservation : Reservations)
		{
			bChanged = EnsureReservationPayloadForType(Reservation.ReservationType, Reservation.ReservationPayload) || bChanged;
		}
		return bChanged;
	}

	bool NormalizePrototypeProviderForEditor(FFoundationProviderPrototypeDefinition& PrototypeProvider);

	bool ForceVCutProviderToSubtractiveVoid(FFoundationProviderDefinition& Provider)
	{
		if (Provider.ProviderType != EFoundationProviderType::VCutVoid
			|| Provider.ContributionType == EFoundationContributionType::SubtractiveVoid)
		{
			return false;
		}

		Provider.ContributionType = EFoundationContributionType::SubtractiveVoid;
		Provider.BiomeTag = FGameplayTag();
		return true;
	}

	bool NormalizeChildProvidersForEditor(TArray<FInstancedStruct>& ChildProviders)
	{
		bool bChanged = false;
		for (FInstancedStruct& ChildProviderStruct : ChildProviders)
		{
			FFoundationProviderDefinition* ChildProvider = ChildProviderStruct.GetMutablePtr<FFoundationProviderDefinition>();
			if (ChildProvider == nullptr)
			{
				continue;
			}

			FMultiInstanceFoundationPayload* MultiPayload = nullptr;
			bChanged = ForceVCutProviderToSubtractiveVoid(*ChildProvider) || bChanged;
			bChanged = EnsureProviderPayloadForType(ChildProvider->ProviderType, ChildProvider->ProviderPayload, &MultiPayload) || bChanged;
			if (ChildProvider->ContributionType == EFoundationContributionType::AdditiveBiome
				&& ChildProvider->ProviderType != EFoundationProviderType::MultiInstance
				&& ChildProvider->ProviderType != EFoundationProviderType::VCutVoid)
			{
				bChanged = EnsureDefaultTerrainProfileForProvider(ChildProvider->ProviderType, ChildProvider->TerrainProfile) || bChanged;
			}

			if (MultiPayload != nullptr)
			{
				FFoundationProviderPrototypeDefinition* PrototypeProvider = MultiPayload->PrototypeProvider.GetMutablePtr<FFoundationProviderPrototypeDefinition>();
				if (PrototypeProvider == nullptr)
				{
					MultiPayload->PrototypeProvider.InitializeAs<FFoundationProviderPrototypeDefinition>();
					PrototypeProvider = MultiPayload->PrototypeProvider.GetMutablePtr<FFoundationProviderPrototypeDefinition>();
					bChanged = true;
				}
				if (PrototypeProvider != nullptr)
				{
					bChanged = NormalizePrototypeProviderForEditor(*PrototypeProvider) || bChanged;
					if (PrototypeProvider->ProviderType == EFoundationProviderType::VCutVoid
						&& ChildProvider->ContributionType != EFoundationContributionType::SubtractiveVoid)
					{
						ChildProvider->ContributionType = EFoundationContributionType::SubtractiveVoid;
						ChildProvider->BiomeTag = FGameplayTag();
						bChanged = true;
					}
				}
			}

			bChanged = NormalizeReservationsForEditor(ChildProvider->Reservations) || bChanged;
			bChanged = NormalizeChildProvidersForEditor(ChildProvider->ChildFoundations) || bChanged;
		}
		return bChanged;
	}

	bool NormalizePrototypeProviderForEditor(FFoundationProviderPrototypeDefinition& PrototypeProvider)
	{
		FMultiInstanceFoundationPayload* MultiPayload = nullptr;
		bool bChanged = EnsureProviderPayloadForType(PrototypeProvider.ProviderType, PrototypeProvider.ProviderPayload, &MultiPayload);
		if (PrototypeProvider.ProviderType != EFoundationProviderType::MultiInstance
			&& PrototypeProvider.ProviderType != EFoundationProviderType::VCutVoid)
		{
			bChanged = EnsureDefaultTerrainProfileForProvider(PrototypeProvider.ProviderType, PrototypeProvider.TerrainProfile) || bChanged;
		}

		if (MultiPayload != nullptr)
		{
			FFoundationProviderPrototypeDefinition* NestedPrototype = MultiPayload->PrototypeProvider.GetMutablePtr<FFoundationProviderPrototypeDefinition>();
			if (NestedPrototype == nullptr)
			{
				MultiPayload->PrototypeProvider.InitializeAs<FFoundationProviderPrototypeDefinition>();
				NestedPrototype = MultiPayload->PrototypeProvider.GetMutablePtr<FFoundationProviderPrototypeDefinition>();
				bChanged = true;
			}
			if (NestedPrototype != nullptr)
			{
				bChanged = NormalizePrototypeProviderForEditor(*NestedPrototype) || bChanged;
			}
		}

		bChanged = NormalizeReservationsForEditor(PrototypeProvider.Reservations) || bChanged;
		bChanged = NormalizeChildProvidersForEditor(PrototypeProvider.ChildFoundations) || bChanged;
		return bChanged;
	}

	bool NormalizeProviderDefinitionForEditor(FFoundationProviderDefinition& Provider)
	{
		FMultiInstanceFoundationPayload* MultiPayload = nullptr;
		bool bChanged = ForceVCutProviderToSubtractiveVoid(Provider);
		bChanged = EnsureProviderPayloadForType(Provider.ProviderType, Provider.ProviderPayload, &MultiPayload) || bChanged;
		if (Provider.ContributionType == EFoundationContributionType::AdditiveBiome
			&& Provider.ProviderType != EFoundationProviderType::MultiInstance
			&& Provider.ProviderType != EFoundationProviderType::VCutVoid)
		{
			bChanged = EnsureDefaultTerrainProfileForProvider(Provider.ProviderType, Provider.TerrainProfile) || bChanged;
		}

		if (MultiPayload != nullptr)
		{
			FFoundationProviderPrototypeDefinition* PrototypeProvider = MultiPayload->PrototypeProvider.GetMutablePtr<FFoundationProviderPrototypeDefinition>();
			if (PrototypeProvider == nullptr)
			{
				MultiPayload->PrototypeProvider.InitializeAs<FFoundationProviderPrototypeDefinition>();
				PrototypeProvider = MultiPayload->PrototypeProvider.GetMutablePtr<FFoundationProviderPrototypeDefinition>();
				bChanged = true;
			}
			if (PrototypeProvider != nullptr)
			{
				bChanged = NormalizePrototypeProviderForEditor(*PrototypeProvider) || bChanged;
				if (PrototypeProvider->ProviderType == EFoundationProviderType::VCutVoid
					&& Provider.ContributionType != EFoundationContributionType::SubtractiveVoid)
				{
					Provider.ContributionType = EFoundationContributionType::SubtractiveVoid;
					Provider.BiomeTag = FGameplayTag();
					bChanged = true;
				}
			}
		}

		bChanged = NormalizeReservationsForEditor(Provider.Reservations) || bChanged;
		bChanged = NormalizeChildProvidersForEditor(Provider.ChildFoundations) || bChanged;
		return bChanged;
	}

	struct FProviderEditorLocation
	{
		FFoundationProviderDefinition* Provider = nullptr;
		FFoundationProviderDefinition* ParentProvider = nullptr;
		TArray<FInstancedStruct>* OwningChildArray = nullptr;
		int32 OwningIndex = INDEX_NONE;
		TArray<FInstancedStruct>* ParentOwningChildArray = nullptr;
		int32 ParentOwningIndex = INDEX_NONE;
		FString Path;
		FString ParentPath;
		FString GrandparentPath;
	};

	bool FindProviderEditorLocationRecursive(
		FFoundationProviderDefinition& Provider,
		const FString& Path,
		const FString& ParentPath,
		const FString& GrandparentPath,
		const FString& TargetPath,
		FFoundationProviderDefinition* ParentProvider,
		TArray<FInstancedStruct>* OwningChildArray,
		const int32 OwningIndex,
		TArray<FInstancedStruct>* ParentOwningChildArray,
		const int32 ParentOwningIndex,
		FProviderEditorLocation& OutLocation)
	{
		if (Path == TargetPath)
		{
			OutLocation.Provider = &Provider;
			OutLocation.ParentProvider = ParentProvider;
			OutLocation.OwningChildArray = OwningChildArray;
			OutLocation.OwningIndex = OwningIndex;
			OutLocation.ParentOwningChildArray = ParentOwningChildArray;
			OutLocation.ParentOwningIndex = ParentOwningIndex;
			OutLocation.Path = Path;
			OutLocation.ParentPath = ParentPath;
			OutLocation.GrandparentPath = GrandparentPath;
			return true;
		}

		if (FMultiInstanceFoundationPayload* MultiPayload = Provider.ProviderPayload.GetMutablePtr<FMultiInstanceFoundationPayload>())
		{
			if (FFoundationProviderPrototypeDefinition* PrototypeProvider = MultiPayload->PrototypeProvider.GetMutablePtr<FFoundationProviderPrototypeDefinition>())
			{
				const FString PrototypePath = MakePrototypeProviderPath(Path);
				for (int32 ChildIndex = 0; ChildIndex < PrototypeProvider->ChildFoundations.Num(); ++ChildIndex)
				{
					FFoundationProviderDefinition* ChildProvider = PrototypeProvider->ChildFoundations[ChildIndex].GetMutablePtr<FFoundationProviderDefinition>();
					if (ChildProvider == nullptr)
					{
						continue;
					}

					const FString ChildPath = MakeChildProviderPath(PrototypePath, ChildIndex, ChildProvider->DebugName);
					if (FindProviderEditorLocationRecursive(
						*ChildProvider,
						ChildPath,
						PrototypePath,
						Path,
						TargetPath,
						&Provider,
						&PrototypeProvider->ChildFoundations,
						ChildIndex,
						OwningChildArray,
						OwningIndex,
						OutLocation))
					{
						return true;
					}
				}
			}
		}

		for (int32 ChildIndex = 0; ChildIndex < Provider.ChildFoundations.Num(); ++ChildIndex)
		{
			FFoundationProviderDefinition* ChildProvider = Provider.ChildFoundations[ChildIndex].GetMutablePtr<FFoundationProviderDefinition>();
			if (ChildProvider == nullptr)
			{
				continue;
			}

			const FString ChildPath = MakeChildProviderPath(Path, ChildIndex, ChildProvider->DebugName);
			if (FindProviderEditorLocationRecursive(
				*ChildProvider,
				ChildPath,
				Path,
				ParentPath,
				TargetPath,
				&Provider,
				&Provider.ChildFoundations,
				ChildIndex,
				OwningChildArray,
				OwningIndex,
				OutLocation))
			{
				return true;
			}
		}

		return false;
	}

	bool FindProviderEditorLocation(UBiomeStrategyData& Strategy, FProviderEditorLocation& OutLocation)
	{
		const FString TargetPath = Strategy.AuthoringTargetProviderPath.IsEmpty()
			? FString(TEXT("RootFoundationProvider"))
			: Strategy.AuthoringTargetProviderPath;
		return FindProviderEditorLocationRecursive(
			Strategy.RootFoundationProvider,
			TEXT("RootFoundationProvider"),
			FString(),
			FString(),
			TargetPath,
			nullptr,
			nullptr,
			INDEX_NONE,
			nullptr,
			INDEX_NONE,
			OutLocation);
	}

	bool SetAuthoringToolResult(UBiomeStrategyData& Strategy, const FString& Message, const bool bSuccess)
	{
		Strategy.LastAuthoringToolResult = Message;
		if (bSuccess)
		{
			UE_LOG(LogPorismDIMsWorldGeneratorExtension, Display, TEXT("Biome strategy authoring tool: %s"), *Message);
		}
		else
		{
			UE_LOG(LogPorismDIMsWorldGeneratorExtension, Warning, TEXT("Biome strategy authoring tool: %s"), *Message);
		}
		return bSuccess;
	}

	void MarkAuthoringToolMutation(UBiomeStrategyData& Strategy)
	{
#if WITH_EDITOR
		Strategy.PostEditChange();
#endif
		Strategy.MarkPackageDirty();
	}

	int32 CountBiomeInstructions(const FBiomeDualData& Biome)
	{
		return Biome.GenU_Mat1.Num()
			+ Biome.GenU_Mat2.Num()
			+ Biome.GenU_Mesh1.Num()
			+ Biome.GenU_Mesh2.Num()
			+ Biome.GenA_Mat1.Num()
			+ Biome.GenA_Mat2.Num()
			+ Biome.GenA_Mesh1.Num()
			+ Biome.GenA_Mesh2.Num()
			+ Biome.GenB_Mat1.Num()
			+ Biome.GenB_Mat2.Num()
			+ Biome.GenB_Mesh1.Num()
			+ Biome.GenB_Mesh2.Num();
	}

	bool HasBiomeDomain(const FBiomeDualData& Biome)
	{
		return !Biome.Domain.IsEmpty() || Biome.DomainBP != nullptr || Biome.DomainRun != nullptr;
	}

	bool HasBiomeGenA(const FBiomeDualData& Biome)
	{
		return !Biome.GenA.IsEmpty() || Biome.GenABP != nullptr || Biome.GenARun != nullptr;
	}

	bool HasBiomeDualSwitch(const FBiomeDualData& Biome)
	{
		return !Biome.DualSwitch.IsEmpty() || Biome.DualSwitchBP != nullptr || Biome.DualSwitchRun != nullptr;
	}

	FString DescribeBiomeNoiseEditor(const UBiomeFastNoiseEditor& BiomeEditor)
	{
		return FString::Printf(
			TEXT("BiomeWrapper(Path='%s' Class='%s' Slot=%s BiomeTag='%s' Strategy='%s')"),
			*BiomeEditor.GetPathName(),
			BiomeEditor.GetClass() != nullptr ? *BiomeEditor.GetClass()->GetPathName() : TEXT("None"),
			LexToString(BiomeEditor.NoiseSlot),
			*BiomeEditor.BiomeTag.ToString(),
			BiomeEditor.Strategy != nullptr ? *BiomeEditor.Strategy->GetPathName() : TEXT("None"));
	}

	FString DescribeNoiseSource(
		const TCHAR* SlotName,
		const FString& EncodedNoise,
		const TSubclassOf<UFastNoiseEditor>& SourceBP,
		const UFastNoiseEditor* SourceRun)
	{
		if (!EncodedNoise.IsEmpty())
		{
			return FString::Printf(TEXT("%s=EncodedString(Length=%d)"), SlotName, EncodedNoise.Len());
		}

		if (SourceRun != nullptr)
		{
			if (const UBiomeFastNoiseEditor* BiomeEditor = Cast<UBiomeFastNoiseEditor>(SourceRun))
			{
				return FString::Printf(TEXT("%s=Run%s"), SlotName, *DescribeBiomeNoiseEditor(*BiomeEditor));
			}

			return FString::Printf(
				TEXT("%s=Run(Path='%s' Class='%s')"),
				SlotName,
				*SourceRun->GetPathName(),
				SourceRun->GetClass() != nullptr ? *SourceRun->GetClass()->GetPathName() : TEXT("None"));
		}

		const UClass* SourceClass = SourceBP.Get();
		if (SourceClass != nullptr)
		{
			const UFastNoiseEditor* DefaultEditor = Cast<UFastNoiseEditor>(SourceClass->GetDefaultObject());
			if (const UBiomeFastNoiseEditor* BiomeEditor = Cast<UBiomeFastNoiseEditor>(DefaultEditor))
			{
				return FString::Printf(
					TEXT("%s=BPClass('%s') Default%s"),
					SlotName,
					*SourceClass->GetPathName(),
					*DescribeBiomeNoiseEditor(*BiomeEditor));
			}

			return FString::Printf(TEXT("%s=BPClass('%s')"), SlotName, *SourceClass->GetPathName());
		}

		return FString::Printf(TEXT("%s=None"), SlotName);
	}

	void GatherConfiguredBiomeRows(const UWorldGenDef& WorldGenDef, TArray<const FBiomeDualData*>& OutBiomes)
	{
		if (WorldGenDef.WorldBiomesDT != nullptr && WorldGenDef.WorldBiomesDT->GetRowMap().Num() > 0)
		{
			if (WorldGenDef.WorldBiomesDT->GetRowStruct() != FBiomeDualData::StaticStruct())
			{
				UE_LOG(
					LogPorismDIMsWorldGeneratorExtension,
					Warning,
					TEXT("WorldGenDef '%s' uses WorldBiomesDT '%s' with unexpected row struct '%s'; expected FBiomeDualData."),
					*WorldGenDef.GetPathName(),
					*WorldGenDef.WorldBiomesDT->GetPathName(),
					WorldGenDef.WorldBiomesDT->GetRowStruct() != nullptr ? *WorldGenDef.WorldBiomesDT->GetRowStruct()->GetName() : TEXT("None"));
				return;
			}

			for (const TPair<FName, uint8*>& Row : WorldGenDef.WorldBiomesDT->GetRowMap())
			{
				if (Row.Value != nullptr)
				{
					OutBiomes.Add(reinterpret_cast<const FBiomeDualData*>(Row.Value));
				}
			}
			return;
		}

		for (const FBiomeDualData& Biome : WorldGenDef.WorldBiomes)
		{
			OutBiomes.Add(&Biome);
		}
	}

	FString MakeWorldGenBiomeDiagnosticsKey(const UWorldGenDef& WorldGenDef, const TArray<const FBiomeDualData*>& Biomes)
	{
		FString Key = WorldGenDef.GetPathName();
		Key += FString::Printf(TEXT("|Rows=%d"), Biomes.Num());
		for (const FBiomeDualData* Biome : Biomes)
		{
			if (Biome == nullptr)
			{
				continue;
			}

			Key += FString::Printf(
				TEXT("|%s:%d:%.3f:%.3f:%d:%d:%d:%d:%s:%s:%s:%s"),
				*Biome->BiomeName,
				Biome->BiomeHidden ? 1 : 0,
				Biome->DomainOver,
				Biome->Overlap,
				CountBiomeInstructions(*Biome),
				HasBiomeDomain(*Biome) ? 1 : 0,
				HasBiomeGenA(*Biome) ? 1 : 0,
				HasBiomeDualSwitch(*Biome) ? 1 : 0,
				*DescribeNoiseSource(TEXT("Domain"), Biome->Domain, Biome->DomainBP, Biome->DomainRun),
				*DescribeNoiseSource(TEXT("GenA"), Biome->GenA, Biome->GenABP, Biome->GenARun),
				*DescribeNoiseSource(TEXT("GenB"), Biome->GenB, Biome->GenBBP, Biome->GenBRun),
				*DescribeNoiseSource(TEXT("DualSwitch"), Biome->DualSwitch, Biome->DualSwitchBP, Biome->DualSwitchRun));
		}
		return Key;
	}

	void LogWorldGenBiomeRowDiagnostics(UObject* Creator)
	{
		AChunkWorldCore* ChunkWorld = ResolveChunkWorldFromCreator(Creator);
		if (ChunkWorld == nullptr || ChunkWorld->WorldGenDef == nullptr)
		{
			return;
		}

		TArray<const FBiomeDualData*> Biomes;
		GatherConfiguredBiomeRows(*ChunkWorld->WorldGenDef, Biomes);
		const FString Key = MakeWorldGenBiomeDiagnosticsKey(*ChunkWorld->WorldGenDef, Biomes);
		if (!ShouldLogStrategyDiagnostics(Key))
		{
			return;
		}

		for (int32 Index = 0; Index < Biomes.Num(); ++Index)
		{
			const FBiomeDualData* Biome = Biomes[Index];
			if (Biome == nullptr)
			{
				continue;
			}

			const int32 InstructionCount = CountBiomeInstructions(*Biome);
			const bool bNoiseOnly = InstructionCount == 0;
			const bool bHasDomain = HasBiomeDomain(*Biome);
			const bool bHasGenA = HasBiomeGenA(*Biome);

			if (!Biome->BiomeHidden && !bNoiseOnly && Biome->DomainOver <= 0.0f)
			{
				UE_LOG(
					LogPorismDIMsWorldGeneratorExtension,
					Warning,
					TEXT("WorldGenDef biome row [%d] Name='%s' has material/mesh instructions but DomainOver <= 0. Porism's non-NoiseOnly generation path divides by DomainOver; set Domain Overlap above zero, for example 0.1 or 1.0, while debugging visible block output."),
					Index,
					*Biome->BiomeName);
			}

			if (!Biome->BiomeHidden && !bNoiseOnly && !bHasGenA)
			{
				UE_LOG(
					LogPorismDIMsWorldGeneratorExtension,
					Warning,
					TEXT("WorldGenDef biome row [%d] Name='%s' has material/mesh instructions but no GenA source. Porism will fall back to a constant-positive GenA, which produces empty terrain because positive terrain noise is air."),
					Index,
					*Biome->BiomeName);
			}

			if (!Biome->BiomeHidden && !bHasDomain)
			{
				UE_LOG(
					LogPorismDIMsWorldGeneratorExtension,
					Warning,
					TEXT("WorldGenDef biome row [%d] Name='%s' has no Domain source. Porism will use a constant-positive fallback domain, so this row can claim the whole sampled world."),
					Index,
					*Biome->BiomeName);
			}
		}
	}

	FString MakeScaleSourceText(const FResolvedWorldGenScaleContext& ScaleContext)
	{
		if (ScaleContext.bResolvedFromChunkWorld)
		{
			return TEXT("ChunkWorld");
		}
		if (ScaleContext.bResolvedFromOverride)
		{
			return TEXT("Override");
		}
		if (ScaleContext.bUsingFallbackDefaults)
		{
			return TEXT("FallbackDefaults");
		}
		return TEXT("Unknown");
	}

	bool HasSuspiciousIslandBlockUnits(const FIslandFoundationShapePayload& IslandPayload)
	{
		const FIslandFoundationBodySettings& Body = IslandPayload.IslandBody;
		return Body.TopRadius < 8.0f
			|| Body.TopHeight < 2.0f
			|| Body.BottomDepth < 2.0f
			|| Body.RimThickness < 1.0f
			|| IslandPayload.DomainRadiusPadding < 1.0f
			|| IslandPayload.DomainVerticalPadding < 1.0f;
	}

	bool HasSuspiciousReservationBlockUnits(const FSurfaceAnchorReservationPayload& ReservationPayload)
	{
		const FVector AnchorExtent = ReservationPayload.Shape == ESurfaceAnchorReservationShape::Box
			? ReservationPayload.BoxHalfExtent
			: ReservationPayload.SphereRadii;
		return AnchorExtent.X < 2.0
			|| AnchorExtent.Y < 2.0
			|| AnchorExtent.Z < 1.0;
	}

	float GetMaxPositiveSurfaceZLiftBlocks(const UBiomeStrategyData& Strategy)
	{
		float MaxLiftBlocks = 0.0f;
		for (const FReservationDefinition& Reservation : Strategy.RootFoundationProvider.Reservations)
		{
			if (const FSurfaceAnchorReservationPayload* ReservationPayload = Reservation.ReservationPayload.GetPtr<FSurfaceAnchorReservationPayload>())
			{
				MaxLiftBlocks = FMath::Max(MaxLiftBlocks, FMath::Max(0.0f, ReservationPayload->SurfaceZLift));
			}
			else if (const FMultiInstanceSurfaceAnchorReservationPayload* MultiPayload = Reservation.ReservationPayload.GetPtr<FMultiInstanceSurfaceAnchorReservationPayload>())
			{
				if (const FSurfaceAnchorReservationPayload* PrototypePayload = MultiPayload->PrototypeSurfaceAnchor.GetPtr<FSurfaceAnchorReservationPayload>())
				{
					const float MaxScale = FMath::Max(MultiPayload->MinUniformScale, MultiPayload->MaxUniformScale);
					MaxLiftBlocks = FMath::Max(MaxLiftBlocks, FMath::Max(0.0f, PrototypePayload->SurfaceZLift * MaxScale));
				}
			}
		}

		return MaxLiftBlocks;
	}

	float GetMaxPositiveProviderSurfaceZLiftBlocks(const FFoundationProviderDefinition& Provider)
	{
		float MaxLiftBlocks = 0.0f;
		for (const FReservationDefinition& Reservation : Provider.Reservations)
		{
			if (const FSurfaceAnchorReservationPayload* ReservationPayload = Reservation.ReservationPayload.GetPtr<FSurfaceAnchorReservationPayload>())
			{
				MaxLiftBlocks = FMath::Max(MaxLiftBlocks, FMath::Max(0.0f, ReservationPayload->SurfaceZLift));
			}
			else if (const FMultiInstanceSurfaceAnchorReservationPayload* MultiPayload = Reservation.ReservationPayload.GetPtr<FMultiInstanceSurfaceAnchorReservationPayload>())
			{
				if (const FSurfaceAnchorReservationPayload* PrototypePayload = MultiPayload->PrototypeSurfaceAnchor.GetPtr<FSurfaceAnchorReservationPayload>())
				{
					const float MaxScale = FMath::Max(MultiPayload->MinUniformScale, MultiPayload->MaxUniformScale);
					MaxLiftBlocks = FMath::Max(MaxLiftBlocks, FMath::Max(0.0f, PrototypePayload->SurfaceZLift * MaxScale));
				}
			}
		}

		return MaxLiftBlocks;
	}

	float GetStrategyMaxPositiveFoundationTerrainProfileHeightBlocks(const FFoundationProviderDefinition& Provider)
	{
		return GetFoundationTerrainProfileMaxPositiveHeightBlocks(Provider.TerrainProfile);
	}

	float GetStrategyIslandDomainRadiusPaddingBlocks(const FIslandFoundationShapePayload& IslandPayload)
	{
		return IslandPayload.DomainRadiusPadding
			+ FMath::Max(0.0f, IslandPayload.IslandBody.SideBulgeBlocks)
			+ (IslandPayload.BodyDetail.bEnableRimNoise ? FMath::Max(0.0f, IslandPayload.BodyDetail.RimNoiseAmplitude) : 0.0f);
	}

	float GetStrategyIslandDomainBottomPaddingBlocks(const FIslandFoundationShapePayload& IslandPayload)
	{
		return IslandPayload.DomainVerticalPadding
			+ FMath::Max(0.0f, IslandPayload.IslandBody.BottomPointDepthBlocks)
			+ (IslandPayload.BodyDetail.bEnableUndersideNoise ? FMath::Max(0.0f, IslandPayload.BodyDetail.UndersideNoiseAmplitude) : 0.0f)
			+ (IslandPayload.BodyDetail.bEnableBottomSpikeNoise ? FMath::Max(0.0f, IslandPayload.BodyDetail.BottomSpikeNoiseAmplitude) : 0.0f);
	}

	FVector GetPositiveVectorOrFallback(const FVector& Candidate, const FVector& Fallback)
	{
		return Candidate.X > 0.0 && Candidate.Y > 0.0 && Candidate.Z > 0.0
			? Candidate
			: Fallback;
	}

	FVector GetAnchorExtent(const FSurfaceAnchorReservationPayload& ReservationPayload)
	{
		return ReservationPayload.Shape == ESurfaceAnchorReservationShape::Box
			? GetPositiveVectorOrFallback(ReservationPayload.BoxHalfExtent, FVector(35.0, 35.0, 18.0))
			: GetPositiveVectorOrFallback(ReservationPayload.SphereRadii, FVector(35.0, 35.0, 18.0));
	}

	FVector GetSpawnExtent(const FSurfaceAnchorReservationPayload& ReservationPayload)
	{
		const FVector Fallback = GetAnchorExtent(ReservationPayload) * 0.5;
		return ReservationPayload.Shape == ESurfaceAnchorReservationShape::Box
			? GetPositiveVectorOrFallback(ReservationPayload.SpawnBoxHalfExtent, Fallback)
			: GetPositiveVectorOrFallback(ReservationPayload.SpawnSphereRadii, Fallback);
	}

	EFoundationSurfaceSampleMode ToFoundationSurfaceSampleMode(const ESurfaceAnchorHeightSolveMode Mode)
	{
		switch (Mode)
		{
		case ESurfaceAnchorHeightSolveMode::CenterSample:
			return EFoundationSurfaceSampleMode::CenterSample;
		case ESurfaceAnchorHeightSolveMode::AverageSamples:
			return EFoundationSurfaceSampleMode::AverageSamples;
		case ESurfaceAnchorHeightSolveMode::CornerMaxSamples:
			return EFoundationSurfaceSampleMode::CornerMaxSamples;
		case ESurfaceAnchorHeightSolveMode::MaxSamples:
		default:
			return EFoundationSurfaceSampleMode::MaxSamples;
		}
	}

	FFoundationSurfaceQuery MakeSurfaceAnchorFoundationQuery(const FSurfaceAnchorReservationPayload& ReservationPayload)
	{
		const FVector AnchorExtent = GetAnchorExtent(ReservationPayload);
		FFoundationSurfaceQuery Query;
		Query.CenterXY = ReservationPayload.CenterXY;
		Query.FootprintHalfExtent = FVector2D(AnchorExtent.X, AnchorExtent.Y);
		Query.bEllipticalFootprint = ReservationPayload.Shape == ESurfaceAnchorReservationShape::Sphere;
		Query.SampleMode = ToFoundationSurfaceSampleMode(ReservationPayload.SurfaceHeightSolveMode);
		return Query;
	}

	FBox MakeIslandFoundationBounds(const UBiomeStrategyData& Strategy)
	{
		const FIslandFoundationShapePayload* IslandPayload = Strategy.RootFoundationProvider.ProviderPayload.GetPtr<FIslandFoundationShapePayload>();
		if (IslandPayload == nullptr)
		{
			return FBox(ForceInit);
		}

		const FIslandFoundationBodySettings& Body = IslandPayload->IslandBody;
		const float Radius = Body.TopRadius + GetStrategyIslandDomainRadiusPaddingBlocks(*IslandPayload);
		const float TopZ = Body.Center.Z
			+ FMath::Max(IslandPayload->DomainVerticalPadding, StrategyMinimumDomainTopAirClearanceBlocks)
			+ FMath::Max(
				GetMaxPositiveSurfaceZLiftBlocks(Strategy),
				GetStrategyMaxPositiveFoundationTerrainProfileHeightBlocks(Strategy.RootFoundationProvider));
		const float BottomZ = Body.Center.Z - Body.TopHeight - Body.BottomDepth - GetStrategyIslandDomainBottomPaddingBlocks(*IslandPayload);
		return FBox(
			FVector(Body.Center.X - Radius, Body.Center.Y - Radius, BottomZ),
			FVector(Body.Center.X + Radius, Body.Center.Y + Radius, TopZ));
	}

	FBox MakeIslandProviderBounds(const FFoundationProviderDefinition& Provider)
	{
		const FIslandFoundationShapePayload* IslandPayload = Provider.ProviderPayload.GetPtr<FIslandFoundationShapePayload>();
		if (IslandPayload == nullptr)
		{
			return FBox(ForceInit);
		}

		const FIslandFoundationBodySettings& Body = IslandPayload->IslandBody;
		const float Radius = Body.TopRadius + GetStrategyIslandDomainRadiusPaddingBlocks(*IslandPayload);
		const float TopZ = Body.Center.Z
			+ FMath::Max(IslandPayload->DomainVerticalPadding, StrategyMinimumDomainTopAirClearanceBlocks)
			+ FMath::Max(
				GetMaxPositiveProviderSurfaceZLiftBlocks(Provider),
				GetStrategyMaxPositiveFoundationTerrainProfileHeightBlocks(Provider));
		const float BottomZ = Body.Center.Z - Body.TopHeight - Body.BottomDepth - GetStrategyIslandDomainBottomPaddingBlocks(*IslandPayload);
		return FBox(
			FVector(Body.Center.X - Radius, Body.Center.Y - Radius, BottomZ),
			FVector(Body.Center.X + Radius, Body.Center.Y + Radius, TopZ));
	}

	FBox MakeVCutProviderBounds(const FFoundationProviderDefinition& Provider)
	{
		const FVCutFoundationPayload* VCutPayload = Provider.ProviderPayload.GetPtr<FVCutFoundationPayload>();
		if (VCutPayload == nullptr)
		{
			return FBox(ForceInit);
		}

		const float HalfLength = FMath::Max(0.0f, VCutPayload->HalfLengthBlocks);
		const float HalfWidth = FMath::Max(VCutPayload->SurfaceHalfWidthBlocks, VCutPayload->BottomHalfWidthBlocks);
		const FVector Extent = VCutPayload->Axis == EVCutFoundationAxis::X
			? FVector(HalfLength, HalfWidth, FMath::Max(0.0f, VCutPayload->DepthBlocks))
			: FVector(HalfWidth, HalfLength, FMath::Max(0.0f, VCutPayload->DepthBlocks));
		return FBox(
			FVector(VCutPayload->Center.X - Extent.X, VCutPayload->Center.Y - Extent.Y, VCutPayload->Center.Z - Extent.Z),
			FVector(VCutPayload->Center.X + Extent.X, VCutPayload->Center.Y + Extent.Y, VCutPayload->Center.Z));
	}

	bool BoxesOverlapXY(const FBox& A, const FBox& B)
	{
		return A.IsValid && B.IsValid
			&& A.Min.X <= B.Max.X
			&& A.Max.X >= B.Min.X
			&& A.Min.Y <= B.Max.Y
			&& A.Max.Y >= B.Min.Y;
	}

	bool MakeSurfaceAnchorFootprintBox(
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		FBox& OutBox)
	{
		const FVector Extent = GetAnchorExtent(ReservationPayload);
		if (Extent.X <= 0.0 || Extent.Y <= 0.0)
		{
			OutBox = FBox(ForceInit);
			return false;
		}

		OutBox = FBox(
			FVector(ReservationPayload.CenterXY.X - Extent.X, ReservationPayload.CenterXY.Y - Extent.Y, 0.0),
			FVector(ReservationPayload.CenterXY.X + Extent.X, ReservationPayload.CenterXY.Y + Extent.Y, 0.0));
		return true;
	}

	void CollectReservationFootprintBoxes(
		const FReservationDefinition& Reservation,
		TArray<FBox>& OutBoxes)
	{
		if (const FSurfaceAnchorReservationPayload* SurfacePayload = Reservation.ReservationPayload.GetPtr<FSurfaceAnchorReservationPayload>())
		{
			FBox Box(ForceInit);
			if (MakeSurfaceAnchorFootprintBox(*SurfacePayload, Box))
			{
				OutBoxes.Add(Box);
			}
			return;
		}

		const FMultiInstanceSurfaceAnchorReservationPayload* MultiPayload = Reservation.ReservationPayload.GetPtr<FMultiInstanceSurfaceAnchorReservationPayload>();
		const FSurfaceAnchorReservationPayload* PrototypePayload = MultiPayload != nullptr
			? MultiPayload->PrototypeSurfaceAnchor.GetPtr<FSurfaceAnchorReservationPayload>()
			: nullptr;
		if (MultiPayload == nullptr || PrototypePayload == nullptr)
		{
			return;
		}

		const float WorstScale = FMath::Max(MultiPayload->MinUniformScale, MultiPayload->MaxUniformScale);
		for (int32 InstanceIndex = 0; InstanceIndex < FMath::Max(1, MultiPayload->InstanceCount); ++InstanceIndex)
		{
			FSurfaceAnchorReservationPayload InstancePayload = *PrototypePayload;
			InstancePayload.CenterXY = InstancePayload.CenterXY * WorstScale + GetMultiInstanceOffsetXY(*MultiPayload, InstanceIndex);
			InstancePayload.SphereRadii *= WorstScale;
			InstancePayload.BoxHalfExtent *= WorstScale;
			FBox Box(ForceInit);
			if (MakeSurfaceAnchorFootprintBox(InstancePayload, Box))
			{
				OutBoxes.Add(Box);
			}
		}
	}

	FSurfaceAnchorReservationPayload ResolveProviderLocalReservationPayload(
		const FFoundationProviderDefinition& Provider,
		const FSurfaceAnchorReservationPayload& ReservationPayload)
	{
		FSurfaceAnchorReservationPayload ResolvedPayload = ReservationPayload;
		if (const FIslandFoundationShapePayload* IslandPayload = Provider.ProviderPayload.GetPtr<FIslandFoundationShapePayload>())
		{
			ResolvedPayload.CenterXY += FVector2D(IslandPayload->IslandBody.Center.X, IslandPayload->IslandBody.Center.Y);
		}
		return ResolvedPayload;
	}

	bool QueryInfinitePlaneProviderSurface(
		const FFoundationProviderDefinition& Provider,
		const FString& DebugPath,
		const FFoundationSurfaceQuery& Query,
		const FResolvedWorldGenScaleContext& ScaleContext,
		FResolvedFoundationSurface& OutSurface)
	{
		const FInfinitePlaneFoundationPayload* PlanePayload = Provider.ProviderPayload.GetPtr<FInfinitePlaneFoundationPayload>();
		if (PlanePayload == nullptr)
		{
			OutSurface = FResolvedFoundationSurface();
			return false;
		}

		OutSurface = FResolvedFoundationSurface();
		OutSurface.bValid = true;
		OutSurface.DebugPath = FString::Printf(TEXT("%s.Surface"), *DebugPath);
		OutSurface.SurfaceZBlock = PlanePayload->SurfaceZ;
		OutSurface.SurfaceZNoise = static_cast<float>(ScaleContext.AuthoredBlockPositionToNoise(FVector(Query.CenterXY.X, Query.CenterXY.Y, PlanePayload->SurfaceZ)).Z);
		OutSurface.SampleCount = 1;
		return true;
	}

	FVector2D GetMultiInstanceOffsetXY(const FMultiInstanceFoundationPayload& Payload, const int32 InstanceIndex)
	{
		const int32 InstanceCount = FMath::Max(1, Payload.InstanceCount);
		switch (Payload.Arrangement)
		{
		case EMultiInstanceFoundationArrangement::Grid:
		{
			const int32 Columns = FMath::Max(1, Payload.GridColumns);
			const int32 Rows = FMath::DivideAndRoundUp(InstanceCount, Columns);
			const int32 Column = InstanceIndex % Columns;
			const int32 Row = InstanceIndex / Columns;
			return Payload.ArrangementCenterXY + FVector2D(
				(static_cast<float>(Column) - (static_cast<float>(Columns) - 1.0f) * 0.5f) * Payload.SpacingBlocks,
				(static_cast<float>(Row) - (static_cast<float>(Rows) - 1.0f) * 0.5f) * Payload.SpacingBlocks);
		}
		case EMultiInstanceFoundationArrangement::Ring:
		{
			if (InstanceCount == 1)
			{
				return Payload.ArrangementCenterXY;
			}

			const float AngleRadians = (static_cast<float>(InstanceIndex) / static_cast<float>(InstanceCount)) * UE_TWO_PI;
			return Payload.ArrangementCenterXY + FVector2D(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians)) * Payload.RingRadiusBlocks;
		}
		case EMultiInstanceFoundationArrangement::Line:
		default:
			return Payload.ArrangementCenterXY + FVector2D(
				(static_cast<float>(InstanceIndex) - (static_cast<float>(InstanceCount) - 1.0f) * 0.5f) * Payload.SpacingBlocks,
				0.0f);
		}
	}

	FVector2D GetMultiInstanceOffsetXY(const FMultiInstanceSurfaceAnchorReservationPayload& Payload, const int32 InstanceIndex)
	{
		const int32 InstanceCount = FMath::Max(1, Payload.InstanceCount);
		switch (Payload.Arrangement)
		{
		case EMultiInstanceFoundationArrangement::Grid:
		{
			const int32 Columns = FMath::Max(1, Payload.GridColumns);
			const int32 Rows = FMath::DivideAndRoundUp(InstanceCount, Columns);
			const int32 Column = InstanceIndex % Columns;
			const int32 Row = InstanceIndex / Columns;
			return Payload.ArrangementCenterXY + FVector2D(
				(static_cast<float>(Column) - (static_cast<float>(Columns) - 1.0f) * 0.5f) * Payload.SpacingBlocks,
				(static_cast<float>(Row) - (static_cast<float>(Rows) - 1.0f) * 0.5f) * Payload.SpacingBlocks);
		}
		case EMultiInstanceFoundationArrangement::Ring:
		{
			if (InstanceCount == 1)
			{
				return Payload.ArrangementCenterXY;
			}

			const float AngleRadians = (static_cast<float>(InstanceIndex) / static_cast<float>(InstanceCount)) * UE_TWO_PI;
			return Payload.ArrangementCenterXY + FVector2D(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians)) * Payload.RingRadiusBlocks;
		}
		case EMultiInstanceFoundationArrangement::Line:
		default:
			return Payload.ArrangementCenterXY + FVector2D(
				(static_cast<float>(InstanceIndex) - (static_cast<float>(InstanceCount) - 1.0f) * 0.5f) * Payload.SpacingBlocks,
				0.0f);
		}
	}

	FRandomStream MakeInstanceRandomStream(const FResolvedStrategyContext& Context, const FString& DebugPath, const int32 InstanceIndex)
	{
		uint32 Seed = GetTypeHash(DebugPath);
		Seed = HashCombine(Seed, GetTypeHash(Context.StrategySeed));
		Seed = HashCombine(Seed, GetTypeHash(InstanceIndex));
		return FRandomStream(static_cast<int32>(Seed));
	}

	float GetRangedInstanceValue(FRandomStream& Stream, const float MinValue, const float MaxValue)
	{
		return FMath::IsNearlyEqual(MinValue, MaxValue)
			? MinValue
			: Stream.FRandRange(FMath::Min(MinValue, MaxValue), FMath::Max(MinValue, MaxValue));
	}

	void TransformReservationPayload(FSurfaceAnchorReservationPayload& Payload, const float UniformScale)
	{
		Payload.CenterXY *= UniformScale;
		Payload.SphereRadii *= UniformScale;
		Payload.BoxHalfExtent *= UniformScale;
		Payload.SpawnSphereRadii *= UniformScale;
		Payload.SpawnBoxHalfExtent *= UniformScale;
		Payload.SurfaceZLift *= UniformScale;
		Payload.SurfaceZOffset *= UniformScale;
		if (FSurfaceAnchorNoisyTerrainPayload* NoisyTerrain = Payload.TerrainPayload.GetMutablePtr<FSurfaceAnchorNoisyTerrainPayload>())
		{
			NoisyTerrain->SurfaceNoiseAmplitude *= UniformScale;
			NoisyTerrain->TerraceStepHeight *= UniformScale;
		}
		if (FSurfaceAnchorBlendedSupportTerrainPayload* BlendedTerrain = Payload.TerrainPayload.GetMutablePtr<FSurfaceAnchorBlendedSupportTerrainPayload>())
		{
			BlendedTerrain->EdgeBlendWidthBlocks *= UniformScale;
		}
	}

	void TransformMultiInstanceSurfaceAnchorPayload(FMultiInstanceSurfaceAnchorReservationPayload& Payload, const float UniformScale)
	{
		Payload.ArrangementCenterXY *= UniformScale;
		Payload.SpacingBlocks *= UniformScale;
		Payload.RingRadiusBlocks *= UniformScale;
		Payload.MinSurfaceZOffsetBlocks *= UniformScale;
		Payload.MaxSurfaceZOffsetBlocks *= UniformScale;
		Payload.FootprintPaddingBlocks *= UniformScale;
		if (FSurfaceAnchorReservationPayload* PrototypePayload = Payload.PrototypeSurfaceAnchor.GetMutablePtr<FSurfaceAnchorReservationPayload>())
		{
			TransformReservationPayload(*PrototypePayload, UniformScale);
		}
	}

	void TransformInfinitePlanePayload(FInfinitePlaneFoundationPayload& Payload, const FVector& OffsetBlocks, const float UniformScale)
	{
		Payload.SurfaceZ = Payload.SurfaceZ * UniformScale + OffsetBlocks.Z;
		Payload.DomainTopPadding *= UniformScale;
		Payload.DomainBottomDepth *= UniformScale;
		if (FInfiniteFoundationRepeatedHolesDomainMaskPayload* RepeatedHoles = Payload.DomainMask.GetMutablePtr<FInfiniteFoundationRepeatedHolesDomainMaskPayload>())
		{
			RepeatedHoles->CellSizeBlocks *= UniformScale;
			RepeatedHoles->HoleRadiusBlocks *= UniformScale;
			RepeatedHoles->EdgeSoftnessBlocks *= UniformScale;
		}
	}

	void TransformVCutPayload(FVCutFoundationPayload& Payload, const FVector& OffsetBlocks, const float UniformScale)
	{
		Payload.Center = Payload.Center * UniformScale + OffsetBlocks;
		Payload.HalfLengthBlocks *= UniformScale;
		Payload.SurfaceHalfWidthBlocks *= UniformScale;
		Payload.BottomHalfWidthBlocks *= UniformScale;
		Payload.DepthBlocks *= UniformScale;
		Payload.EdgeSoftnessBlocks *= UniformScale;
		Payload.EdgeNoiseAmplitudeBlocks *= UniformScale;
		Payload.CenterlineWanderAmplitudeBlocks *= UniformScale;
		Payload.DomainWarpAmplitudeBlocks *= UniformScale;
		Payload.StepHeightBlocks *= UniformScale;
	}

	void TransformReservationPayload(
		FSurfaceAnchorReservationPayload& Payload,
		const FVector2D& OffsetXY,
		const float AdditionalSurfaceZOffsetBlocks,
		const float UniformScale)
	{
		TransformReservationPayload(Payload, UniformScale);
		Payload.CenterXY += OffsetXY;
		Payload.SurfaceZOffset += AdditionalSurfaceZOffsetBlocks;
	}

	void TransformProviderDefinition(FFoundationProviderDefinition& Provider, const FVector& OffsetBlocks, const float UniformScale)
	{
		if (FIslandFoundationShapePayload* IslandPayload = Provider.ProviderPayload.GetMutablePtr<FIslandFoundationShapePayload>())
		{
			IslandPayload->IslandBody.Center = IslandPayload->IslandBody.Center * UniformScale + OffsetBlocks;
			IslandPayload->IslandBody.TopRadius *= UniformScale;
			IslandPayload->IslandBody.TopHeight *= UniformScale;
			IslandPayload->IslandBody.BottomDepth *= UniformScale;
			IslandPayload->IslandBody.RimThickness *= UniformScale;
			IslandPayload->IslandBody.SideBulgeBlocks *= UniformScale;
			IslandPayload->IslandBody.BottomPointDepthBlocks *= UniformScale;
			IslandPayload->BodyDetail.RimNoiseAmplitude *= UniformScale;
			IslandPayload->BodyDetail.UndersideNoiseAmplitude *= UniformScale;
			IslandPayload->BodyDetail.BottomSpikeNoiseAmplitude *= UniformScale;
			IslandPayload->DomainRadiusPadding *= UniformScale;
			IslandPayload->DomainVerticalPadding *= UniformScale;
		}
		else if (FInfinitePlaneFoundationPayload* PlanePayload = Provider.ProviderPayload.GetMutablePtr<FInfinitePlaneFoundationPayload>())
		{
			TransformInfinitePlanePayload(*PlanePayload, OffsetBlocks, UniformScale);
		}
		else if (FVCutFoundationPayload* VCutPayload = Provider.ProviderPayload.GetMutablePtr<FVCutFoundationPayload>())
		{
			TransformVCutPayload(*VCutPayload, OffsetBlocks, UniformScale);
		}
		ApplyFoundationTerrainProfileUniformScale(Provider.TerrainProfile, UniformScale);

		for (FReservationDefinition& Reservation : Provider.Reservations)
		{
			if (FSurfaceAnchorReservationPayload* ReservationPayload = Reservation.ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
			{
				TransformReservationPayload(*ReservationPayload, UniformScale);
			}
			else if (FMultiInstanceSurfaceAnchorReservationPayload* MultiPayload = Reservation.ReservationPayload.GetMutablePtr<FMultiInstanceSurfaceAnchorReservationPayload>())
			{
				TransformMultiInstanceSurfaceAnchorPayload(*MultiPayload, UniformScale);
			}
		}

		for (FInstancedStruct& ChildProviderStruct : Provider.ChildFoundations)
		{
			if (FFoundationProviderDefinition* ChildProvider = ChildProviderStruct.GetMutablePtr<FFoundationProviderDefinition>())
			{
				TransformProviderDefinition(*ChildProvider, OffsetBlocks, UniformScale);
			}
		}
	}

	FReservationDefinition MakeSurfaceAnchorReservationInstance(
		const FReservationDefinition& SourceReservation,
		const FSurfaceAnchorReservationPayload& PrototypePayload,
		const FMultiInstanceSurfaceAnchorReservationPayload& MultiPayload,
		const FResolvedStrategyContext& Context,
		const FString& DebugPath,
		const int32 InstanceIndex)
	{
		FRandomStream InstanceStream = MakeInstanceRandomStream(Context, DebugPath, InstanceIndex);
		const float UniformScale = GetRangedInstanceValue(InstanceStream, MultiPayload.MinUniformScale, MultiPayload.MaxUniformScale);
		const float SurfaceZOffsetBlocks = GetRangedInstanceValue(InstanceStream, MultiPayload.MinSurfaceZOffsetBlocks, MultiPayload.MaxSurfaceZOffsetBlocks);
		const FVector2D OffsetXY = GetMultiInstanceOffsetXY(MultiPayload, InstanceIndex);

		FSurfaceAnchorReservationPayload InstancePayload = PrototypePayload;
		TransformReservationPayload(InstancePayload, OffsetXY, SurfaceZOffsetBlocks, UniformScale);

		FReservationDefinition InstanceReservation = SourceReservation;
		InstanceReservation.DebugName = FName(*FString::Printf(
			TEXT("%s.Instances[%d]"),
			*SourceReservation.DebugName.ToString(),
			InstanceIndex));
		InstanceReservation.ReservationType = EReservationType::SurfaceAnchor;
		InstanceReservation.ReservationPayload.InitializeAs<FSurfaceAnchorReservationPayload>(InstancePayload);
		return InstanceReservation;
	}

	void ExpandMultiInstanceReservationsForSnapshot(
		FFoundationProviderDefinition& Provider,
		const FResolvedStrategyContext& Context,
		const FString& DebugPath)
	{
		TArray<FReservationDefinition> ExpandedReservations;
		ExpandedReservations.Reserve(Provider.Reservations.Num());

		for (int32 ReservationIndex = 0; ReservationIndex < Provider.Reservations.Num(); ++ReservationIndex)
		{
			const FReservationDefinition& Reservation = Provider.Reservations[ReservationIndex];
			if (Reservation.ReservationType != EReservationType::MultiInstanceSurfaceAnchor)
			{
				ExpandedReservations.Add(Reservation);
				continue;
			}

			const FMultiInstanceSurfaceAnchorReservationPayload* MultiPayload = Reservation.ReservationPayload.GetPtr<FMultiInstanceSurfaceAnchorReservationPayload>();
			const FSurfaceAnchorReservationPayload* PrototypePayload = MultiPayload != nullptr
				? MultiPayload->PrototypeSurfaceAnchor.GetPtr<FSurfaceAnchorReservationPayload>()
				: nullptr;
			if (MultiPayload == nullptr || PrototypePayload == nullptr)
			{
				continue;
			}

			const FString ReservationPath = FString::Printf(
				TEXT("%s.Reservations[%d:%s]"),
				*DebugPath,
				ReservationIndex,
				*Reservation.DebugName.ToString());
			const int32 InstanceCount = FMath::Max(1, MultiPayload->InstanceCount);
			for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
			{
				ExpandedReservations.Add(MakeSurfaceAnchorReservationInstance(
					Reservation,
					*PrototypePayload,
					*MultiPayload,
					Context,
					ReservationPath,
					InstanceIndex));
			}
		}

		Provider.Reservations = MoveTemp(ExpandedReservations);
	}

	/** Expands multi-instance children inside provider snapshots so existing recursive FNE composition can render them. */
	void ExpandMultiInstanceChildrenForSnapshot(
		FFoundationProviderDefinition& Provider,
		const FResolvedStrategyContext& Context,
		const FString& DebugPath)
	{
		ExpandMultiInstanceReservationsForSnapshot(Provider, Context, DebugPath);

		TArray<FInstancedStruct> ExpandedChildren;
		ExpandedChildren.Reserve(Provider.ChildFoundations.Num());

		for (int32 ChildIndex = 0; ChildIndex < Provider.ChildFoundations.Num(); ++ChildIndex)
		{
			const FFoundationProviderDefinition* ChildProvider = Provider.ChildFoundations[ChildIndex].GetPtr<FFoundationProviderDefinition>();
			if (ChildProvider == nullptr)
			{
				continue;
			}

			const FString ChildPath = FString::Printf(
				TEXT("%s.ChildFoundations[%d:%s]"),
				*DebugPath,
				ChildIndex,
				*ChildProvider->DebugName.ToString());

			if (ChildProvider->ProviderType != EFoundationProviderType::MultiInstance)
			{
				FFoundationProviderDefinition ChildSnapshot = *ChildProvider;
				ExpandMultiInstanceChildrenForSnapshot(ChildSnapshot, Context, ChildPath);
				ExpandedChildren.Add(FInstancedStruct::Make(ChildSnapshot));
				continue;
			}

			const FMultiInstanceFoundationPayload* MultiPayload = ChildProvider->ProviderPayload.GetPtr<FMultiInstanceFoundationPayload>();
			const FFoundationProviderPrototypeDefinition* PrototypeProvider = MultiPayload != nullptr
				? MultiPayload->PrototypeProvider.GetPtr<FFoundationProviderPrototypeDefinition>()
				: nullptr;
			if (MultiPayload == nullptr || PrototypeProvider == nullptr)
			{
				continue;
			}

			const int32 InstanceCount = FMath::Max(1, MultiPayload->InstanceCount);
			for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
			{
				FRandomStream InstanceStream = MakeInstanceRandomStream(Context, ChildPath, InstanceIndex);
				const float UniformScale = GetRangedInstanceValue(InstanceStream, MultiPayload->MinUniformScale, MultiPayload->MaxUniformScale);
				const float ZOffsetBlocks = GetRangedInstanceValue(InstanceStream, MultiPayload->MinZOffsetBlocks, MultiPayload->MaxZOffsetBlocks);
				const FVector2D OffsetXY = GetMultiInstanceOffsetXY(*MultiPayload, InstanceIndex);

				FFoundationProviderDefinition InstanceSnapshot = MaterializePrototypeProvider(*ChildProvider, *PrototypeProvider);
				TransformProviderDefinition(InstanceSnapshot, FVector(OffsetXY.X, OffsetXY.Y, ZOffsetBlocks), UniformScale);

				const FString InstancePath = FString::Printf(
					TEXT("%s.Instances[%d:%s]"),
					*ChildPath,
					InstanceIndex,
					*InstanceSnapshot.DebugName.ToString());
				ExpandMultiInstanceChildrenForSnapshot(InstanceSnapshot, Context, InstancePath);
				ExpandedChildren.Add(FInstancedStruct::Make(InstanceSnapshot));
			}

			for (int32 FixedChildIndex = 0; FixedChildIndex < ChildProvider->ChildFoundations.Num(); ++FixedChildIndex)
			{
				const FFoundationProviderDefinition* FixedChildProvider = ChildProvider->ChildFoundations[FixedChildIndex].GetPtr<FFoundationProviderDefinition>();
				if (FixedChildProvider == nullptr)
				{
					continue;
				}

				FFoundationProviderDefinition FixedChildSnapshot = *FixedChildProvider;
				const FString FixedChildPath = FString::Printf(
					TEXT("%s.ChildFoundations[%d:%s]"),
					*ChildPath,
					FixedChildIndex,
					*FixedChildSnapshot.DebugName.ToString());
				ExpandMultiInstanceChildrenForSnapshot(FixedChildSnapshot, Context, FixedChildPath);
				ExpandedChildren.Add(FInstancedStruct::Make(FixedChildSnapshot));
			}
		}

		Provider.ChildFoundations = MoveTemp(ExpandedChildren);
	}

	FBox MakeSurfaceAnchorFieldBounds(
		const FSurfaceAnchorReservationPayload& ReservationPayload,
		const EReservationFieldKind FieldKind,
		const float FoundationSurfaceZ)
	{
		const FVector Extent = FieldKind == EReservationFieldKind::Spawn
			? GetSpawnExtent(ReservationPayload)
			: GetAnchorExtent(ReservationPayload);
		const float SurfaceZ = FoundationSurfaceZ + ReservationPayload.SurfaceZLift + ReservationPayload.SurfaceZOffset;
		const float TopExtent = FieldKind == EReservationFieldKind::Spawn
			? Extent.Z
			: FMath::Max(Extent.Z, FMath::Max(Extent.X, Extent.Y));
		const FVector Center(ReservationPayload.CenterXY.X, ReservationPayload.CenterXY.Y, SurfaceZ);
		return FBox(
			FVector(Center.X - Extent.X, Center.Y - Extent.Y, Center.Z - Extent.Z),
			FVector(Center.X + Extent.X, Center.Y + Extent.Y, Center.Z + TopExtent));
	}

	bool ShouldIncludeFieldBounds(const FBox& QueryBounds, const FBox& FieldBounds)
	{
		return !QueryBounds.IsValid || QueryBounds.Intersect(FieldBounds);
	}

	void PopulateResolvedFieldFromBounds(
		const FResolvedFoundationProviderInstance& ProviderInstance,
		const FReservationDefinition& Reservation,
		const EReservationFieldKind FieldKind,
		const FBox& FieldBounds,
		FTaggedReservationField& OutField)
	{
		OutField.DebugName = Reservation.DebugName;
		OutField.BiomeTag = Reservation.BiomeTag;
		OutField.FieldKind = FieldKind;
		OutField.DebugPath = FString::Printf(
			TEXT("%s.Reservations[%s].%s"),
			*ProviderInstance.DebugPath,
			*Reservation.DebugName.ToString(),
			FieldKind == EReservationFieldKind::Spawn ? TEXT("Spawn") : TEXT("Reservation"));
		OutField.FieldTags = FieldKind == EReservationFieldKind::Spawn
			? Reservation.SpawnFieldTags
			: Reservation.FieldTags;
		OutField.AuthoredMinBlock = FieldBounds.Min;
		OutField.AuthoredMaxBlock = FieldBounds.Max;
	}

	const TCHAR* LexToString(const ESurfaceAnchorHeightSolveMode Mode)
	{
		switch (Mode)
		{
		case ESurfaceAnchorHeightSolveMode::CenterSample:
			return TEXT("CenterSample");
		case ESurfaceAnchorHeightSolveMode::AverageSamples:
			return TEXT("AverageSamples");
		case ESurfaceAnchorHeightSolveMode::MaxSamples:
			return TEXT("MaxSamples");
		case ESurfaceAnchorHeightSolveMode::CornerMaxSamples:
			return TEXT("CornerMaxSamples");
		default:
			return TEXT("Unknown");
		}
	}

	void LogStrategyDiagnostics(
		const UBiomeStrategyData& Strategy,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const EBiomeNoiseSlot NoiseSlot,
		const FGameplayTag BiomeTag)
	{
		const FString Key = MakeStrategyDiagnosticsKey(Strategy, ScaleContext, NoiseSlot, BiomeTag);
		if (!ShouldLogStrategyDiagnostics(Key))
		{
			return;
		}

		const FIslandFoundationShapePayload* IslandPayload = Strategy.RootFoundationProvider.ProviderPayload.GetPtr<FIslandFoundationShapePayload>();
		if (IslandPayload == nullptr)
		{
			UE_LOG(
				LogPorismDIMsWorldGeneratorExtension,
				Verbose,
				TEXT("Biome strategy '%s' root provider has no direct island payload; resolved provider expansion may still contribute biome noise."),
				*Strategy.GetPathName());
			return;
		}

		const FIslandFoundationBodySettings& Body = IslandPayload->IslandBody;
		const FVector SurfaceAuthored = Body.Center;
		const FVector BodyBottomAuthored(Body.Center.X, Body.Center.Y, Body.Center.Z - Body.TopHeight - Body.BottomDepth);
		const FVector SurfaceRaw = ScaleContext.AuthoredBlockPositionToRawBlock(SurfaceAuthored);
		const FVector SurfaceNoise = ScaleContext.AuthoredBlockPositionToNoise(SurfaceAuthored);
		const FVector BodyBottomRaw = ScaleContext.AuthoredBlockPositionToRawBlock(BodyBottomAuthored);
		const FVector BodyBottomNoise = ScaleContext.AuthoredBlockPositionToNoise(BodyBottomAuthored);

		bool bSuspiciousBlockUnits = HasSuspiciousIslandBlockUnits(*IslandPayload);
		FString ReservationSummary = TEXT("None");
		for (const FReservationDefinition& Reservation : Strategy.RootFoundationProvider.Reservations)
		{
			const FSurfaceAnchorReservationPayload* ReservationPayload = Reservation.ReservationPayload.GetPtr<FSurfaceAnchorReservationPayload>();
			if (ReservationPayload == nullptr)
			{
				continue;
			}

			const FVector AnchorExtent = ReservationPayload->Shape == ESurfaceAnchorReservationShape::Box
				? ReservationPayload->BoxHalfExtent
				: ReservationPayload->SphereRadii;
			const float SurfaceZAdjustmentBlocks = ReservationPayload->SurfaceZLift + ReservationPayload->SurfaceZOffset;
			const FVector AnchorAuthored(ReservationPayload->CenterXY.X, ReservationPayload->CenterXY.Y, Body.Center.Z + SurfaceZAdjustmentBlocks);
			FString TerrainType = TEXT("Flat");
			float SurfaceNoiseAmplitude = 0.0f;
			float SurfaceNoiseScale = 0.0f;
			bool bUseTerraces = false;
			if (const FSurfaceAnchorNoisyTerrainPayload* NoisyTerrain = ReservationPayload->TerrainPayload.GetPtr<FSurfaceAnchorNoisyTerrainPayload>())
			{
				TerrainType = TEXT("Noisy");
				SurfaceNoiseAmplitude = NoisyTerrain->SurfaceNoiseAmplitude;
				SurfaceNoiseScale = NoisyTerrain->SurfaceNoiseScale;
				bUseTerraces = NoisyTerrain->bUseTerraces;
			}
			else if (ReservationPayload->TerrainPayload.GetPtr<FSurfaceAnchorBlendedSupportTerrainPayload>() != nullptr)
			{
				TerrainType = TEXT("BlendedSupport");
			}

			ReservationSummary = FString::Printf(
				TEXT("Name=%s BiomeTag=%s Shape=%s CenterXY=%s ExtentBlocks=%s SurfaceZLiftBlocks=%.3f SurfaceZOffsetBlocks=%.3f SurfaceZAdjustmentBlocks=%.3f SurfaceSolve=%s TerrainType=%s SurfaceNoiseAmplitude=%.3f SurfaceNoiseScale=%.3f Terraces=%s CenterRaw=%s CenterNoise=%s"),
				*Reservation.DebugName.ToString(),
				*Reservation.BiomeTag.ToString(),
				ReservationPayload->Shape == ESurfaceAnchorReservationShape::Box ? TEXT("Box") : TEXT("Sphere"),
				*ReservationPayload->CenterXY.ToString(),
				*AnchorExtent.ToString(),
				ReservationPayload->SurfaceZLift,
				ReservationPayload->SurfaceZOffset,
				SurfaceZAdjustmentBlocks,
				LexToString(ReservationPayload->SurfaceHeightSolveMode),
				*TerrainType,
				SurfaceNoiseAmplitude,
				SurfaceNoiseScale,
				bUseTerraces ? TEXT("true") : TEXT("false"),
				*ScaleContext.AuthoredBlockPositionToRawBlock(AnchorAuthored).ToString(),
				*ScaleContext.AuthoredBlockPositionToNoise(AnchorAuthored).ToString());
			bSuspiciousBlockUnits = bSuspiciousBlockUnits || HasSuspiciousReservationBlockUnits(*ReservationPayload);
			break;
		}

		const FString DiagnosticsMessage = FString::Printf(
			TEXT("Biome strategy '%s' compiled with %s scale: BaseBlockSize=%d NoiseScale=%s NoiseOffset=%s ReferenceSpanBlocks=%s AuthoredOriginToRawOffset=%s IslandBlocks(CenterSurface=%s TopRadius=%.3f TopHeight=%.3f BottomDepth=%.3f RimThickness=%.3f DomainPaddingRadius=%.3f DomainPaddingZ=%.3f) Converted(SurfaceRaw=%s SurfaceNoise=%s BodyBottomRaw=%s BodyBottomNoise=%s) FirstReservation(%s)."),
			*Strategy.GetPathName(),
			*MakeScaleSourceText(ScaleContext),
			ScaleContext.BaseBlockSize,
			*ScaleContext.NoiseScale.ToString(),
			*ScaleContext.NoiseCoordinateOffset.ToString(),
			*ScaleContext.ReferenceChunkBlockSpan.ToString(),
			*ScaleContext.AuthoredOriginToRawBlockOffset.ToString(),
			*SurfaceAuthored.ToString(),
			Body.TopRadius,
			Body.TopHeight,
			Body.BottomDepth,
			Body.RimThickness,
			IslandPayload->DomainRadiusPadding,
			IslandPayload->DomainVerticalPadding,
			*SurfaceRaw.ToString(),
			*SurfaceNoise.ToString(),
			*BodyBottomRaw.ToString(),
			*BodyBottomNoise.ToString(),
			*ReservationSummary);
		if (bSuspiciousBlockUnits)
		{
			UE_LOG(
				LogPorismDIMsWorldGeneratorExtension,
				Warning,
				TEXT("%s Block-unit island/reservation values look very small; old raw FastNoise values such as 1.2 will generate a tiny or invisible domain after the Stage 2.5 block-unit refactor."),
				*DiagnosticsMessage);
			return;
		}
	}

	const TCHAR* LexToString(const EBiomeNoiseSlot NoiseSlot)
	{
		switch (NoiseSlot)
		{
		case EBiomeNoiseSlot::DomainNoise:
			return TEXT("DomainNoise");
		case EBiomeNoiseSlot::GenA:
			return TEXT("GenA");
		default:
			return TEXT("Unknown");
		}
	}

}

FFoundationProviderDefinition::FFoundationProviderDefinition()
{
	TerrainProfile.InitializeAs<FNoisyFoundationTerrainProfilePayload>();
}

void FBiomeStrategyValidationResult::AddIssue(
	const EBiomeStrategyValidationSeverity Severity,
	const FString& DebugPath,
	const FString& Message,
	const FString& FixText)
{
	FBiomeStrategyValidationIssue& Issue = Issues.AddDefaulted_GetRef();
	Issue.Severity = Severity;
	Issue.DebugPath = DebugPath;
	Issue.Message = Message;
	Issue.FixText = FixText;
}

bool FBiomeStrategyValidationResult::HasErrors() const
{
	return Issues.ContainsByPredicate(
		[](const FBiomeStrategyValidationIssue& Issue)
		{
			return Issue.Severity == EBiomeStrategyValidationSeverity::Error;
		});
}

bool FBiomeStrategyValidationResult::HasWarnings() const
{
	return Issues.ContainsByPredicate(
		[](const FBiomeStrategyValidationIssue& Issue)
		{
			return Issue.Severity == EBiomeStrategyValidationSeverity::Warning;
		});
}

FFoundationProviderQueryCapabilities UBiomeStrategyData::GetProviderQueryCapabilities(const FFoundationProviderDefinition& Provider)
{
	FFoundationProviderQueryCapabilities Capabilities;
	switch (Provider.ProviderType)
	{
	case EFoundationProviderType::Island:
		if (Provider.ProviderPayload.GetPtr<FIslandFoundationShapePayload>() != nullptr)
		{
			Capabilities.SurfaceQueryMode = EFoundationProviderSurfaceQueryMode::Finite;
			Capabilities.VolumeQueryMode = EFoundationProviderVolumeQueryMode::Finite;
		}
		break;
	case EFoundationProviderType::InfinitePlane:
		if (Provider.ProviderPayload.GetPtr<FInfinitePlaneFoundationPayload>() != nullptr)
		{
			Capabilities.SurfaceQueryMode = EFoundationProviderSurfaceQueryMode::Infinite;
			Capabilities.VolumeQueryMode = EFoundationProviderVolumeQueryMode::Infinite;
		}
		break;
	case EFoundationProviderType::VCutVoid:
		if (Provider.ProviderPayload.GetPtr<FVCutFoundationPayload>() != nullptr)
		{
			Capabilities.SurfaceQueryMode = EFoundationProviderSurfaceQueryMode::Unsupported;
			Capabilities.VolumeQueryMode = EFoundationProviderVolumeQueryMode::Finite;
		}
		break;
	case EFoundationProviderType::MultiInstance:
		if (Provider.ProviderPayload.GetPtr<FMultiInstanceFoundationPayload>() != nullptr)
		{
			Capabilities.SurfaceQueryMode = EFoundationProviderSurfaceQueryMode::CellBounded;
			Capabilities.VolumeQueryMode = EFoundationProviderVolumeQueryMode::Finite;
		}
		break;
	default:
		break;
	}

	return Capabilities;
}

bool UBiomeStrategyData::QueryProviderBounds(
	const FFoundationProviderDefinition& Provider,
	const FString& DebugPath,
	FFoundationProviderBounds& OutBounds)
{
	OutBounds = FFoundationProviderBounds();
	OutBounds.DebugPath = DebugPath;

	FBox Bounds(ForceInit);
	if (Provider.ProviderType == EFoundationProviderType::Island)
	{
		Bounds = MakeIslandProviderBounds(Provider);
	}
	else if (Provider.ProviderType == EFoundationProviderType::VCutVoid)
	{
		Bounds = MakeVCutProviderBounds(Provider);
	}
	OutBounds.bValid = Bounds.IsValid != 0;
	if (!Bounds.IsValid)
	{
		return false;
	}

	OutBounds.AuthoredMinBlock = Bounds.Min;
	OutBounds.AuthoredMaxBlock = Bounds.Max;
	return true;
}

bool UBiomeStrategyData::QueryProviderSurface(
	const FFoundationProviderDefinition& Provider,
	const FString& DebugPath,
	const FFoundationSurfaceQuery& Query,
	const FResolvedWorldGenScaleContext& ScaleContext,
	FResolvedFoundationSurface& OutSurface)
{
	if (Provider.ProviderType == EFoundationProviderType::Island)
	{
		return UIslandBiomeFastNoiseLibrary::QueryIslandProviderSurface(
			Provider,
			DebugPath,
			Query,
			ScaleContext,
			OutSurface);
	}
	if (Provider.ProviderType == EFoundationProviderType::InfinitePlane)
	{
		return QueryInfinitePlaneProviderSurface(Provider, DebugPath, Query, ScaleContext, OutSurface);
	}

	OutSurface = FResolvedFoundationSurface();
	return false;
}

const FReservationDefinition* UBiomeStrategyData::FindFirstReservationForBiomeTag(const FGameplayTag BiomeTag) const
{
	if (!BiomeTag.IsValid())
	{
		return nullptr;
	}

	return RootFoundationProvider.Reservations.FindByPredicate(
		[BiomeTag](const FReservationDefinition& Reservation)
		{
			return Reservation.BiomeTag == BiomeTag;
		});
}

bool UBiomeStrategyData::MoveTargetReservationEarlier()
{
	FProviderEditorLocation Location;
	if (!FindProviderEditorLocation(*this, Location) || Location.Provider == nullptr)
	{
		return SetAuthoringToolResult(*this, FString::Printf(TEXT("Provider path '%s' was not found."), *AuthoringTargetProviderPath), false);
	}

	if (!Location.Provider->Reservations.IsValidIndex(AuthoringTargetReservationIndex))
	{
		return SetAuthoringToolResult(*this, FString::Printf(TEXT("Reservation index %d is not valid for provider '%s'."), AuthoringTargetReservationIndex, *Location.Path), false);
	}

	if (AuthoringTargetReservationIndex <= 0)
	{
		return SetAuthoringToolResult(*this, TEXT("Target reservation is already first in its provider."), false);
	}

#if WITH_EDITOR
	Modify();
#endif

	const int32 NewIndex = AuthoringTargetReservationIndex - 1;
	Location.Provider->Reservations.Swap(AuthoringTargetReservationIndex, NewIndex);
	AuthoringTargetReservationIndex = NewIndex;
	MarkAuthoringToolMutation(*this);
	return SetAuthoringToolResult(*this, FString::Printf(TEXT("Moved reservation to index %d in '%s'."), NewIndex, *Location.Path), true);
}

bool UBiomeStrategyData::MoveTargetReservationLater()
{
	FProviderEditorLocation Location;
	if (!FindProviderEditorLocation(*this, Location) || Location.Provider == nullptr)
	{
		return SetAuthoringToolResult(*this, FString::Printf(TEXT("Provider path '%s' was not found."), *AuthoringTargetProviderPath), false);
	}

	if (!Location.Provider->Reservations.IsValidIndex(AuthoringTargetReservationIndex))
	{
		return SetAuthoringToolResult(*this, FString::Printf(TEXT("Reservation index %d is not valid for provider '%s'."), AuthoringTargetReservationIndex, *Location.Path), false);
	}

	if (AuthoringTargetReservationIndex >= Location.Provider->Reservations.Num() - 1)
	{
		return SetAuthoringToolResult(*this, TEXT("Target reservation is already last in its provider."), false);
	}

#if WITH_EDITOR
	Modify();
#endif

	const int32 NewIndex = AuthoringTargetReservationIndex + 1;
	Location.Provider->Reservations.Swap(AuthoringTargetReservationIndex, NewIndex);
	AuthoringTargetReservationIndex = NewIndex;
	MarkAuthoringToolMutation(*this);
	return SetAuthoringToolResult(*this, FString::Printf(TEXT("Moved reservation to index %d in '%s'."), NewIndex, *Location.Path), true);
}

bool UBiomeStrategyData::MoveTargetProviderEarlier()
{
	FProviderEditorLocation Location;
	if (!FindProviderEditorLocation(*this, Location) || Location.Provider == nullptr)
	{
		return SetAuthoringToolResult(*this, FString::Printf(TEXT("Provider path '%s' was not found."), *AuthoringTargetProviderPath), false);
	}

	if (Location.OwningChildArray == nullptr || Location.OwningIndex == INDEX_NONE)
	{
		return SetAuthoringToolResult(*this, TEXT("Root/prototype providers cannot be reordered as child siblings."), false);
	}

	if (Location.OwningIndex <= 0)
	{
		return SetAuthoringToolResult(*this, TEXT("Target provider is already first in its child list."), false);
	}

#if WITH_EDITOR
	Modify();
#endif

	const int32 NewIndex = Location.OwningIndex - 1;
	const FName DebugName = Location.Provider->DebugName;
	Location.OwningChildArray->Swap(Location.OwningIndex, NewIndex);
	AuthoringTargetProviderPath = MakeChildProviderPath(Location.ParentPath, NewIndex, DebugName);
	MarkAuthoringToolMutation(*this);
	return SetAuthoringToolResult(*this, FString::Printf(TEXT("Moved provider to '%s'."), *AuthoringTargetProviderPath), true);
}

bool UBiomeStrategyData::MoveTargetProviderLater()
{
	FProviderEditorLocation Location;
	if (!FindProviderEditorLocation(*this, Location) || Location.Provider == nullptr)
	{
		return SetAuthoringToolResult(*this, FString::Printf(TEXT("Provider path '%s' was not found."), *AuthoringTargetProviderPath), false);
	}

	if (Location.OwningChildArray == nullptr || Location.OwningIndex == INDEX_NONE)
	{
		return SetAuthoringToolResult(*this, TEXT("Root/prototype providers cannot be reordered as child siblings."), false);
	}

	if (Location.OwningIndex >= Location.OwningChildArray->Num() - 1)
	{
		return SetAuthoringToolResult(*this, TEXT("Target provider is already last in its child list."), false);
	}

#if WITH_EDITOR
	Modify();
#endif

	const int32 NewIndex = Location.OwningIndex + 1;
	const FName DebugName = Location.Provider->DebugName;
	Location.OwningChildArray->Swap(Location.OwningIndex, NewIndex);
	AuthoringTargetProviderPath = MakeChildProviderPath(Location.ParentPath, NewIndex, DebugName);
	MarkAuthoringToolMutation(*this);
	return SetAuthoringToolResult(*this, FString::Printf(TEXT("Moved provider to '%s'."), *AuthoringTargetProviderPath), true);
}

bool UBiomeStrategyData::PromoteTargetProviderOneLevel()
{
	FProviderEditorLocation Location;
	if (!FindProviderEditorLocation(*this, Location) || Location.Provider == nullptr)
	{
		return SetAuthoringToolResult(*this, FString::Printf(TEXT("Provider path '%s' was not found."), *AuthoringTargetProviderPath), false);
	}

	if (Location.OwningChildArray == nullptr || Location.ParentOwningChildArray == nullptr || Location.ParentOwningIndex == INDEX_NONE)
	{
		return SetAuthoringToolResult(*this, TEXT("Target provider cannot be promoted because its parent is not a child provider."), false);
	}

#if WITH_EDITOR
	Modify();
#endif

	FInstancedStruct MovingProvider = (*Location.OwningChildArray)[Location.OwningIndex];
	const FName DebugName = Location.Provider->DebugName;
	Location.OwningChildArray->RemoveAt(Location.OwningIndex);
	const int32 NewIndex = Location.ParentOwningIndex + 1;
	Location.ParentOwningChildArray->Insert(MoveTemp(MovingProvider), NewIndex);
	AuthoringTargetProviderPath = MakeChildProviderPath(Location.GrandparentPath, NewIndex, DebugName);
	MarkAuthoringToolMutation(*this);
	return SetAuthoringToolResult(*this, FString::Printf(TEXT("Promoted provider to '%s'."), *AuthoringTargetProviderPath), true);
}

bool UBiomeStrategyData::DemoteTargetProviderIntoPreviousSibling()
{
	FProviderEditorLocation Location;
	if (!FindProviderEditorLocation(*this, Location) || Location.Provider == nullptr)
	{
		return SetAuthoringToolResult(*this, FString::Printf(TEXT("Provider path '%s' was not found."), *AuthoringTargetProviderPath), false);
	}

	if (Location.OwningChildArray == nullptr || Location.OwningIndex <= 0)
	{
		return SetAuthoringToolResult(*this, TEXT("Target provider has no previous sibling to demote into."), false);
	}

	FFoundationProviderDefinition* PreviousSibling = (*Location.OwningChildArray)[Location.OwningIndex - 1].GetMutablePtr<FFoundationProviderDefinition>();
	if (PreviousSibling == nullptr)
	{
		return SetAuthoringToolResult(*this, TEXT("Previous sibling is not a foundation provider."), false);
	}

#if WITH_EDITOR
	Modify();
#endif

	FInstancedStruct MovingProvider = (*Location.OwningChildArray)[Location.OwningIndex];
	const FName DebugName = Location.Provider->DebugName;
	const FString NewParentPath = MakeChildProviderPath(Location.ParentPath, Location.OwningIndex - 1, PreviousSibling->DebugName);
	Location.OwningChildArray->RemoveAt(Location.OwningIndex);
	PreviousSibling = (*Location.OwningChildArray)[Location.OwningIndex - 1].GetMutablePtr<FFoundationProviderDefinition>();
	const int32 NewIndex = PreviousSibling != nullptr ? PreviousSibling->ChildFoundations.Add(MoveTemp(MovingProvider)) : INDEX_NONE;
	if (NewIndex == INDEX_NONE)
	{
		return SetAuthoringToolResult(*this, TEXT("Previous sibling became unavailable during demotion."), false);
	}

	AuthoringTargetProviderPath = MakeChildProviderPath(NewParentPath, NewIndex, DebugName);
	MarkAuthoringToolMutation(*this);
	return SetAuthoringToolResult(*this, FString::Printf(TEXT("Demoted provider to '%s'."), *AuthoringTargetProviderPath), true);
}

bool UBiomeStrategyData::DemoteTargetProviderIntoNextSibling()
{
	FProviderEditorLocation Location;
	if (!FindProviderEditorLocation(*this, Location) || Location.Provider == nullptr)
	{
		return SetAuthoringToolResult(*this, FString::Printf(TEXT("Provider path '%s' was not found."), *AuthoringTargetProviderPath), false);
	}

	if (Location.OwningChildArray == nullptr || Location.OwningIndex == INDEX_NONE || Location.OwningIndex >= Location.OwningChildArray->Num() - 1)
	{
		return SetAuthoringToolResult(*this, TEXT("Target provider has no next sibling to demote into."), false);
	}

	FFoundationProviderDefinition* NextSibling = (*Location.OwningChildArray)[Location.OwningIndex + 1].GetMutablePtr<FFoundationProviderDefinition>();
	if (NextSibling == nullptr)
	{
		return SetAuthoringToolResult(*this, TEXT("Next sibling is not a foundation provider."), false);
	}

#if WITH_EDITOR
	Modify();
#endif

	FInstancedStruct MovingProvider = (*Location.OwningChildArray)[Location.OwningIndex];
	const FName DebugName = Location.Provider->DebugName;
	const FName NextSiblingName = NextSibling->DebugName;
	Location.OwningChildArray->RemoveAt(Location.OwningIndex);
	NextSibling = (*Location.OwningChildArray)[Location.OwningIndex].GetMutablePtr<FFoundationProviderDefinition>();
	const int32 NewIndex = NextSibling != nullptr ? NextSibling->ChildFoundations.Add(MoveTemp(MovingProvider)) : INDEX_NONE;
	if (NewIndex == INDEX_NONE)
	{
		return SetAuthoringToolResult(*this, TEXT("Next sibling became unavailable during demotion."), false);
	}

	const FString NewParentPath = MakeChildProviderPath(Location.ParentPath, Location.OwningIndex, NextSiblingName);
	AuthoringTargetProviderPath = MakeChildProviderPath(NewParentPath, NewIndex, DebugName);
	MarkAuthoringToolMutation(*this);
	return SetAuthoringToolResult(*this, FString::Printf(TEXT("Demoted provider to '%s'."), *AuthoringTargetProviderPath), true);
}

void UBiomeStrategyData::GetReservationsForBiomeTag(
	const FGameplayTag BiomeTag,
	TArray<const FReservationDefinition*>& OutReservations) const
{
	if (!BiomeTag.IsValid())
	{
		return;
	}

	for (const FReservationDefinition& Reservation : RootFoundationProvider.Reservations)
	{
		if (Reservation.BiomeTag == BiomeTag)
		{
			OutReservations.Add(&Reservation);
		}
	}
}

FResolvedStrategyContext UBiomeStrategyData::ResolveStrategyContext(UObject* Creator) const
{
	FResolvedStrategyContext Context;
	Context.ScaleContext = FWorldGenScaleContextResolver::Resolve(
		Creator,
		&ScaleOverride,
		bUseScaleOverride);

	if (AChunkWorldCore* ChunkWorld = ResolveChunkWorldFromCreator(Creator))
	{
		Context.StrategySeed = ChunkWorld->Seed;
		Context.bSeedResolvedFromChunkWorld = true;
		Context.bSeedResolvedFromFallback = false;
		Context.DebugSourcePath = ChunkWorld->GetPathName();
		return Context;
	}

	Context.StrategySeed = FallbackStrategySeed;
	Context.bSeedResolvedFromChunkWorld = false;
	Context.bSeedResolvedFromFallback = true;
	Context.DebugSourcePath = Creator != nullptr ? Creator->GetPathName() : GetPathName();
	return Context;
}

bool UBiomeStrategyData::ResolveBiomeStrategy(UObject* Creator, FResolvedBiomeStrategy& OutResolvedStrategy) const
{
	OutResolvedStrategy = FResolvedBiomeStrategy();
	const FBiomeStrategyValidationResult ValidationResult = ValidateStrategy();
	if (ValidationResult.HasErrors())
	{
		return false;
	}

	OutResolvedStrategy.Context = ResolveStrategyContext(Creator);
	auto ResolveProvider = [&OutResolvedStrategy](
		const FFoundationProviderDefinition& Provider,
		const FString& DebugPath,
		const int32 HierarchyDepth,
		auto&& ResolveProviderRef) -> void
	{
		if (Provider.ProviderType == EFoundationProviderType::MultiInstance)
		{
			const FMultiInstanceFoundationPayload* MultiPayload = Provider.ProviderPayload.GetPtr<FMultiInstanceFoundationPayload>();
			const FFoundationProviderPrototypeDefinition* PrototypeProvider = MultiPayload != nullptr
				? MultiPayload->PrototypeProvider.GetPtr<FFoundationProviderPrototypeDefinition>()
				: nullptr;
			if (MultiPayload == nullptr || PrototypeProvider == nullptr)
			{
				return;
			}

			const int32 InstanceCount = FMath::Max(1, MultiPayload->InstanceCount);
			for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
			{
				FRandomStream InstanceStream = MakeInstanceRandomStream(OutResolvedStrategy.Context, DebugPath, InstanceIndex);
				const float UniformScale = GetRangedInstanceValue(InstanceStream, MultiPayload->MinUniformScale, MultiPayload->MaxUniformScale);
				const float ZOffsetBlocks = GetRangedInstanceValue(InstanceStream, MultiPayload->MinZOffsetBlocks, MultiPayload->MaxZOffsetBlocks);
				const FVector2D OffsetXY = GetMultiInstanceOffsetXY(*MultiPayload, InstanceIndex);
				FFoundationProviderDefinition ResolvedProvider = MaterializePrototypeProvider(Provider, *PrototypeProvider);
				TransformProviderDefinition(ResolvedProvider, FVector(OffsetXY.X, OffsetXY.Y, ZOffsetBlocks), UniformScale);

				const FString InstancePath = FString::Printf(
					TEXT("%s.Instances[%d:%s]"),
					*DebugPath,
					InstanceIndex,
					*ResolvedProvider.DebugName.ToString());
				ResolveProviderRef(ResolvedProvider, InstancePath, HierarchyDepth + 1, ResolveProviderRef);
			}

			for (int32 ChildIndex = 0; ChildIndex < Provider.ChildFoundations.Num(); ++ChildIndex)
			{
				const FFoundationProviderDefinition* ChildProvider = Provider.ChildFoundations[ChildIndex].GetPtr<FFoundationProviderDefinition>();
				if (ChildProvider == nullptr)
				{
					continue;
				}

				const FString ChildPath = FString::Printf(
					TEXT("%s.ChildFoundations[%d:%s]"),
					*DebugPath,
					ChildIndex,
					*ChildProvider->DebugName.ToString());
				ResolveProviderRef(*ChildProvider, ChildPath, HierarchyDepth + 1, ResolveProviderRef);
			}
			return;
		}

		FResolvedFoundationProviderInstance& Instance = OutResolvedStrategy.ProviderInstances.AddDefaulted_GetRef();
		Instance.DebugName = Provider.DebugName;
		Instance.DebugPath = DebugPath;
		Instance.ContributionType = Provider.ContributionType;
		Instance.ProviderType = Provider.ProviderType;
		Instance.BiomeTag = Provider.BiomeTag;
		Instance.HierarchyDepth = HierarchyDepth;
		Instance.Provider = &Provider;
		Instance.ProviderSnapshot = Provider;
		ExpandMultiInstanceChildrenForSnapshot(Instance.ProviderSnapshot, OutResolvedStrategy.Context, DebugPath);
		Instance.QueryCapabilities = GetProviderQueryCapabilities(Provider);
		QueryProviderBounds(Provider, DebugPath, Instance.Bounds);

		for (int32 ChildIndex = 0; ChildIndex < Provider.ChildFoundations.Num(); ++ChildIndex)
		{
			const FFoundationProviderDefinition* ChildProvider = Provider.ChildFoundations[ChildIndex].GetPtr<FFoundationProviderDefinition>();
			if (ChildProvider == nullptr)
			{
				continue;
			}

			const FString ChildPath = FString::Printf(
				TEXT("%s.ChildFoundations[%d:%s]"),
				*DebugPath,
				ChildIndex,
				*ChildProvider->DebugName.ToString());
			ResolveProviderRef(*ChildProvider, ChildPath, HierarchyDepth + 1, ResolveProviderRef);
		}
	};

	ResolveProvider(RootFoundationProvider, TEXT("RootFoundationProvider"), 0, ResolveProvider);
	OutResolvedStrategy.bValid = true;
	return true;
}

bool UBiomeStrategyData::QueryFoundationProviderBounds(UObject* Creator, FFoundationProviderBounds& OutBounds) const
{
	OutBounds = FFoundationProviderBounds();
	FResolvedBiomeStrategy ResolvedStrategy;
	if (!ResolveBiomeStrategy(Creator, ResolvedStrategy) || ResolvedStrategy.ProviderInstances.IsEmpty())
	{
		return false;
	}

	OutBounds = ResolvedStrategy.ProviderInstances[0].Bounds;
	return OutBounds.bValid;
}

void UBiomeStrategyData::QueryFoundationProviderBounds(UObject* Creator, TArray<FFoundationProviderBounds>& OutBounds) const
{
	FResolvedBiomeStrategy ResolvedStrategy;
	if (!ResolveBiomeStrategy(Creator, ResolvedStrategy))
	{
		return;
	}

	for (const FResolvedFoundationProviderInstance& ProviderInstance : ResolvedStrategy.ProviderInstances)
	{
		if (ProviderInstance.Bounds.bValid)
		{
			OutBounds.Add(ProviderInstance.Bounds);
		}
	}
}

bool UBiomeStrategyData::QueryFoundationSurface(
	UObject* Creator,
	const FFoundationSurfaceQuery& Query,
	FResolvedFoundationSurface& OutSurface) const
{
	const FResolvedStrategyContext Context = ResolveStrategyContext(Creator);
	return QueryProviderSurface(RootFoundationProvider, TEXT("RootFoundationProvider"), Query, Context.ScaleContext, OutSurface);
}

void UBiomeStrategyData::QueryTaggedReservationFields(
	UObject* Creator,
	const FBox& AuthoredBlockBounds,
	TArray<FTaggedReservationField>& OutFields) const
{
	FResolvedBiomeStrategy ResolvedStrategy;
	if (!ResolveBiomeStrategy(Creator, ResolvedStrategy))
	{
		return;
	}

	for (const FResolvedFoundationProviderInstance& ProviderInstance : ResolvedStrategy.ProviderInstances)
	{
		const FFoundationProviderDefinition* Provider = &ProviderInstance.ProviderSnapshot;
		if (Provider->ContributionType == EFoundationContributionType::SubtractiveVoid)
		{
			continue;
		}

		for (const FReservationDefinition& Reservation : Provider->Reservations)
		{
			const FSurfaceAnchorReservationPayload* ReservationPayload = Reservation.ReservationPayload.GetPtr<FSurfaceAnchorReservationPayload>();
			if (ReservationPayload == nullptr)
			{
				continue;
			}

			const FSurfaceAnchorReservationPayload ResolvedPayload = ResolveProviderLocalReservationPayload(*Provider, *ReservationPayload);
			FResolvedFoundationSurface FoundationSurface;
			const bool bResolvedSurface = QueryProviderSurface(
				*Provider,
				ProviderInstance.DebugPath,
				MakeSurfaceAnchorFoundationQuery(ResolvedPayload),
				ResolvedStrategy.Context.ScaleContext,
				FoundationSurface);
			if (!bResolvedSurface)
			{
				continue;
			}

			const FBox ReservationBounds = MakeSurfaceAnchorFieldBounds(ResolvedPayload, EReservationFieldKind::Reservation, FoundationSurface.SurfaceZBlock);
			if (ShouldIncludeFieldBounds(AuthoredBlockBounds, ReservationBounds))
			{
				FTaggedReservationField& Field = OutFields.AddDefaulted_GetRef();
				PopulateResolvedFieldFromBounds(ProviderInstance, Reservation, EReservationFieldKind::Reservation, ReservationBounds, Field);
			}

			if (Reservation.bEnableSpawnReservation)
			{
				const FBox SpawnBounds = MakeSurfaceAnchorFieldBounds(ResolvedPayload, EReservationFieldKind::Spawn, FoundationSurface.SurfaceZBlock);
				if (ShouldIncludeFieldBounds(AuthoredBlockBounds, SpawnBounds))
				{
					FTaggedReservationField& Field = OutFields.AddDefaulted_GetRef();
					PopulateResolvedFieldFromBounds(ProviderInstance, Reservation, EReservationFieldKind::Spawn, SpawnBounds, Field);
				}
			}
		}
	}
}

FBiomeStrategyValidationResult UBiomeStrategyData::ValidateStrategy() const
{
	struct FReservationTerrainGroupInfo
	{
		const UScriptStruct* TerrainStruct = nullptr;
		FString DebugPath;
	};

	FBiomeStrategyValidationResult Result;
	TMap<FGameplayTag, FReservationTerrainGroupInfo> ReservationTerrainByBiomeTag;

	auto AddError = [&Result](const FString& DebugPath, const FString& Message, const FString& FixText)
	{
		Result.AddIssue(EBiomeStrategyValidationSeverity::Error, DebugPath, Message, FixText);
	};

	auto AddWarning = [&Result](const FString& DebugPath, const FString& Message, const FString& FixText)
	{
		Result.AddIssue(EBiomeStrategyValidationSeverity::Warning, DebugPath, Message, FixText);
	};

	auto ValidateSurfaceAnchorPayload = [&AddError](
		const FSurfaceAnchorReservationPayload& SurfacePayload,
		const FString& ReservationPath) -> bool
	{
			if (!SurfacePayload.TerrainPayload.IsValid())
			{
				AddError(
					ReservationPath,
					TEXT("Surface Anchor reservation has no terrain payload."),
					TEXT("Choose Flat Surface Anchor Terrain Payload for a flat pad, Blended Support Surface Anchor Terrain Payload for an edge-eased pad, or Noisy Surface Anchor Terrain Payload for varied terrain."));
				return false;
			}

			if (const FSurfaceAnchorBlendedSupportTerrainPayload* BlendedTerrain = SurfacePayload.TerrainPayload.GetPtr<FSurfaceAnchorBlendedSupportTerrainPayload>())
			{
				if (BlendedTerrain->EdgeBlendWidthBlocks < 0.0f)
				{
					AddError(
						ReservationPath,
						TEXT("Blended Support Surface Anchor Terrain has a negative edge blend width."),
						TEXT("Set Edge Blend Width Blocks to zero or a positive authored-block value."));
					return false;
				}
				return true;
			}

			if (const FSurfaceAnchorNoisyTerrainPayload* NoisyTerrain = SurfacePayload.TerrainPayload.GetPtr<FSurfaceAnchorNoisyTerrainPayload>())
			{
				if (NoisyTerrain->SurfaceNoiseAmplitude < 0.0f
					|| NoisyTerrain->SurfaceNoiseScale < 0.0f
					|| (NoisyTerrain->bUseTerraces && NoisyTerrain->TerraceStepHeight <= 0.0f)
					|| NoisyTerrain->TerraceSmoothness < 0.0f)
				{
					AddError(
						ReservationPath,
						TEXT("Noisy Surface Anchor Terrain has invalid noise or terrace settings."),
						TEXT("Use zero or positive noise amplitude/scale, positive Terrace Step Height, and zero or positive Terrace Smoothness."));
					return false;
				}
				return true;
			}

			if (SurfacePayload.TerrainPayload.GetPtr<FSurfaceAnchorFlatTerrainPayload>() == nullptr)
			{
				AddError(
					ReservationPath,
					TEXT("Surface Anchor reservation uses an unsupported terrain payload."),
					TEXT("Set Terrain Payload to Flat Surface Anchor Terrain Payload, Blended Support Surface Anchor Terrain Payload, or Noisy Surface Anchor Terrain Payload."));
				return false;
			}

			return true;
		};

	auto ValidateMultiInstanceSurfaceAnchorPayload = [&AddError, &ValidateSurfaceAnchorPayload](
		const FReservationDefinition& Reservation,
		const FMultiInstanceSurfaceAnchorReservationPayload& MultiPayload,
		const FString& ReservationPath) -> bool
	{
		bool bValid = true;
		if (MultiPayload.InstanceCount <= 0)
		{
			AddError(
				ReservationPath,
				TEXT("Multi Instance Surface Anchor reservation has no instances."),
				TEXT("Set Instance Count to at least 1."));
			bValid = false;
		}
		if (MultiPayload.MinUniformScale <= 0.0f || MultiPayload.MaxUniformScale <= 0.0f)
		{
			AddError(
				ReservationPath,
				TEXT("Multi Instance Surface Anchor reservation has a non-positive uniform scale range."),
				TEXT("Set both Min Uniform Scale and Max Uniform Scale above zero."));
			bValid = false;
		}

		const FSurfaceAnchorReservationPayload* PrototypePayload = MultiPayload.PrototypeSurfaceAnchor.GetPtr<FSurfaceAnchorReservationPayload>();
		if (PrototypePayload == nullptr)
		{
			AddError(
				ReservationPath,
				TEXT("Multi Instance Surface Anchor reservation has no surface-anchor prototype."),
				TEXT("Set Prototype Surface Anchor to Surface Anchor Reservation Payload."));
			return false;
		}

		bValid = ValidateSurfaceAnchorPayload(*PrototypePayload, ReservationPath + TEXT(".PrototypeSurfaceAnchor")) && bValid;

		if (MultiPayload.bRequireNoOverlap)
		{
			const FVector AnchorExtent = GetAnchorExtent(*PrototypePayload);
			const float WorstCaseRadius = FMath::Max(AnchorExtent.X, AnchorExtent.Y)
				* FMath::Max(MultiPayload.MinUniformScale, MultiPayload.MaxUniformScale)
				+ MultiPayload.FootprintPaddingBlocks;
			for (int32 A = 0; A < MultiPayload.InstanceCount; ++A)
			{
				for (int32 B = A + 1; B < MultiPayload.InstanceCount; ++B)
				{
					const float Distance = FVector2D::Distance(
						GetMultiInstanceOffsetXY(MultiPayload, A),
						GetMultiInstanceOffsetXY(MultiPayload, B));
					if (Distance < WorstCaseRadius * 2.0f)
					{
						AddError(
							ReservationPath,
							FString::Printf(TEXT("Multi Instance Surface Anchor reservation '%s' arrangement can overlap at worst-case scale."), *Reservation.DebugName.ToString()),
							TEXT("Increase spacing/ring radius, reduce instance count, reduce Max Uniform Scale, shrink the prototype footprint, or lower Footprint Padding Blocks."));
						return false;
					}
				}
			}
		}

		return bValid;
	};

	auto ValidateReservation = [&AddError, &AddWarning, &ReservationTerrainByBiomeTag, &ValidateSurfaceAnchorPayload, &ValidateMultiInstanceSurfaceAnchorPayload](
		const FReservationDefinition& Reservation,
		const FString& ReservationPath,
		const FFoundationProviderQueryCapabilities& ProviderCapabilities) -> void
	{
		if (Reservation.DebugName.IsNone())
		{
			AddError(
				ReservationPath,
				TEXT("Reservation has no Debug Name."),
				TEXT("Set a human-readable Debug Name so validation messages can identify this reservation."));
		}
		if (!Reservation.BiomeTag.IsValid())
		{
			AddError(
				ReservationPath,
				TEXT("Reservation has no Biome Tag."),
				TEXT("Set the Biome Tag to the reservation biome row this field should contribute to, for example Biome.Reservation.Center."));
		}
		if (!Reservation.ReservationPayload.IsValid())
		{
			AddError(
				ReservationPath,
				TEXT("Reservation has no reservation payload."),
				TEXT("Choose the payload matching the Reservation Type. SurfaceAnchor requires Surface Anchor Reservation Payload."));
			return;
		}

		const FSurfaceAnchorReservationPayload* SurfacePayload = nullptr;
		if (Reservation.ReservationType == EReservationType::SurfaceAnchor)
		{
			SurfacePayload = Reservation.ReservationPayload.GetPtr<FSurfaceAnchorReservationPayload>();
			if (SurfacePayload == nullptr)
			{
				AddError(
					ReservationPath,
					TEXT("Surface Anchor reservation does not use Surface Anchor Reservation Payload."),
					TEXT("Replace the Reservation Payload with Surface Anchor Reservation Payload or change the Reservation Type."));
				return;
			}

			if (!ValidateSurfaceAnchorPayload(*SurfacePayload, ReservationPath))
			{
				return;
			}
		}
		else if (Reservation.ReservationType == EReservationType::MultiInstanceSurfaceAnchor)
		{
			const FMultiInstanceSurfaceAnchorReservationPayload* MultiPayload = Reservation.ReservationPayload.GetPtr<FMultiInstanceSurfaceAnchorReservationPayload>();
			if (MultiPayload == nullptr)
			{
				AddError(
					ReservationPath,
					TEXT("Multi Instance Surface Anchor reservation does not use Multi Instance Surface Anchor Reservation Payload."),
					TEXT("Replace the Reservation Payload with Multi Instance Surface Anchor Reservation Payload or change the Reservation Type."));
				return;
			}

			if (!ValidateMultiInstanceSurfaceAnchorPayload(Reservation, *MultiPayload, ReservationPath))
			{
				return;
			}

			SurfacePayload = MultiPayload->PrototypeSurfaceAnchor.GetPtr<FSurfaceAnchorReservationPayload>();
		}

		if (!Reservation.BiomeTag.IsValid() || SurfacePayload == nullptr)
		{
			return;
		}

		const UScriptStruct* TerrainStruct = SurfacePayload->TerrainPayload.GetScriptStruct();
		if (FReservationTerrainGroupInfo* ExistingInfo = ReservationTerrainByBiomeTag.Find(Reservation.BiomeTag))
		{
			if (ExistingInfo->TerrainStruct != nullptr && TerrainStruct != nullptr && ExistingInfo->TerrainStruct != TerrainStruct)
			{
				AddWarning(
					ReservationPath,
					FString::Printf(
						TEXT("Reservation shares Biome Tag '%s' with '%s' but uses a different terrain payload type."),
						*Reservation.BiomeTag.ToString(),
						*ExistingInfo->DebugPath),
					TEXT("Use separate Biome Tags when grouped reservations need visibly different GenA terrain behavior, or use matching terrain payload types for fields that should share one biome row."));
			}
			return;
		}

		FReservationTerrainGroupInfo GroupInfo;
		GroupInfo.TerrainStruct = TerrainStruct;
		GroupInfo.DebugPath = ReservationPath;
		ReservationTerrainByBiomeTag.Add(Reservation.BiomeTag, GroupInfo);
	};

	auto ValidateFoundationTerrainProfile = [&AddError](const FFoundationProviderDefinition& Provider, const FString& Path) -> void
	{
		if (Provider.ContributionType == EFoundationContributionType::SubtractiveVoid)
		{
			// The terrain profile field is hidden for void providers and ignored by runtime builders.
			return;
		}

		if (Provider.ProviderType == EFoundationProviderType::MultiInstance)
		{
			return;
		}

		if (!Provider.TerrainProfile.IsValid())
		{
			AddError(
				Path,
				TEXT("Additive terrain provider has no terrain profile."),
				*FString::Printf(TEXT("Choose one of the supported terrain profiles for this provider: %s."), *GetSupportedFoundationTerrainProfileNamesForProvider(Provider.ProviderType)));
			return;
		}

		TArray<FFoundationTerrainProfileValidationIssue> TerrainProfileIssues;
		AppendFoundationTerrainProfileValidationIssues(Provider, Path, TerrainProfileIssues);
		for (const FFoundationTerrainProfileValidationIssue& Issue : TerrainProfileIssues)
		{
			AddError(Issue.Path, Issue.Message, Issue.Fix);
		}
	};

	auto ValidateInfinitePlanePayload = [&AddError](const FInfinitePlaneFoundationPayload& PlanePayload, const FString& Path) -> void
	{
		if (PlanePayload.DomainTopPadding < 0.0f || PlanePayload.DomainBottomDepth <= 0.0f)
		{
			AddError(
				Path,
				TEXT("Infinite Plane provider has invalid vertical domain settings."),
				TEXT("Set Domain Top Padding to zero or greater and Domain Bottom Depth above zero."));
		}

		if (!PlanePayload.DomainMask.IsValid())
		{
			AddError(
				Path + TEXT(".DomainMask"),
				TEXT("Infinite Plane provider has no domain mask payload."),
				TEXT("Set Domain Mask to No Domain Mask, Noise Patch Domain Mask, or Repeated Holes Domain Mask."));
			return;
		}

		if (PlanePayload.DomainMask.GetPtr<FInfiniteFoundationNoDomainMaskPayload>() != nullptr)
		{
			return;
		}

		if (const FInfiniteFoundationNoisePatchDomainMaskPayload* NoisePatch = PlanePayload.DomainMask.GetPtr<FInfiniteFoundationNoisePatchDomainMaskPayload>())
		{
			if (NoisePatch->FrequencyScale <= 0.0f || NoisePatch->EdgeSoftness < 0.0f)
			{
				AddError(
					Path + TEXT(".DomainMask"),
					TEXT("Infinite Plane noise patch domain mask has invalid scale or edge softness."),
					TEXT("Set Frequency Scale above zero and Edge Softness to zero or greater."));
			}
			return;
		}

		if (const FInfiniteFoundationRepeatedHolesDomainMaskPayload* RepeatedHoles = PlanePayload.DomainMask.GetPtr<FInfiniteFoundationRepeatedHolesDomainMaskPayload>())
		{
			if (RepeatedHoles->CellSizeBlocks <= 0.0f || RepeatedHoles->HoleRadiusBlocks <= 0.0f || RepeatedHoles->EdgeSoftnessBlocks < 0.0f)
			{
				AddError(
					Path + TEXT(".DomainMask"),
					TEXT("Infinite Plane repeated holes domain mask has invalid cell, hole, or edge settings."),
					TEXT("Set Cell Size Blocks and Hole Radius Blocks above zero, and Edge Softness Blocks to zero or greater."));
				return;
			}
			if (RepeatedHoles->HoleRadiusBlocks >= RepeatedHoles->CellSizeBlocks * 0.5f)
			{
				AddError(
					Path + TEXT(".DomainMask"),
					TEXT("Infinite Plane repeated holes domain mask has holes large enough to merge between cells."),
					TEXT("Keep Hole Radius Blocks below half of Cell Size Blocks, or increase Cell Size Blocks for isolated holes."));
			}
			return;
		}

		AddError(
			Path + TEXT(".DomainMask"),
			TEXT("Infinite Plane provider uses an unsupported domain mask payload."),
			TEXT("Set Domain Mask to No Domain Mask, Noise Patch Domain Mask, or Repeated Holes Domain Mask."));
	};

	auto ValidateVCutPayload = [&AddError](const FVCutFoundationPayload& VCutPayload, const FString& Path) -> void
	{
		if (VCutPayload.HalfLengthBlocks <= 0.0f
			|| VCutPayload.SurfaceHalfWidthBlocks <= 0.0f
			|| VCutPayload.BottomHalfWidthBlocks <= 0.0f
			|| VCutPayload.DepthBlocks <= 0.0f
			|| VCutPayload.EdgeSoftnessBlocks < 0.0f)
		{
			AddError(
				Path,
				TEXT("V Cut Void provider has invalid dimensions."),
				TEXT("Set Half Length, Surface Half Width, Bottom Half Width, and Depth above zero, and Edge Softness to zero or greater."));
		}

		if (VCutPayload.SurfaceHalfWidthBlocks < VCutPayload.BottomHalfWidthBlocks)
		{
			AddError(
				Path,
				TEXT("V Cut Void provider is wider at the bottom than at the surface."),
				TEXT("Set Surface Half Width greater than or equal to Bottom Half Width for a V-shaped cut. Use equal widths only for a straight slot cut."));
		}

		if (VCutPayload.bEnableOrganicVariation)
		{
			if (VCutPayload.EdgeNoiseAmplitudeBlocks < 0.0f
				|| VCutPayload.EdgeNoiseScale <= 0.0f
				|| VCutPayload.CenterlineWanderAmplitudeBlocks < 0.0f
				|| VCutPayload.CenterlineWanderScale <= 0.0f
				|| VCutPayload.DomainWarpAmplitudeBlocks < 0.0f
				|| VCutPayload.DomainWarpFrequency <= 0.0f)
			{
				AddError(
					Path,
					TEXT("V Cut Void organic variation has invalid noise settings."),
					TEXT("Use zero or positive amplitudes, and keep Edge Noise Scale, Centerline Wander Scale, and Domain Warp Frequency above zero."));
			}

			if (VCutPayload.bEnableSteps && (VCutPayload.StepHeightBlocks <= 0.0f || VCutPayload.StepSmoothness < 0.0f))
			{
				AddError(
					Path,
					TEXT("V Cut Void stepped walls have invalid settings."),
					TEXT("Set Step Height above zero and Step Smoothness to zero or greater."));
			}
		}
	};

	auto ValidateProvider = [&AddError, &ValidateReservation, &ValidateFoundationTerrainProfile, &ValidateInfinitePlanePayload, &ValidateVCutPayload](const FFoundationProviderDefinition& Provider, const FString& Path, auto&& ValidateProviderRef) -> void
	{
		if (Provider.DebugName.IsNone())
		{
			AddError(
				Path,
				TEXT("Provider has no Debug Name."),
				TEXT("Set a human-readable Debug Name so validation messages can identify this provider."));
		}

		if (Provider.ContributionType == EFoundationContributionType::AdditiveBiome && !Provider.BiomeTag.IsValid())
		{
			AddError(
				Path,
				TEXT("Additive provider has no Biome Tag."),
				TEXT("Set the Biome Tag to the foundation biome row this provider should contribute to, for example Biome.Foundation.Main."));
		}

		if (Provider.ProviderType == EFoundationProviderType::VCutVoid && Provider.ContributionType != EFoundationContributionType::SubtractiveVoid)
		{
			AddError(
				Path,
				TEXT("V Cut Void provider is not subtractive."),
				TEXT("Set Contribution Type to Subtractive Void. V-cut providers remove parent terrain and do not contribute their own biome."));
		}

		if (!Provider.ProviderPayload.IsValid())
		{
			AddError(
				Path,
				TEXT("Provider has no provider payload."),
				TEXT("Choose the payload matching the Provider Type. Island requires Island Foundation Shape Payload."));
		}
		else if (Provider.ProviderType == EFoundationProviderType::Island && Provider.ProviderPayload.GetPtr<FIslandFoundationShapePayload>() == nullptr)
		{
			AddError(
				Path,
				TEXT("Island provider does not use Island Foundation Shape Payload."),
				TEXT("Replace the Provider Payload with Island Foundation Shape Payload or change the Provider Type."));
		}
		else if (Provider.ProviderType == EFoundationProviderType::InfinitePlane && Provider.ProviderPayload.GetPtr<FInfinitePlaneFoundationPayload>() == nullptr)
		{
			AddError(
				Path,
				TEXT("Infinite Plane provider does not use Infinite Plane Foundation Payload."),
				TEXT("Replace the Provider Payload with Infinite Plane Foundation Payload or change the Provider Type."));
		}
		else if (Provider.ProviderType == EFoundationProviderType::InfinitePlane)
		{
			ValidateInfinitePlanePayload(*Provider.ProviderPayload.GetPtr<FInfinitePlaneFoundationPayload>(), Path);
		}
		else if (Provider.ProviderType == EFoundationProviderType::VCutVoid && Provider.ProviderPayload.GetPtr<FVCutFoundationPayload>() == nullptr)
		{
			AddError(
				Path,
				TEXT("V Cut Void provider does not use V Cut Foundation Payload."),
				TEXT("Replace the Provider Payload with V Cut Foundation Payload or change the Provider Type."));
		}
		else if (Provider.ProviderType == EFoundationProviderType::VCutVoid)
		{
			ValidateVCutPayload(*Provider.ProviderPayload.GetPtr<FVCutFoundationPayload>(), Path);
		}
		else if (Provider.ProviderType == EFoundationProviderType::MultiInstance && Provider.ProviderPayload.GetPtr<FMultiInstanceFoundationPayload>() == nullptr)
		{
			AddError(
				Path,
				TEXT("Multi Instance provider does not use Multi Instance Foundation Payload."),
				TEXT("Replace the Provider Payload with Multi Instance Foundation Payload or change the Provider Type."));
		}
		else if (const FIslandFoundationShapePayload* IslandPayload = Provider.ProviderPayload.GetPtr<FIslandFoundationShapePayload>())
		{
			if (IslandPayload->IslandBody.SideBulgeBlocks < 0.0f
				|| IslandPayload->IslandBody.BottomPointDepthBlocks < 0.0f
				|| IslandPayload->IslandBody.LowerConeSharpness < 0.0f
				|| IslandPayload->IslandBody.LowerConeSharpness > 1.0f)
			{
				AddError(
					Path,
					TEXT("Island body silhouette settings are outside their supported range."),
					TEXT("Set Side Bulge and Bottom Point Depth to zero or positive values, and keep Lower Cone Sharpness between zero and one."));
			}
			if ((IslandPayload->BodyDetail.bEnableRimNoise && IslandPayload->BodyDetail.RimNoiseAmplitude < 0.0f)
				|| (IslandPayload->BodyDetail.bEnableUndersideNoise && IslandPayload->BodyDetail.UndersideNoiseAmplitude < 0.0f)
				|| (IslandPayload->BodyDetail.bEnableBottomSpikeNoise && IslandPayload->BodyDetail.BottomSpikeNoiseAmplitude < 0.0f))
			{
				AddError(
					Path,
					TEXT("Island body detail has a negative amplitude."),
					TEXT("Set rim, underside, and bottom-spike amplitudes to zero or positive authored-block values."));
			}
		}

		ValidateFoundationTerrainProfile(Provider, Path);

		if (Provider.ContributionType == EFoundationContributionType::SubtractiveVoid && Provider.Reservations.Num() > 0)
		{
			AddError(
				Path,
				TEXT("Subtractive void provider has reservations."),
				TEXT("Remove reservations from this provider. Reservations are disabled for void contributions; place crossing/entry fields on an additive provider instead."));
		}

		if (Provider.ProviderType == EFoundationProviderType::MultiInstance)
		{
			const FMultiInstanceFoundationPayload* MultiPayload = Provider.ProviderPayload.GetPtr<FMultiInstanceFoundationPayload>();
			if (Provider.Reservations.Num() > 0)
			{
				AddError(
					Path,
					TEXT("Multi Instance provider has direct reservations."),
					TEXT("Move reservations onto the Prototype Provider to repeat them for every generated instance, or onto a direct child foundation provider to make them fixed to that provider's surface."));
			}
			if (MultiPayload != nullptr)
			{
				if (MultiPayload->InstanceCount <= 0)
				{
					AddError(
						Path,
						TEXT("Multi Instance provider has no instances."),
						TEXT("Set Instance Count to at least 1."));
				}
				if (MultiPayload->MinUniformScale <= 0.0f || MultiPayload->MaxUniformScale <= 0.0f)
				{
					AddError(
						Path,
						TEXT("Multi Instance provider has a non-positive uniform scale range."),
						TEXT("Set both Min Uniform Scale and Max Uniform Scale above zero."));
				}
				if (!MultiPayload->PrototypeProvider.IsValid())
				{
					AddError(
						Path,
						TEXT("Multi Instance provider has no prototype provider."),
						TEXT("Set Prototype Provider to the foundation provider configuration that should be repeated."));
				}
				else if (const FFoundationProviderPrototypeDefinition* PrototypeProvider = MultiPayload->PrototypeProvider.GetPtr<FFoundationProviderPrototypeDefinition>())
				{
					const FFoundationProviderDefinition MaterializedPrototype = MaterializePrototypeProvider(Provider, *PrototypeProvider);
					ValidateProviderRef(
						MaterializedPrototype,
						MakePrototypeProviderPath(Path),
						ValidateProviderRef);

					if (MultiPayload->bRequireNoOverlap)
					{
						if (const FIslandFoundationShapePayload* PrototypeIsland = PrototypeProvider->ProviderPayload.GetPtr<FIslandFoundationShapePayload>())
						{
							const float WorstCaseRadius = (PrototypeIsland->IslandBody.TopRadius + GetStrategyIslandDomainRadiusPaddingBlocks(*PrototypeIsland))
								* FMath::Max(MultiPayload->MinUniformScale, MultiPayload->MaxUniformScale)
								+ MultiPayload->FootprintPaddingBlocks;
							for (int32 A = 0; A < MultiPayload->InstanceCount; ++A)
							{
								for (int32 B = A + 1; B < MultiPayload->InstanceCount; ++B)
								{
									const float Distance = FVector2D::Distance(
										GetMultiInstanceOffsetXY(*MultiPayload, A),
										GetMultiInstanceOffsetXY(*MultiPayload, B));
									if (Distance < WorstCaseRadius * 2.0f)
									{
										AddError(
											Path,
											TEXT("Multi Instance provider arrangement can overlap at worst-case scale."),
											TEXT("Increase spacing/ring radius, reduce instance count, reduce Max Uniform Scale, or lower Footprint Padding Blocks."));
										A = MultiPayload->InstanceCount;
										break;
									}
								}
							}
						}
					}
				}
				else
				{
					AddError(
						Path,
						TEXT("Multi Instance prototype slot is not a Foundation Provider Definition."),
						TEXT("Replace Prototype Provider with Foundation Provider Definition."));
				}
			}
			for (int32 ChildIndex = 0; ChildIndex < Provider.ChildFoundations.Num(); ++ChildIndex)
			{
				const FInstancedStruct& ChildProviderStruct = Provider.ChildFoundations[ChildIndex];
				const FFoundationProviderDefinition* ChildProvider = ChildProviderStruct.GetPtr<FFoundationProviderDefinition>();
				if (ChildProvider == nullptr)
				{
					AddError(
						FString::Printf(TEXT("%s.ChildFoundations[%d]"), *Path, ChildIndex),
						TEXT("Child foundation slot is not a Foundation Provider Definition."),
						TEXT("Replace the child slot with Foundation Provider Definition."));
					continue;
				}

				const FString ChildPath = FString::Printf(TEXT("%s.ChildFoundations[%d:%s]"), *Path, ChildIndex, *ChildProvider->DebugName.ToString());
				ValidateProviderRef(*ChildProvider, ChildPath, ValidateProviderRef);
			}
			return;
		}

		const FFoundationProviderQueryCapabilities ProviderCapabilities = GetProviderQueryCapabilities(Provider);
		if (Provider.ContributionType != EFoundationContributionType::SubtractiveVoid)
		{
			for (int32 ReservationIndex = 0; ReservationIndex < Provider.Reservations.Num(); ++ReservationIndex)
			{
				const FReservationDefinition& Reservation = Provider.Reservations[ReservationIndex];
				const FString ReservationPath = FString::Printf(TEXT("%s.Reservations[%d:%s]"), *Path, ReservationIndex, *Reservation.DebugName.ToString());
				ValidateReservation(Reservation, ReservationPath, ProviderCapabilities);
			}

			for (int32 A = 0; A < Provider.Reservations.Num(); ++A)
			{
				TArray<FBox> ABoxes;
				CollectReservationFootprintBoxes(Provider.Reservations[A], ABoxes);
				for (int32 B = A + 1; B < Provider.Reservations.Num(); ++B)
				{
					if (Provider.Reservations[A].BiomeTag == Provider.Reservations[B].BiomeTag)
					{
						continue;
					}

					TArray<FBox> BBoxes;
					CollectReservationFootprintBoxes(Provider.Reservations[B], BBoxes);
					for (const FBox& ABox : ABoxes)
					{
						if (BBoxes.ContainsByPredicate([&ABox](const FBox& BBox) { return BoxesOverlapXY(ABox, BBox); }))
						{
							AddError(
								Path,
								FString::Printf(
									TEXT("Reservations '%s' and '%s' overlap but contribute to different Biome Tags."),
									*Provider.Reservations[A].DebugName.ToString(),
									*Provider.Reservations[B].DebugName.ToString()),
								TEXT("Move one reservation, give compatible overlapping reservations the same Biome Tag so they union, or place the conflicting area under an explicit child provider hierarchy."));
							B = Provider.Reservations.Num();
							break;
						}
					}
				}
			}
		}

		for (int32 ChildIndex = 0; ChildIndex < Provider.ChildFoundations.Num(); ++ChildIndex)
		{
			const FInstancedStruct& ChildProviderStruct = Provider.ChildFoundations[ChildIndex];
			const FFoundationProviderDefinition* ChildProvider = ChildProviderStruct.GetPtr<FFoundationProviderDefinition>();
			if (ChildProvider == nullptr)
			{
				AddError(
					FString::Printf(TEXT("%s.ChildFoundations[%d]"), *Path, ChildIndex),
					TEXT("Child foundation slot is not a Foundation Provider Definition."),
					TEXT("Replace the child slot with Foundation Provider Definition."));
				continue;
			}

			const FString ChildPath = FString::Printf(TEXT("%s.ChildFoundations[%d:%s]"), *Path, ChildIndex, *ChildProvider->DebugName.ToString());
			ValidateProviderRef(*ChildProvider, ChildPath, ValidateProviderRef);
		}

		for (int32 A = 0; A < Provider.ChildFoundations.Num(); ++A)
		{
			const FFoundationProviderDefinition* AProvider = Provider.ChildFoundations[A].GetPtr<FFoundationProviderDefinition>();
			if (AProvider == nullptr)
			{
				continue;
			}

			FFoundationProviderBounds ABounds;
			if (!QueryProviderBounds(*AProvider, FString(), ABounds))
			{
				continue;
			}

			for (int32 B = A + 1; B < Provider.ChildFoundations.Num(); ++B)
			{
				const FFoundationProviderDefinition* BProvider = Provider.ChildFoundations[B].GetPtr<FFoundationProviderDefinition>();
				if (BProvider == nullptr)
				{
					continue;
				}

				FFoundationProviderBounds BBounds;
				if (QueryProviderBounds(*BProvider, FString(), BBounds) && BoxesOverlapXY(FBox(ABounds.AuthoredMinBlock, ABounds.AuthoredMaxBlock), FBox(BBounds.AuthoredMinBlock, BBounds.AuthoredMaxBlock)))
				{
					AddError(
						Path,
						FString::Printf(
							TEXT("Child foundation providers '%s' and '%s' overlap as siblings."),
							*AProvider->DebugName.ToString(),
							*BProvider->DebugName.ToString()),
						TEXT("If overlap is intentional, move one provider under the other so hierarchy carve/union rules define ownership, or use a validated multi-instance arrangement with enough spacing."));
				}
			}
		}
	};

	ValidateProvider(RootFoundationProvider, TEXT("RootFoundationProvider"), ValidateProvider);
	return Result;
}

FNodeLink UBiomeStrategyData::BuildBiomeNoise(
	UFastNoiseEditor* Editor,
	UObject* Creator,
	const EBiomeNoiseSlot NoiseSlot,
	const FGameplayTag BiomeTag) const
{
	const FBiomeStrategyValidationResult ValidationResult = ValidateStrategy();
	if (ValidationResult.HasErrors())
	{
		const FString ValidationKey = FString::Printf(TEXT("%s|ValidationErrors"), *GetPathName());
		if (ShouldLogStrategyDiagnostics(ValidationKey))
		{
			for (const FBiomeStrategyValidationIssue& Issue : ValidationResult.Issues)
			{
				if (Issue.Severity == EBiomeStrategyValidationSeverity::Error)
				{
					UE_LOG(
						LogPorismDIMsWorldGeneratorExtension,
						Verbose,
						TEXT("Biome strategy '%s' is invalid at %s: %s Fix: %s"),
						*GetPathName(),
						*Issue.DebugPath,
						*Issue.Message,
						*Issue.FixText);
				}
			}
		}
		return Editor != nullptr ? Editor->Constant(0.0f) : FNodeLink();
	}

	FResolvedBiomeStrategy ResolvedStrategy;
	if (!ResolveBiomeStrategy(Creator, ResolvedStrategy))
	{
		return Editor != nullptr ? Editor->Constant(0.0f) : FNodeLink();
	}

	const FResolvedWorldGenScaleContext& ScaleContext = ResolvedStrategy.Context.ScaleContext;
	LogWorldGenBiomeRowDiagnostics(Creator);
	LogStrategyDiagnostics(*this, ScaleContext, NoiseSlot, BiomeTag);

	if (!BiomeTag.IsValid())
	{
		return Editor != nullptr ? Editor->Constant(0.0f) : FNodeLink();
	}

	return NoiseSlot == EBiomeNoiseSlot::GenA
		? UIslandBiomeFastNoiseLibrary::BuildResolvedBiomeGenA(Editor, ResolvedStrategy, BiomeTag)
		: UIslandBiomeFastNoiseLibrary::BuildResolvedBiomeDomain(Editor, ResolvedStrategy, BiomeTag);
}

#if WITH_EDITOR
void UBiomeStrategyData::NormalizeProviderPayloadsForEditor()
{
	if (NormalizeProviderDefinitionForEditor(RootFoundationProvider))
	{
		MarkPackageDirty();
	}
}

void UBiomeStrategyData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Keep paired type/payload fields synchronized so data assets cannot retain stale payload structs after type changes.
	NormalizeProviderPayloadsForEditor();
}

EDataValidationResult UBiomeStrategyData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	const FBiomeStrategyValidationResult ValidationResult = ValidateStrategy();
	for (const FBiomeStrategyValidationIssue& Issue : ValidationResult.Issues)
	{
		const FString Message = Issue.FixText.IsEmpty()
			? FString::Printf(TEXT("%s: %s"), *Issue.DebugPath, *Issue.Message)
			: FString::Printf(TEXT("%s: %s Fix: %s"), *Issue.DebugPath, *Issue.Message, *Issue.FixText);
		if (Issue.Severity == EBiomeStrategyValidationSeverity::Error)
		{
			Context.AddError(FText::FromString(Message));
			Result = EDataValidationResult::Invalid;
		}
		else
		{
			Context.AddWarning(FText::FromString(Message));
		}
	}
	return Result;
}
#endif
