// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Layout/Types/LayoutTypes.h"
#include "Misc/DataValidation.h"

#include "LayoutWorldBindingAsset.generated.h"

class ULayoutProfileAsset;

/** One weighted root candidate that shares the same surrounding world-placement policy. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutWorldBindingCandidate
{
	GENERATED_BODY()

	/** Stable identity for this candidate inside one world binding. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Stable identity for this candidate inside one world binding. Planning records and solve requests use this id instead of a legacy row name."))
	FName CandidateId;

	/** Region profile selected when this candidate wins world-binding root selection. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Region profile selected when this candidate wins world-binding root selection. The profile owns structural layout behavior once a root site is chosen."))
	TObjectPtr<ULayoutProfileAsset> LayoutProfile = nullptr;

	/** Relative selection weight for this candidate inside the surrounding world-binding policy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Relative selection weight for this candidate inside the surrounding world-binding policy. If different candidates need different terrain or spacing rules, split them into separate world bindings instead."))
	int32 Weight = 1;

	/** If true, this candidate overrides the binding's default terrain-transition policy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "If true, this candidate overrides the binding's DefaultPlacementPolicy terrain-transition policy. Use this only when one specific root layout needs different foundation fill or perimeter-ramp behavior than the shared binding default."))
	bool bOverrideTerrainTransitionPolicy = false;

	/** Candidate-owned terrain-transition override used when bOverrideTerrainTransitionPolicy is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (EditCondition = "bOverrideTerrainTransitionPolicy", EditConditionHides, ToolTip = "Candidate-owned terrain-transition override used when bOverrideTerrainTransitionPolicy is enabled. Shared surface search still comes from the binding DefaultPlacementPolicy."))
	FLayoutWorldBindingTerrainTransitionPolicy TerrainTransitionPolicyOverride;

	/** Ordinary-root endpoint connector tags exported when this candidate wins root selection. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (Categories = "Layout.Connector", ToolTip = "Ordinary-root endpoint connector tags exported when this candidate wins world-binding root selection. Author world-facing continuation/export participation here instead of on the structural profile so site export policy stays on the binding-owned root contract."))
	FGameplayTagContainer ExportedConnectorTypeTags;
};

/** One weighted continuation candidate owned by one continuation family. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutWorldBindingContinuationCandidate
{
	GENERATED_BODY()

	/** Stable identity for this continuation candidate inside one continuation family. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Stable identity for this continuation candidate inside one continuation family. Runtime continuation request rebuilding uses this id instead of reviving a row-era connector identity."))
	FName CandidateId;

	/** Region profile selected when this continuation candidate wins family selection. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Region profile selected when this continuation candidate wins family selection. This profile must already author continuation-capable behavior and explicit continuation entry intent."))
	TObjectPtr<ULayoutProfileAsset> LayoutProfile = nullptr;

	/** Relative selection weight for this continuation candidate inside its continuation family. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Relative selection weight for this continuation candidate inside its continuation family. Do not author root-only density, export, or spacing policy here."))
	int32 Weight = 1;
};

/** Continuation-family authoring surface for the world-binding continuation pipeline. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutWorldBindingContinuationFamily
{
	GENERATED_BODY()

	/** Stable identity for this continuation family. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Stable identity for this continuation family. Use separate ids for surface paths, bridges, tunnels, and later underground ingress/path families."))
	FName FamilyId;

	/** Named continuation family role so runtime selection does not guess from content. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Named continuation family role. This is the world-binding-side family selection contract, not a broad profile flag."))
	ELayoutWorldBindingContinuationFamilyType FamilyType = ELayoutWorldBindingContinuationFamilyType::SurfacePath;

	/** Exported endpoint connector tag this family is allowed to pair on the active runtime path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (Categories = "Layout.Connector", ToolTip = "Exported endpoint connector tag this family is allowed to pair on the active runtime path. Keep this on the family so surface roads, bridges, and tunnels do not reintroduce blind connector-row pairing or runtime guessing."))
	FGameplayTag EndpointConnectorTypeTag;

	/** Maximum number of paths one resolved site may create through this continuation family. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Maximum number of paths one resolved site may create through this continuation family. Keep this on the family so road, bridge, and tunnel selection does not fall back to the legacy connector-row carrier."))
	int32 MaxConnectionsPerSite = 1;

	/** Maximum allowed endpoint distance between paired sites in continuation-grid cells. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Maximum allowed endpoint distance between paired sites in continuation-grid cells for this family. This replaces the legacy connector-row distance limit on the final world-binding continuation shape."))
	int32 MaxConnectionDistanceInCells = 64;

	/** Future continuation/path candidates that will be selected under this family. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Future continuation/path candidates that will be selected under this family. These are continuation-only candidates and do not inherit root-only distribution or exported-root connector policy."))
	TArray<FLayoutWorldBindingContinuationCandidate> Candidates;

	/** Shared continuation-family path and placement policy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Shared continuation-family path and placement policy. Use this instead of hardening bridge, tunnel, or path behavior into separate row-era carriers."))
	FLayoutWorldBindingContinuationPolicy ContinuationPolicy;

	/** If true, this continuation family overrides the binding's default terrain-transition policy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "If true, this continuation family overrides the binding's DefaultPlacementPolicy terrain-transition policy. Continuations still use the binding-owned shared surface-search contract."))
	bool bOverrideTerrainTransitionPolicy = false;

	/** Continuation-family terrain-transition override used when bOverrideTerrainTransitionPolicy is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (EditCondition = "bOverrideTerrainTransitionPolicy", EditConditionHides, ToolTip = "Continuation-family terrain-transition override used when bOverrideTerrainTransitionPolicy is enabled. Shared surface search still comes from the binding DefaultPlacementPolicy."))
	FLayoutWorldBindingTerrainTransitionPolicy TerrainTransitionPolicyOverride;

	/** Positive solve budget used when this family builds one continuation solve request. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Positive solve budget used when this family builds one continuation solve request. Keep continuation execution policy on the world-binding family instead of reviving the legacy connector row, and avoid zero or negative authored budgets on the active path."))
	FLayoutRootSolveBudgetSettings SolveBudget;
};

/**
 * Authored world-facing root-placement contract that owns shared cell metrics,
 * terrain/site policy, and weighted candidate-region selection.
 */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API ULayoutWorldBindingAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Validates the authored world-binding contract without performing world sampling or solve execution. */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FLayoutValidationResult ValidateWorldBinding() const;

#if WITH_EDITOR
	/** Runs world-binding validation immediately and publishes the result to the editor Message Log. */
	UFUNCTION(CallInEditor, Category = "Layout|Validation", meta = (DisplayName = "Validate Layout World Binding", ToolTip = "Runs this world binding's validation immediately, including selected root candidates, and reports warnings or errors to the Porism Layout Message Log."))
	void ValidateLayoutWorldBindingInEditor() const;

	/** Reports editor validation failures early so invalid world-binding contracts are visible before runtime. */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	/** Stable identity for diagnostics, planning records, and future world-binding publication surfaces. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Identity", meta = (ToolTip = "Stable identity for diagnostics, planning records, and future world-binding publication surfaces. Leave empty only when the asset name is sufficient as the first-pass identifier."))
	FName BindingId;

	/** Optional semantic tags for higher-level grouping or filtering. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Identity", meta = (Categories = "Layout.Binding", ToolTip = "Optional semantic tags for higher-level grouping or filtering. These do not replace the stable BindingId."))
	FGameplayTagContainer BindingTags;

	/** Compatible biome/domain rows that may source ordinary sites for this binding. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Filters", meta = (ToolTip = "Compatible biome/domain row names that may source ordinary sites for this binding. One binding may own multiple biome rows when the surrounding terrain, spacing, and candidate policy are the same."))
	TArray<FName> BiomeRowNames;

	/** Authoritative shared one-cell dimensions for all candidates selected by this binding. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Metrics", meta = (ClampMin = "1", ToolTip = "Authoritative shared one-cell dimensions in block units for every candidate selected by this binding. Root alignment, terrain fit, preview, and realization all consume this one world-binding-owned metric contract."))
	FIntVector BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	/** Request-owned template placement offset applied so the intended floor plane aligns cleanly with terrain. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Metrics", meta = (ToolTip = "Request-owned template placement offset in block units. Use this to align the intended floor plane with terrain so the first realized block layer overlaps the sampled surface cleanly instead of hovering apart."))
	int32 TemplatePlacementZOffsetBlocks = 0;

	/** All automatic root candidates share this edge-to-edge XY gap in normal binding cells. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Planning", meta = (ClampMin = "0", UIMin = "0", DisplayName = "Minimum Root Gap (Cells)", ToolTip = "Minimum free XY gap between authored root footprints across all candidates in this binding. Uses Base Cell Dimensions Blocks, so 5 cells at 5 blocks means 25 free blocks. Zero adds no clearance beyond overlap protection. Direct Layout Generator placement is not constrained by this setting."))
	int32 MinimumRootGapCells = 0;

	/** Stable site acceptance gate for automatic ordinary roots, independent of candidate selection and solves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Planning", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1", ToolTip = "Probability that an automatic candidate site is admitted: 1 fills valid available sites, 0 disables automatic roots for this binding. One deterministic draw per seed, binding and snapped site; retries and LOD changes do not reroll. Not candidate Weight or a percentage of terrain coverage. Explicit Layout Generator and continuations are unaffected."))
	float OccupancyProbability = 1.0f;

	/** Weighted region candidates that compete under this surrounding world-placement policy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Candidates", meta = (ToolTip = "Weighted region candidates that compete under this surrounding world-placement policy. If one candidate needs different terrain or spacing rules, split it into a separate binding instead of overriding that policy here."))
	TArray<FLayoutWorldBindingCandidate> Candidates;

	/** Ordinary-root terrain and alignment policy for this world binding. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Terrain", meta = (ToolTip = "Ordinary-root terrain and alignment policy for this world binding. This is the active world-facing placement-policy carrier instead of the older terrain-conforming request payload."))
	FLayoutWorldBindingPlacementPolicy DefaultPlacementPolicy;

	/** Positive root-attempt solve budget used when this binding builds one ordinary or explicit root solve request. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Execution", meta = (ToolTip = "Positive root-attempt solve budget used when this binding builds one ordinary or explicit root solve request. This stays lean and world-facing: it controls one candidate attempt without reintroducing broad profile-owned execution policy, and the active authored binding path rejects zero or negative budgets."))
	FLayoutRootSolveBudgetSettings SolveBudget;

	/** Continuation/path families authored on the world binding for surface paths, bridges, tunnels, and later underground routing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Continuation", meta = (ToolTip = "Future continuation/path families stubbed now so surface paths, bridges, tunnels, and later underground ingress can land on the final world-binding asset shape instead of another row-era adapter."))
	TArray<FLayoutWorldBindingContinuationFamily> ContinuationFamilies;
};
