// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "FastNoise/FastNoiseEditor.h"
#include "Biome/Noise/Strategy/BiomeStrategyData.h"

#include "BiomeFastNoiseEditor.generated.h"

/** Biome-slot FastNoiseEditor wrapper backed by one shared biome strategy asset. */
UCLASS(BlueprintType, Blueprintable)
class PORISMDIMSWORLDGENERATOREXTENSION_API UBiomeFastNoiseEditor : public UFastNoiseEditor
{
	GENERATED_BODY()

public:
	/** Shared strategy asset that owns foundation providers and provider-relative reservations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Noise", meta = (ToolTip = "Shared strategy asset that owns foundation providers and provider-relative reservations."))
	TObjectPtr<UBiomeStrategyData> Strategy = nullptr;

	/** Porism noise slot this wrapper returns for the selected biome tag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Noise", meta = (ToolTip = "Porism noise slot this wrapper returns for the selected biome tag. Use DomainNoise for WorldGenDef Domain and GenA for WorldGenDef GenA."))
	EBiomeNoiseSlot NoiseSlot = EBiomeNoiseSlot::DomainNoise;

	/** Biome grouping tag selected for this WorldGenDef row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Noise", meta = (Categories = "Biome", ToolTip = "Biome grouping tag selected for this WorldGenDef row. All providers and reservations with this tag contribute to this FNE output."))
	FGameplayTag BiomeTag;

	/** Builds the configured biome slot noise field from the shared strategy. */
	virtual FNodeLink GetNoiseRef_Implementation(UObject* Creator) override;
};
