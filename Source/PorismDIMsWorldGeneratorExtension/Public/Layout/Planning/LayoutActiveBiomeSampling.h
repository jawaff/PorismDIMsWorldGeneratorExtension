// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "FastNoise/FastNoiseEditor.h"
#include "Layout/Planning/LayoutNoiseCoordinateLibrary.h"
#include "Layout/Planning/LayoutReservationPocketPlanning.h"

#include <vector>

class UWorldGenDef;

/** One active Porism biome row compiled for layout planning queries. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutActiveBiomeRowRef
{
	/** Stable DataTable row name when rows come from a table, or biome display name for inline rows. */
	FName RowName;

	/** Human-readable Porism biome name stored on the row. */
	FString BiomeName;

	/** Original row index after hidden rows are filtered out for this planning sampler. */
	int32 SourceRowIndex = INDEX_NONE;
};

/** One sample of the active Porism biome and terrain branch at a block-world position. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutActiveBiomeSample
{
	bool bIsValid = false;
	bool bAnyPositiveDomain = false;
	bool bHasTerrainContribution = false;
	FLayoutActiveBiomeRowRef WinningRow;
	float WinningDomainValue = 0.0f;
	float TerrainContributionMargin = 0.0f;
	float TerrainValue = 0.0f;
	bool bTerrainSolid = false;
	bool bSelectedGenA = true;
};

/** The first solid block found while scanning down through one active Porism biome. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutActiveBiomeSurfaceSample
{
	bool bIsValid = false;
	FIntVector SurfaceBlockWorldPos = FIntVector::ZeroValue;
	FLayoutActiveBiomeSample BiomeSample;
};

/** Compiles WorldGenDef biome rows once and samples the active biome surface for layout planning. */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutActiveBiomeSampler
{
public:
	struct FCompiledRow
	{
		FLayoutActiveBiomeRowRef RowRef;
		FastNoise::SmartNode<> DomainNode;
		FastNoise::SmartNode<> DualSwitchNode;
		FastNoise::SmartNode<> GenANode;
		FastNoise::SmartNode<> GenBNode;
		float DomainOver = 0.0f;
		float Overlap = 0.0f;
		bool bNoiseOnly = true;
	};

	/** Releases transient FastNoise editor node wrappers created during compilation. */
	~FLayoutActiveBiomeSampler();

	/** Compiles all non-hidden biome rows from the supplied WorldGenDef using Porism's node fallback rules. */
	bool Initialize(UObject* Creator, const UWorldGenDef* WorldGenDef, int32 InSeed);

	/** Returns true when this sampler has at least one compiled active row. */
	bool IsInitialized() const { return CompiledRows.Num() > 0; }

	/** Checks configured active row/display names without sampling terrain. A missing row proves ineligibility. */
	bool HasMatchingRow(const FName RowName) const
	{
		return CompiledRows.ContainsByPredicate([RowName](const FCompiledRow& Row)
		{
			return DoesRowMatchName(Row.RowRef, RowName);
		});
	}

	/** Samples Porism's complete Shape-stage density and winning packed-biome owner at one block-world position. */
	bool SampleAtBlockPosition(
		FIntVector BlockWorldPosition,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		FLayoutActiveBiomeSample& OutSample) const;

	/** Samples complete Shape-stage density and reports ownership only when Porism's winning row is compatible. */
	bool SampleAtBlockPositionFromAnyRow(
		FIntVector BlockWorldPosition,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		TConstArrayView<FName> EligibleBiomeRowNames,
		FLayoutActiveBiomeSample& OutSample) const;

	/** Mirrors Porism's per-row DomainOver admission against accumulated biome power. */
	static bool DoesDomainContribute(float DomainValue, float DomainOver, float BiomePower);

	/** Samples a compatible row whose biome noise contributes through Porism's DomainOver blend band. */
	bool SampleContributingTerrainAtBlockPositionFromAnyRow(
		FIntVector BlockWorldPosition,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		TConstArrayView<FName> EligibleBiomeRowNames,
		FLayoutActiveBiomeSample& OutSample) const;

	/** Resolves generated packed biome index to its configured WorldBiomes row. */
	bool TryGetRowRefForSourceIndex(int32 SourceRowIndex, FLayoutActiveBiomeRowRef& OutRowRef) const;

	/** Finds the top solid block owned by the requested biome row within a downward search column. */
	bool FindEligibleBiomeSurface(
		FName EligibleBiomeRowName,
		FIntPoint BlockXY,
		int32 SearchStartZBlockWorld,
		int32 SearchDepthBlocks,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		FLayoutActiveBiomeSurfaceSample& OutSurface) const;

	/** Finds the top solid block owned by any requested biome row within a downward search column. */
	bool FindEligibleBiomeSurfaceFromAnyRow(
		TConstArrayView<FName> EligibleBiomeRowNames,
		FIntPoint BlockXY,
		int32 SearchStartZBlockWorld,
		int32 SearchDepthBlocks,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		FLayoutActiveBiomeSurfaceSample& OutSurface) const;

	/** Finds the top solid block owned by any positive active biome within a downward search column. */
	bool FindAnyActiveBiomeSurface(
		FIntPoint BlockXY,
		int32 SearchStartZBlockWorld,
		int32 SearchDepthBlocks,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		FLayoutActiveBiomeSurfaceSample& OutSurface) const;

	/** Samples a finite XY grid, marking samples eligible only when the requested biome owns a solid surface. */
	bool SampleEligibleBiomeSurfaceGrid(
		FName EligibleBiomeRowName,
		FIntPoint MinBlockXY,
		FIntPoint MaxBlockXY,
		int32 SampleSpacingInBlocks,
		int32 SearchStartZBlockWorld,
		int32 SearchDepthBlocks,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		TArray<FLayoutReservationPocketSample>& OutSamples,
		TMap<FIntPoint, int32>* OutSurfaceZBySampleGrid = nullptr,
		TMap<FIntPoint, int32>* OutSurfaceZByBlockXY = nullptr) const;

private:
	friend struct FLayoutActiveBiomeNoiseSnapshot;

	/** Mirrors Porism's row-name behavior while also accepting the row's display BiomeName. */
	static bool DoesRowMatchName(const FLayoutActiveBiomeRowRef& RowRef, FName RequestedName);

	/** Mirrors Porism's row-name behavior while accepting any of the supplied row/display names. */
	static bool DoesRowMatchAnyName(const FLayoutActiveBiomeRowRef& RowRef, TConstArrayView<FName> RequestedNames);

	/** Evaluates one compiled FastNoise node at Porism's block-world noise coordinate. */
	float EvaluateNode(
		const FastNoise::SmartNode<>& Node,
		FIntVector BlockWorldPosition,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings) const;

	/** Evaluates one node at an already transformed Porism noise coordinate. */
	float EvaluateNodeAtNoiseCoordinate(const FastNoise::SmartNode<>& Node, const FVector& NoiseCoordinate) const;

	TArray<FCompiledRow> CompiledRows;
	FastNoise::SmartNode<> GenOnlyTestNode;
	FastNoise::SmartNode<> WorldGenNode;
	FastNoise::SmartNode<> BiomeOffsetXNode;
	FastNoise::SmartNode<> BiomeOffsetYNode;
	FastNoise::SmartNode<> BiomeOffsetZNode;
	std::vector<FNodeLink> EditorNodes;
	int32 Seed = 0;
};
