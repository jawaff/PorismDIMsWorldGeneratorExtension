// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "FastNoise/FastNoiseEditor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Biome/Noise/Strategy/BiomeStrategyData.h"

#include "IslandBiomeFastNoiseLibrary.generated.h"

/** FastNoiseEditor helpers for island biome domain and terrain fields. */
UCLASS()
class PORISMDIMSWORLDGENERATOREXTENSION_API UIslandBiomeFastNoiseLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Builds a cheap positive-inside island domain envelope without terrain-detail noise. */
	UFUNCTION(BlueprintCallable, Category = "Island Noise", meta = (DefaultToSelf = "Editor"))
	static FNodeLink BuildIslandEnvelopeDomain(UFastNoiseEditor* Editor, const UBiomeStrategyData* Strategy);
	static FNodeLink BuildIslandEnvelopeDomain(UFastNoiseEditor* Editor, const UBiomeStrategyData* Strategy, const FResolvedWorldGenScaleContext* ScaleContext);

	/** Builds a positive-inside shallow reservation domain centered on the resolved island surface. */
	UFUNCTION(BlueprintCallable, Category = "Island Noise", meta = (DefaultToSelf = "Editor"))
	static FNodeLink BuildReservationDomain(UFastNoiseEditor* Editor, const UBiomeStrategyData* Strategy, FGameplayTag BiomeTag);
	static FNodeLink BuildReservationDomain(UFastNoiseEditor* Editor, const UBiomeStrategyData* Strategy, FGameplayTag BiomeTag, const FResolvedWorldGenScaleContext* ScaleContext);

	/** Builds the combined provider/reservation domain for every contribution matching BiomeTag. */
	static FNodeLink BuildBiomeDomain(UFastNoiseEditor* Editor, const UBiomeStrategyData* Strategy, FGameplayTag BiomeTag, const FResolvedWorldGenScaleContext* ScaleContext);

	/** Builds the combined domain from resolved provider instances so deterministic provider expansion affects runtime FNE output. */
	static FNodeLink BuildResolvedBiomeDomain(UFastNoiseEditor* Editor, const FResolvedBiomeStrategy& ResolvedStrategy, FGameplayTag BiomeTag);

	/** Builds a foundation domain with enabled reservation domains carved out. */
	UFUNCTION(BlueprintCallable, Category = "Island Noise", meta = (DefaultToSelf = "Editor"))
	static FNodeLink BuildFoundationDomain(UFastNoiseEditor* Editor, const UBiomeStrategyData* Strategy);
	static FNodeLink BuildFoundationDomain(UFastNoiseEditor* Editor, const UBiomeStrategyData* Strategy, const FResolvedWorldGenScaleContext* ScaleContext);

	/** Builds a spawn-safe mask inside the first matching reservation that enables spawn output. */
	UFUNCTION(BlueprintCallable, Category = "Island Noise", meta = (DefaultToSelf = "Editor"))
	static FNodeLink BuildSpawnDomain(UFastNoiseEditor* Editor, const UBiomeStrategyData* Strategy, FGameplayTag BiomeTag);
	static FNodeLink BuildSpawnDomain(UFastNoiseEditor* Editor, const UBiomeStrategyData* Strategy, FGameplayTag BiomeTag, const FResolvedWorldGenScaleContext* ScaleContext);

	/** Builds the foundation terrain GenA field using Porism terrain polarity: low/negative is solid, high/positive is air. */
	UFUNCTION(BlueprintCallable, Category = "Island Noise", meta = (DefaultToSelf = "Editor"))
	static FNodeLink BuildFoundationGenA(UFastNoiseEditor* Editor, const UBiomeStrategyData* Strategy);
	static FNodeLink BuildFoundationGenA(UFastNoiseEditor* Editor, const UBiomeStrategyData* Strategy, const FResolvedWorldGenScaleContext* ScaleContext);

	/** Builds a reservation-surface GenA field using Porism terrain polarity. */
	UFUNCTION(BlueprintCallable, Category = "Island Noise", meta = (DefaultToSelf = "Editor"))
	static FNodeLink BuildReservationGenA(UFastNoiseEditor* Editor, const UBiomeStrategyData* Strategy, FGameplayTag BiomeTag);
	static FNodeLink BuildReservationGenA(UFastNoiseEditor* Editor, const UBiomeStrategyData* Strategy, FGameplayTag BiomeTag, const FResolvedWorldGenScaleContext* ScaleContext);

	/** Builds the combined provider/reservation GenA terrain for every contribution matching BiomeTag. */
	static FNodeLink BuildBiomeGenA(UFastNoiseEditor* Editor, const UBiomeStrategyData* Strategy, FGameplayTag BiomeTag, const FResolvedWorldGenScaleContext* ScaleContext);

	/** Builds the combined GenA terrain from resolved provider instances so deterministic provider expansion affects runtime FNE output. */
	static FNodeLink BuildResolvedBiomeGenA(UFastNoiseEditor* Editor, const FResolvedBiomeStrategy& ResolvedStrategy, FGameplayTag BiomeTag);

	/** Resolves the island provider's authored-block surface for provider-relative reservation queries. */
	static bool QueryIslandFoundationSurface(
		const UBiomeStrategyData* Strategy,
		const FFoundationSurfaceQuery& Query,
		const FResolvedWorldGenScaleContext& ScaleContext,
		FResolvedFoundationSurface& OutSurface);

	/** Resolves one island provider instance's authored-block surface for provider-relative reservation queries. */
	static bool QueryIslandProviderSurface(
		const FFoundationProviderDefinition& Provider,
		const FString& DebugPath,
		const FFoundationSurfaceQuery& Query,
		const FResolvedWorldGenScaleContext& ScaleContext,
		FResolvedFoundationSurface& OutSurface);
};
