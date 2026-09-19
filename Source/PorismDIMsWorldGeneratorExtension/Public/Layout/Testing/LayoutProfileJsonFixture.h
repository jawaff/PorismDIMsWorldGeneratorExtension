// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Planning/LayoutActiveBiomeSampling.h"
#include "Layout/Solver/LayoutProfileSolver.h"

class UObject;
class ULayoutProfileAsset;
class ULayoutRegionContentSetAsset;

/** Imported transient layout fixture assets built from one solver-focused JSON test case. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProfileJsonFixtureAssets
{
	/** Transient profile reconstructed from the fixture. */
	TObjectPtr<ULayoutProfileAsset> Profile = nullptr;

	/** Transient unified content set reconstructed from the fixture when available. */
	TObjectPtr<ULayoutRegionContentSetAsset> ContentSet = nullptr;

	/** Snapshot rebuilt from the imported transient profile. */
	FLayoutProfileSolveSnapshot ProfileSnapshot;

	/** Snapshot rebuilt from the imported transient module set. */
	FLayoutModuleCatalog ModuleCatalog;

	/** Snapshot rebuilt from the imported transient unified content set. */
	FLayoutRegionContentSetSolveSnapshot ContentSetSnapshot;
};

/** Optional stepped-terrain sampling inputs for imported explicit-root fixture replay. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutImportedRootSteppedTerrainSupportContext
{
	/** Eligible biome row filter used when sampling support from an active biome source. */
	FName EligibleBiomeRowName = NAME_None;

	/** World-space center cell used to rebuild the explicit-root support footprint. */
	FIntVector SiteCenterBlockWorldPos = FIntVector::ZeroValue;

	/** Noise-coordinate contract shared with runtime explicit-root stepped sampling. */
	FLayoutNoiseCoordinateSettings CoordinateSettings;

	/** Active biome sampler that resolves terrain support for the imported request. */
	const FLayoutActiveBiomeSampler* ActiveBiomeSampler = nullptr;
};

/** Converts layout profile/module-set assets to and from deterministic JSON fixtures for automation repros. */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProfileJsonFixture
{
public:
	/** Serializes the solver-relevant profile and unified content-set contract to a deterministic recursive JSON string with de-duplicated child/profile/module libraries. */
	static bool ExportToString(
		const ULayoutProfileAsset* Profile,
		const ULayoutRegionContentSetAsset* ContentSet,
		FString& OutJson,
		FString& OutError);

	/** Reconstructs transient layout assets from a JSON fixture string. */
	static bool ImportFromString(
		const FString& Json,
		UObject* Outer,
		FLayoutProfileJsonFixtureAssets& OutAssets,
		TArray<FString>& OutIssues);

	/**
	 * Builds one standalone root solve request from imported fixture assets while
	 * letting fixture replay own the top-level solve budget explicitly. When the
	 * root placement policy or root candidate ids are omitted, stable fixture
	 * replay defaults are derived from the requested root region path. The helper
	 * also stamps one deterministic ordinary-root placement policy so fixture
	 * replay follows the active explicit world-facing request carrier instead of
	 * reconstructing placement behavior from profile terrain fields. The root
	 * solve id always defaults from that same region path for queue/cache trace
	 * parity. When compiled fixture snapshots carry a shared-cell metric that the
	 * reconstructed transient content-set asset no longer republishes, this helper
	 * preserves that metric on the frozen request carrier explicitly instead of
	 * reopening compatibility asset state. Callers may optionally provide a
	 * placement-policy override plus stepped-terrain sampling context so imported
	 * explicit-root replay can stay on the same caller-owned stepped-support seam
	 * as preview and direct runtime explicit-root request construction.
	 */
	static bool BuildImportedRootRequest(
		const FLayoutProfileJsonFixtureAssets& Assets,
		int32 Seed,
		const FString& RegionDebugPath,
		FLayoutRegionSolveRequest& OutRequest,
		const FLayoutRootSolveBudgetSettings& SolveBudget = FLayoutRootSolveBudgetSettings(),
		FName RootPlacementPolicyId = NAME_None,
		FName RootCandidateId = NAME_None,
		int32 TemplatePlacementZOffsetBlocks = 0,
		FString* OutError = nullptr,
		const FLayoutWorldBindingPlacementPolicy* PlacementPolicyOverride = nullptr,
		const FLayoutImportedRootSteppedTerrainSupportContext* SteppedTerrainSupportContext = nullptr);

	/** Serializes the solver-relevant profile and unified content-set contract to a recursive fixture file path. */
	static bool ExportToFile(
		const ULayoutProfileAsset* Profile,
		const ULayoutRegionContentSetAsset* ContentSet,
		const FString& AbsoluteFilePath,
		FString& OutError);

	/** Loads a JSON fixture file and reconstructs transient layout assets from it. */
	static bool ImportFromFile(
		const FString& AbsoluteFilePath,
		UObject* Outer,
		FLayoutProfileJsonFixtureAssets& OutAssets,
		TArray<FString>& OutIssues);
};
