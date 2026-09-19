// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Biome/Noise/Strategy/BiomeStrategyData.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "BiomeStrategyBindingLibrary.generated.h"

class UWorldGenDef;

/** Resolved association between one Porism biome row and one biome strategy/tag pair. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FBiomeStrategyRowBinding
{
	GENERATED_BODY()

	/** Index of the WorldGenDef biome row that supplied this binding. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	int32 RowIndex = INDEX_NONE;

	/** DataTable row name when the WorldGenDef uses a WorldBiomes DataTable. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FName RowName;

	/** Porism biome display name copied from the row. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FString BiomeName;

	/** Strategy asset referenced by the row's biome FastNoiseEditor wrapper. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	TObjectPtr<const UBiomeStrategyData> Strategy = nullptr;

	/** Biome tag selected by the row's biome FastNoiseEditor wrapper. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FGameplayTag BiomeTag;

	/** True when the row has a matching DomainNoise wrapper. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	bool bHasDomainNoiseBinding = false;

	/** True when the row has a matching GenA wrapper. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	bool bHasGenABinding = false;

	/** Count of Porism material or mesh instructions on the row. Zero means Porism compiles it as NoiseOnly. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	int32 InstructionCount = 0;

	/** True when the row has no material or mesh instructions and therefore cannot paint visible biome ownership. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	bool bNoiseOnly = true;

	/** Porism domain-overlap value copied from the row for runtime visibility diagnostics. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	float DomainOver = 0.0f;

	/** Human-readable row/wrapper path for diagnostics. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FString DebugPath;
};

/** Resolved reservation field associated with a concrete Porism biome row binding. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FBiomeStrategyReservationFieldBinding
{
	GENERATED_BODY()

	/** Biome row binding that exposed this field. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FBiomeStrategyRowBinding RowBinding;

	/** Tagged reservation or spawn field resolved from the row's strategy/tag pair. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FTaggedReservationField Field;
};

/** Query helpers that map explicit Porism WorldGenDef biome rows back to strategy-authored fields. */
UCLASS()
class PORISMDIMSWORLDGENERATOREXTENSION_API UBiomeStrategyBindingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Collects every biome strategy/tag binding explicitly referenced by WorldGenDef biome rows. */
	UFUNCTION(BlueprintCallable, Category = "Biome Strategy")
	static void QueryBiomeStrategyRowBindings(
		UObject* Creator,
		const UWorldGenDef* WorldGenDef,
		TArray<FBiomeStrategyRowBinding>& OutBindings);

	/** Queries reservation/spawn fields associated with strategy-backed WorldGenDef biome rows. */
	UFUNCTION(BlueprintCallable, Category = "Biome Strategy")
	static void QueryWorldGenReservationFields(
		UObject* Creator,
		const UWorldGenDef* WorldGenDef,
		const FBox& AuthoredBlockBounds,
		TArray<FBiomeStrategyReservationFieldBinding>& OutBindings);
};
