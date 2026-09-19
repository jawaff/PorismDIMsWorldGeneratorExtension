// Copyright 2026 Spotted Loaf Studio

#include "Biome/Noise/Strategy/BiomeStrategyBindingLibrary.h"

#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Engine/DataTable.h"

namespace
{
	struct FBiomeRowRef
	{
		const FBiomeDualData* Biome = nullptr;
		FName RowName;
		int32 RowIndex = INDEX_NONE;
	};

	void GatherBiomeRows(const UWorldGenDef& WorldGenDef, TArray<FBiomeRowRef>& OutRows)
	{
		if (WorldGenDef.WorldBiomesDT != nullptr && WorldGenDef.WorldBiomesDT->GetRowMap().Num() > 0)
		{
			if (WorldGenDef.WorldBiomesDT->GetRowStruct() != FBiomeDualData::StaticStruct())
			{
				return;
			}

			int32 RowIndex = 0;
			for (const TPair<FName, uint8*>& Row : WorldGenDef.WorldBiomesDT->GetRowMap())
			{
				if (Row.Value != nullptr)
				{
					FBiomeRowRef& RowRef = OutRows.AddDefaulted_GetRef();
					RowRef.Biome = reinterpret_cast<const FBiomeDualData*>(Row.Value);
					RowRef.RowName = Row.Key;
					RowRef.RowIndex = RowIndex;
				}
				++RowIndex;
			}
			return;
		}

		for (int32 RowIndex = 0; RowIndex < WorldGenDef.WorldBiomes.Num(); ++RowIndex)
		{
			FBiomeRowRef& RowRef = OutRows.AddDefaulted_GetRef();
			RowRef.Biome = &WorldGenDef.WorldBiomes[RowIndex];
			RowRef.RowIndex = RowIndex;
		}
	}

	const UBiomeFastNoiseEditor* ResolveBiomeEditor(
		const TObjectPtr<UFastNoiseEditor>& RuntimeEditor,
		const TSubclassOf<UFastNoiseEditor>& EditorClass)
	{
		if (const UBiomeFastNoiseEditor* RuntimeBiomeEditor = Cast<UBiomeFastNoiseEditor>(RuntimeEditor.Get()))
		{
			return RuntimeBiomeEditor;
		}

		const UClass* Class = EditorClass.Get();
		return Class != nullptr
			? Cast<UBiomeFastNoiseEditor>(Class->GetDefaultObject())
			: nullptr;
	}

	FString MakeRowDebugPath(const FBiomeRowRef& RowRef)
	{
		return RowRef.RowName.IsNone()
			? FString::Printf(TEXT("WorldBiomes[%d:%s]"), RowRef.RowIndex, RowRef.Biome != nullptr ? *RowRef.Biome->BiomeName : TEXT("None"))
			: FString::Printf(TEXT("WorldBiomesDT[%d:%s]"), RowRef.RowIndex, *RowRef.RowName.ToString());
	}

	int32 CountBoundBiomeInstructions(const FBiomeDualData& Biome)
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

	void AddOrUpdateBinding(
		const FBiomeRowRef& RowRef,
		const UBiomeFastNoiseEditor& BiomeEditor,
		const EBiomeNoiseSlot ExpectedSlot,
		TArray<FBiomeStrategyRowBinding>& OutBindings)
	{
		if (BiomeEditor.Strategy == nullptr || !BiomeEditor.BiomeTag.IsValid() || BiomeEditor.NoiseSlot != ExpectedSlot)
		{
			return;
		}

		FBiomeStrategyRowBinding* Binding = OutBindings.FindByPredicate(
			[&BiomeEditor, &RowRef](const FBiomeStrategyRowBinding& Candidate)
			{
				return Candidate.RowIndex == RowRef.RowIndex
					&& Candidate.Strategy == BiomeEditor.Strategy
					&& Candidate.BiomeTag == BiomeEditor.BiomeTag;
			});
		if (Binding == nullptr)
		{
			Binding = &OutBindings.AddDefaulted_GetRef();
			Binding->RowIndex = RowRef.RowIndex;
			Binding->RowName = RowRef.RowName;
			Binding->BiomeName = RowRef.Biome != nullptr ? RowRef.Biome->BiomeName : FString();
			Binding->Strategy = BiomeEditor.Strategy;
			Binding->BiomeTag = BiomeEditor.BiomeTag;
			if (RowRef.Biome != nullptr)
			{
				Binding->InstructionCount = CountBoundBiomeInstructions(*RowRef.Biome);
				Binding->bNoiseOnly = Binding->InstructionCount == 0;
				Binding->DomainOver = RowRef.Biome->DomainOver;
			}
			Binding->DebugPath = FString::Printf(
				TEXT("%s.%s[%s]"),
				*MakeRowDebugPath(RowRef),
				ExpectedSlot == EBiomeNoiseSlot::DomainNoise ? TEXT("Domain") : TEXT("GenA"),
				*BiomeEditor.BiomeTag.ToString());
		}

		if (ExpectedSlot == EBiomeNoiseSlot::DomainNoise)
		{
			Binding->bHasDomainNoiseBinding = true;
		}
		else if (ExpectedSlot == EBiomeNoiseSlot::GenA)
		{
			Binding->bHasGenABinding = true;
		}
	}
}

void UBiomeStrategyBindingLibrary::QueryBiomeStrategyRowBindings(
	UObject* Creator,
	const UWorldGenDef* WorldGenDef,
	TArray<FBiomeStrategyRowBinding>& OutBindings)
{
	(void)Creator;
	OutBindings.Reset();
	if (WorldGenDef == nullptr)
	{
		return;
	}

	TArray<FBiomeRowRef> RowRefs;
	GatherBiomeRows(*WorldGenDef, RowRefs);
	for (const FBiomeRowRef& RowRef : RowRefs)
	{
		const FBiomeDualData* Biome = RowRef.Biome;
		if (Biome == nullptr || Biome->BiomeHidden)
		{
			continue;
		}

		if (const UBiomeFastNoiseEditor* DomainEditor = ResolveBiomeEditor(Biome->DomainRun, Biome->DomainBP))
		{
			AddOrUpdateBinding(RowRef, *DomainEditor, EBiomeNoiseSlot::DomainNoise, OutBindings);
		}
		if (const UBiomeFastNoiseEditor* GenAEditor = ResolveBiomeEditor(Biome->GenARun, Biome->GenABP))
		{
			AddOrUpdateBinding(RowRef, *GenAEditor, EBiomeNoiseSlot::GenA, OutBindings);
		}
	}
}

void UBiomeStrategyBindingLibrary::QueryWorldGenReservationFields(
	UObject* Creator,
	const UWorldGenDef* WorldGenDef,
	const FBox& AuthoredBlockBounds,
	TArray<FBiomeStrategyReservationFieldBinding>& OutBindings)
{
	OutBindings.Reset();

	TArray<FBiomeStrategyRowBinding> RowBindings;
	QueryBiomeStrategyRowBindings(Creator, WorldGenDef, RowBindings);
	for (const FBiomeStrategyRowBinding& RowBinding : RowBindings)
	{
		if (RowBinding.Strategy == nullptr || !RowBinding.BiomeTag.IsValid())
		{
			continue;
		}

		TArray<FTaggedReservationField> Fields;
		RowBinding.Strategy->QueryTaggedReservationFields(Creator, AuthoredBlockBounds, Fields);
		for (const FTaggedReservationField& Field : Fields)
		{
			if (Field.BiomeTag != RowBinding.BiomeTag)
			{
				continue;
			}

			FBiomeStrategyReservationFieldBinding& FieldBinding = OutBindings.AddDefaulted_GetRef();
			FieldBinding.RowBinding = RowBinding;
			FieldBinding.Field = Field;
		}
	}
}
