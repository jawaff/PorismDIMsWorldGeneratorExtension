// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FastNoise/FastNoiseEditor.h"
#include "GameplayTagContainer.h"
#include "Biome/Noise/WorldGenScaleContext.h"
#include "Misc/DataValidation.h"
#include "StructUtils/InstancedStruct.h"

#include "BiomeStrategyData.generated.h"

/** Porism noise slot selected by a shared-strategy biome FastNoiseEditor wrapper. */
UENUM(BlueprintType)
enum class EBiomeNoiseSlot : uint8
{
	/** DomainNoise controls whether this biome exists at a sampled position. */
	DomainNoise,

	/** GenA controls terrain density for positions owned by this biome. */
	GenA
};

/** Contribution behavior for a foundation provider within its parent branch. */
UENUM(BlueprintType)
enum class EFoundationContributionType : uint8
{
	/** Adds or unions authored terrain into a concrete biome. */
	AdditiveBiome,

	/** Removes terrain from the parent branch without adding a replacement biome. */
	SubtractiveVoid
};

/** Concrete foundation provider family. */
UENUM(BlueprintType)
enum class EFoundationProviderType : uint8
{
	/** Single floating-island provider backed by an Island Foundation Shape Payload. */
	Island,

	/** Infinite X/Y plane provider with a provider-local flat surface. */
	InfinitePlane,

	/** Finite subtractive V-cut provider backed by a V Cut Foundation Payload. */
	VCutVoid,

	/** Deterministic repeated arrangement of one prototype foundation provider. */
	MultiInstance
};

/** Deterministic arrangement mode for a multi-instance foundation provider. */
UENUM(BlueprintType)
enum class EMultiInstanceFoundationArrangement : uint8
{
	/** Places instances along the X axis around Arrangement Center. */
	Line,

	/** Places instances in horizontal rows and columns around Arrangement Center. */
	Grid,

	/** Places instances on a horizontal ring around Arrangement Center. */
	Ring
};

/** High-level reservation family used to keep strategy data extensible. */
UENUM(BlueprintType)
enum class EReservationType : uint8
{
	/** A deterministic shallow reservation volume centered on an intended terrain surface. */
	SurfaceAnchor,

	/** Deterministic repeated arrangement of one prototype surface-anchor reservation. */
	MultiInstanceSurfaceAnchor
};

/** Surface query capability exposed by a foundation provider family. */
UENUM(BlueprintType)
enum class EFoundationProviderSurfaceQueryMode : uint8
{
	/** Provider cannot resolve surface-bound reservations. */
	Unsupported,

	/** Provider exposes a finite authored-block surface. */
	Finite,

	/** Provider exposes repeat cells that can resolve bounded local surfaces. */
	CellBounded,

	/** Provider exposes an unbounded authored-block surface. */
	Infinite
};

/** Volume/bounds capability exposed by a foundation provider family. */
UENUM(BlueprintType)
enum class EFoundationProviderVolumeQueryMode : uint8
{
	/** Provider cannot report authored-block volume/bounds. */
	Unsupported,

	/** Provider reports finite authored-block bounds. */
	Finite,

	/** Provider is unbounded on at least one axis. */
	Infinite
};

/** Generic surface sampling policy used by reservation fields that bind to a foundation provider. */
UENUM(BlueprintType)
enum class EFoundationSurfaceSampleMode : uint8
{
	/** Resolve from the provider surface at the requested center XY. */
	CenterSample,

	/** Resolve from the average provider surface across the requested footprint. */
	AverageSamples,

	/** Resolve from the highest provider surface across the requested footprint. */
	MaxSamples,

	/** Resolve from the highest provider surface at footprint corners or equivalent edge samples. */
	CornerMaxSamples
};

/** Runtime/query kind for a resolved tagged reservation field. */
UENUM(BlueprintType)
enum class EReservationFieldKind : uint8
{
	/** Primary buildable reservation field. */
	Reservation,

	/** Spawn-readable sub-field inside or near a reservation. */
	Spawn
};

/** Severity for one biome strategy validation issue. */
UENUM(BlueprintType)
enum class EBiomeStrategyValidationSeverity : uint8
{
	/** Tuning or authoring concern that should be reviewed but does not block runtime construction. */
	Warning,

	/** Invalid configuration that blocks runtime FNE construction. */
	Error
};

/** Structured validation issue with enough context for editor tooling and tests. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FBiomeStrategyValidationIssue
{
	GENERATED_BODY()

	/** Whether this issue blocks runtime strategy construction. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	EBiomeStrategyValidationSeverity Severity = EBiomeStrategyValidationSeverity::Error;

	/** Human-readable provider/reservation path that produced the issue. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FString DebugPath;

	/** Short description of the invalid or suspicious configuration. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FString Message;

	/** Actionable authoring guidance for resolving the issue. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FString FixText;
};

/** Aggregated validation result for a biome strategy asset. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FBiomeStrategyValidationResult
{
	GENERATED_BODY()

	/** All validation issues discovered while walking the provider hierarchy. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	TArray<FBiomeStrategyValidationIssue> Issues;

	/** Adds a structured validation issue. */
	void AddIssue(EBiomeStrategyValidationSeverity Severity, const FString& DebugPath, const FString& Message, const FString& FixText);

	/** True when any issue blocks runtime strategy construction. */
	bool HasErrors() const;

	/** True when any non-blocking tuning warnings were produced. */
	bool HasWarnings() const;
};

/** Base family for strategy-level payloads consumed by concrete biome noise builders. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FFoundationPayloadBase
{
	GENERATED_BODY()
};

/** Base family for additive foundation GenA terrain profiles. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FFoundationTerrainProfilePayloadBase
{
	GENERATED_BODY()
};

/** Base family for reservation-shape payloads consumed by concrete biome noise builders. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FReservationPayloadBase
{
	GENERATED_BODY()
};

/** Generic reservation metadata shared by domain, terrain, planning, and spawn outputs. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FReservationDefinition
{
	GENERATED_BODY()

	/** Human-readable name used in validation, tooling, and diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy", meta = (ToolTip = "Human-readable name used in validation, tooling, and diagnostics. Biome selection is controlled by Biome Tag."))
	FName DebugName = TEXT("Center");

	/** Biome grouping tag this reservation contributes to. FNEs select reservation noise by this tag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy", meta = (Categories = "Biome", ToolTip = "Biome grouping tag this reservation contributes to. Every reservation with the same tag is combined into that biome's DomainNoise and GenA."))
	FGameplayTag BiomeTag;

	/** High-level reservation family. The editor keeps the payload synchronized with this value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy", meta = (ToolTip = "High-level reservation family. The editor keeps Reservation Payload synchronized with this value so stale payload structs do not block runtime noise generation."))
	EReservationType ReservationType = EReservationType::SurfaceAnchor;

	/** Concrete reservation-shape payload, such as a surface-anchor reservation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy", meta = (BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.ReservationPayloadBase", ExcludeBaseStruct, ToolTip = "Concrete reservation payload consumed by the selected reservation type. Changing Reservation Type in the editor replaces stale payload data with the matching default payload."))
	FInstancedStruct ReservationPayload;

	/** Tags used by structure generation, validation, and tooling to classify the primary reservation field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Tags", meta = (ToolTip = "Gameplay/strategy tags used by structure generation, validation, and tooling to classify the primary reservation field."))
	FGameplayTagContainer FieldTags;

	/** If true, query APIs expose this reservation's spawn-readable sub-field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Spawn", meta = (ToolTip = "If true, query APIs expose this informational spawn reservation. This does not need to become a Porism biome row unless spawn ownership should affect terrain/material lookup."))
	bool bEnableSpawnReservation = false;

	/** Tags used by spawn systems, validation, and tooling to classify the spawn-readable sub-field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Spawn", meta = (EditCondition = "bEnableSpawnReservation", ToolTip = "Gameplay/strategy tags used by spawn systems, validation, and tooling to classify the spawn-readable sub-field."))
	FGameplayTagContainer SpawnFieldTags;
};

/** Base family for foundation provider definitions stored in provider hierarchies. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FFoundationProviderDefinitionBase
{
	GENERATED_BODY()
};

/** Prototype foundation repeated by a multi-instance provider. Contribution, biome tag, and debug identity come from the owning multi-instance provider. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FFoundationProviderPrototypeDefinition : public FFoundationProviderDefinitionBase
{
	GENERATED_BODY()

	/** Concrete provider family repeated by the owning multi-instance provider. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy", meta = (ToolTip = "Concrete provider family repeated by the owning multi-instance provider. Contribution type and biome tag are inherited from that multi-instance provider."))
	EFoundationProviderType ProviderType = EFoundationProviderType::Island;

	/** Concrete provider payload, such as an island foundation shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy", meta = (BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.FoundationPayloadBase", ExcludeBaseStruct, ToolTip = "Concrete provider payload consumed by the selected foundation builder. For Island providers, use Island Foundation Shape Payload."))
	FInstancedStruct ProviderPayload;

	/** Reusable GenA terrain profile used by the repeated additive provider instances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Terrain", meta = (BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.FoundationTerrainProfilePayloadBase", ExcludeBaseStruct, NoClear, EditCondition = "ProviderType != EFoundationProviderType::MultiInstance && ProviderType != EFoundationProviderType::VCutVoid", EditConditionHides, ToolTip = "Reusable GenA terrain profile used by repeated additive provider instances. Editor normalization replaces empty or provider-incompatible profiles with a provider-appropriate default. V-cut prototypes are subtractive voids and do not use terrain profiles."))
	FInstancedStruct TerrainProfile;

	/** Reservations authored relative to each repeated provider instance's top-center or surface frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy", meta = (ToolTip = "Reservations authored relative to each repeated provider instance's top-center or surface frame."))
	TArray<FReservationDefinition> Reservations;

	/** Child foundation providers repeated with each prototype instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy", meta = (BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.FoundationProviderDefinitionBase", ExcludeBaseStruct, ToolTip = "Child foundation providers repeated with each prototype instance."))
	TArray<FInstancedStruct> ChildFoundations;
};

/** Generic repeated arrangement payload for one prototype foundation provider. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FMultiInstanceFoundationPayload : public FFoundationPayloadBase
{
	GENERATED_BODY()

	/** Prototype shape/terrain/reservations copied for each resolved instance. Contribution and biome tag come from this multi-instance provider. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Multi Instance", meta = (BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.FoundationProviderPrototypeDefinition", ToolTip = "Prototype shape/terrain/reservations copied for each resolved instance. Contribution type, biome tag, and debug identity come from this multi-instance provider; generic instance transforms are applied afterward."))
	FInstancedStruct PrototypeProvider;

	/** Deterministic horizontal arrangement used to place prototype instances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Multi Instance", meta = (ToolTip = "Deterministic horizontal arrangement used to place prototype instances."))
	EMultiInstanceFoundationArrangement Arrangement = EMultiInstanceFoundationArrangement::Line;

	/** Number of resolved instances to create from the prototype. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Multi Instance", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Number of resolved instances to create from the prototype."))
	int32 InstanceCount = 2;

	/** Center of the whole arrangement in authored block XY coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Multi Instance", meta = (DisplayName = "Arrangement Center XY (Blocks)", ToolTip = "Center of the whole arrangement in authored block coordinates."))
	FVector2D ArrangementCenterXY = FVector2D::ZeroVector;

	/** Horizontal spacing between line/grid instances in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Multi Instance", meta = (ClampMin = "0.001", UIMin = "0.001", EditCondition = "Arrangement != EMultiInstanceFoundationArrangement::Ring", ToolTip = "Horizontal spacing between line/grid instances in authored blocks. Validation should keep this large enough to avoid overlap. Ring arrangements use Ring Radius instead."))
	float SpacingBlocks = 220.0f;

	/** Number of columns used by Grid arrangement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Multi Instance", meta = (ClampMin = "1", UIMin = "1", EditCondition = "Arrangement == EMultiInstanceFoundationArrangement::Grid", ToolTip = "Number of columns used by Grid arrangement. Rows are derived from Instance Count."))
	int32 GridColumns = 2;

	/** Ring radius used by Ring arrangement in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Multi Instance", meta = (ClampMin = "0.001", UIMin = "0.001", EditCondition = "Arrangement == EMultiInstanceFoundationArrangement::Ring", ToolTip = "Ring radius used by Ring arrangement in authored blocks."))
	float RingRadiusBlocks = 220.0f;

	/** Minimum random Z offset per instance in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Multi Instance|Variation", meta = (ToolTip = "Minimum deterministic per-instance Z offset in authored blocks. Uses the strategy seed when random variation is needed."))
	float MinZOffsetBlocks = 0.0f;

	/** Maximum random Z offset per instance in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Multi Instance|Variation", meta = (ToolTip = "Maximum deterministic per-instance Z offset in authored blocks. Uses the strategy seed when random variation is needed."))
	float MaxZOffsetBlocks = 0.0f;

	/** Minimum uniform geometric scale applied to each prototype instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Multi Instance|Variation", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Minimum deterministic uniform geometric scale applied to each prototype instance. This does not override the strategy-wide worldgen scale context."))
	float MinUniformScale = 1.0f;

	/** Maximum uniform geometric scale applied to each prototype instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Multi Instance|Variation", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Maximum deterministic uniform geometric scale applied to each prototype instance. This does not override the strategy-wide worldgen scale context."))
	float MaxUniformScale = 1.0f;

	/** Extra authored-block padding used by validation when checking instance overlap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Multi Instance|Validation", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Extra authored-block padding used by validation when checking instance overlap."))
	float FootprintPaddingBlocks = 0.0f;

	/** If true, validation rejects finite arrangements whose worst-case instance footprints overlap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Multi Instance|Validation", meta = (ToolTip = "If true, validation rejects finite arrangements whose worst-case instance footprints overlap."))
	bool bRequireNoOverlap = true;
};

/** Foundation provider node that owns concrete terrain plus provider-relative reservations. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FFoundationProviderDefinition : public FFoundationProviderDefinitionBase
{
	GENERATED_BODY()

	FFoundationProviderDefinition();

	/** Human-readable name used in validation, tooling, and diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy", meta = (ToolTip = "Human-readable name used in validation, tooling, and diagnostics. It is not used as a stable runtime identifier."))
	FName DebugName = TEXT("Root");

	/** Whether this provider adds terrain to a biome or subtracts terrain from its parent branch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy", meta = (EditCondition = "ProviderType != EFoundationProviderType::VCutVoid", ToolTip = "Whether this provider adds terrain to a biome or subtracts terrain from its parent branch. V-cut providers are always subtractive voids and do not expose reservations."))
	EFoundationContributionType ContributionType = EFoundationContributionType::AdditiveBiome;

	/** Biome grouping tag this provider contributes to when additive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy", meta = (Categories = "Biome", EditCondition = "ContributionType == EFoundationContributionType::AdditiveBiome", EditConditionHides, ToolTip = "Biome grouping tag this provider contributes to. FNEs select provider noise by this tag."))
	FGameplayTag BiomeTag;

	/** Concrete provider family. The editor keeps the payload synchronized with this value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy", meta = (ToolTip = "Concrete provider family. The editor keeps Provider Payload synchronized with this value so stale payload structs do not block runtime noise generation."))
	EFoundationProviderType ProviderType = EFoundationProviderType::Island;

	/** Concrete provider payload, such as an island foundation shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy", meta = (BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.FoundationPayloadBase", ExcludeBaseStruct, ToolTip = "Concrete provider payload consumed by the selected foundation builder. Changing Provider Type in the editor replaces stale payload data with the matching default payload."))
	FInstancedStruct ProviderPayload;

	/** Reusable GenA terrain profile used by additive providers that generate visible terrain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Terrain", meta = (BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.FoundationTerrainProfilePayloadBase", ExcludeBaseStruct, NoClear, EditCondition = "ContributionType == EFoundationContributionType::AdditiveBiome && ProviderType != EFoundationProviderType::MultiInstance", EditConditionHides, ToolTip = "Reusable GenA terrain profile used by additive providers that generate visible terrain. Editor normalization replaces empty or provider-incompatible profiles with a provider-appropriate default."))
	FInstancedStruct TerrainProfile;

	/** Reservations authored relative to this provider's top-center/surface frame. Disabled for subtractive void providers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy", meta = (EditCondition = "ContributionType != EFoundationContributionType::SubtractiveVoid", EditConditionHides, ToolTip = "Reservations authored relative to this provider's top-center or surface frame. Disabled for subtractive void providers."))
	TArray<FReservationDefinition> Reservations;

	/** Child foundation providers evaluated relative to this provider branch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy", meta = (BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.FoundationProviderDefinitionBase", ExcludeBaseStruct, ToolTip = "Child foundation providers evaluated relative to this provider branch. Same Biome Tag unions with the parent; different Biome Tags carve and replace the parent branch; subtractive voids carve only."))
	TArray<FInstancedStruct> ChildFoundations;
};

/** Resolved strategy context associated with a specific creator/chunk world. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FResolvedStrategyContext
{
	GENERATED_BODY()

	/** Resolved block/noise conversion context shared by all providers in this strategy. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FResolvedWorldGenScaleContext ScaleContext;

	/** Deterministic strategy seed, preferably sourced from the owning chunk world. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	int32 StrategySeed = 0;

	/** True when StrategySeed came from the owning chunk world. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	bool bSeedResolvedFromChunkWorld = false;

	/** True when StrategySeed used the strategy fallback value. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	bool bSeedResolvedFromFallback = false;

	/** Human-readable source path used in diagnostics. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FString DebugSourcePath;
};

/** Authored-block bounds and metadata for one resolved provider. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FFoundationProviderBounds
{
	GENERATED_BODY()

	/** Human-readable provider path for validation and diagnostics. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FString DebugPath;

	/** Minimum authored-block coordinate covered by this provider. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FVector AuthoredMinBlock = FVector::ZeroVector;

	/** Maximum authored-block coordinate covered by this provider. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FVector AuthoredMaxBlock = FVector::ZeroVector;

	/** True when bounds were resolved from a supported provider payload. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	bool bValid = false;
};

/** Generic provider query capabilities used by surface-bound reservations and validation. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FFoundationProviderQueryCapabilities
{
	GENERATED_BODY()

	/** How this provider resolves provider-local surface queries. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	EFoundationProviderSurfaceQueryMode SurfaceQueryMode = EFoundationProviderSurfaceQueryMode::Unsupported;

	/** How this provider reports foundation volume/bounds. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	EFoundationProviderVolumeQueryMode VolumeQueryMode = EFoundationProviderVolumeQueryMode::Unsupported;

};

/** Flattened provider instance resolved from the authored provider hierarchy for one creator/chunk-world context. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FResolvedFoundationProviderInstance
{
	GENERATED_BODY()

	/** Human-readable provider name copied from the authored provider. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FName DebugName;

	/** Human-readable provider path for validation, query results, and diagnostics. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FString DebugPath;

	/** Contribution behavior copied from the authored provider. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	EFoundationContributionType ContributionType = EFoundationContributionType::AdditiveBiome;

	/** Provider family copied from the authored provider. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	EFoundationProviderType ProviderType = EFoundationProviderType::Island;

	/** Biome grouping tag for additive providers. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FGameplayTag BiomeTag;

	/** Depth in the authored provider hierarchy; root is zero. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	int32 HierarchyDepth = 0;

	/** Resolved authored-block bounds for this provider instance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FFoundationProviderBounds Bounds;

	/** Generic surface/volume query capabilities for this resolved provider instance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FFoundationProviderQueryCapabilities QueryCapabilities;

	/** Authored provider definition backing this resolved instance. Valid only while the strategy asset is alive. */
	const FFoundationProviderDefinition* Provider = nullptr;

	/** Resolved provider definition snapshot after generic transforms are applied. */
	FFoundationProviderDefinition ProviderSnapshot;
};

/** Resolved strategy data for one creator/chunk-world context. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FResolvedBiomeStrategy
{
	GENERATED_BODY()

	/** Scale and seed context shared by every resolved provider instance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FResolvedStrategyContext Context;

	/** Flattened provider hierarchy in deterministic parent-before-child order. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	TArray<FResolvedFoundationProviderInstance> ProviderInstances;

	/** True when validation passed and provider instances were resolved. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	bool bValid = false;
};

/** Provider-local surface query used by reservations that bind to a foundation surface. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FFoundationSurfaceQuery
{
	GENERATED_BODY()

	/** Provider-local authored-block center XY to sample. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy")
	FVector2D CenterXY = FVector2D::ZeroVector;

	/** Provider-local authored-block half extent/radii used to build footprint samples. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy")
	FVector2D FootprintHalfExtent = FVector2D::ZeroVector;

	/** If true, corner samples outside an elliptical footprint are ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy")
	bool bEllipticalFootprint = false;

	/** Sampling policy used to collapse footprint samples into one surface height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy")
	EFoundationSurfaceSampleMode SampleMode = EFoundationSurfaceSampleMode::MaxSamples;
};

/** Resolved provider-local foundation surface for a reservation placement query. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FResolvedFoundationSurface
{
	GENERATED_BODY()

	/** True when the associated foundation provider resolved a valid surface. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	bool bValid = false;

	/** Provider/debug path that resolved the surface. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FString DebugPath;

	/** Resolved provider-local authored-block surface Z before reservation lift/offset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	float SurfaceZBlock = 0.0f;

	/** Resolved Porism FastNoise surface Z before reservation lift/offset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	float SurfaceZNoise = 0.0f;

	/** Number of provider surface samples that contributed to the result. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	int32 SampleCount = 0;
};

/** Query result for one resolved tagged reservation or spawn field. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FTaggedReservationField
{
	GENERATED_BODY()

	/** Human-readable reservation name that produced this field. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FName DebugName;

	/** Biome grouping tag this field contributes to. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FGameplayTag BiomeTag;

	/** Runtime/query kind for this field. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	EReservationFieldKind FieldKind = EReservationFieldKind::Reservation;

	/** Human-readable provider/reservation path for validation and diagnostics. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FString DebugPath;

	/** Gameplay/strategy tags used to select this field. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FGameplayTagContainer FieldTags;

	/** Minimum authored-block coordinate covered by this field. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FVector AuthoredMinBlock = FVector::ZeroVector;

	/** Maximum authored-block coordinate covered by this field. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome Strategy")
	FVector AuthoredMaxBlock = FVector::ZeroVector;
};

/** Shared strategy asset referenced by biome-slot FastNoiseEditor wrappers. */
UCLASS(BlueprintType, Blueprintable)
class PORISMDIMSWORLDGENERATOREXTENSION_API UBiomeStrategyData : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Root provider for the strategy's foundation hierarchy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy", meta = (ToolTip = "Root provider for the strategy's foundation hierarchy. Reservations are authored under the provider they are relative to."))
	FFoundationProviderDefinition RootFoundationProvider;

	/** If true, FNE builders use ScaleOverride when no chunk-world creator context is available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Scale", meta = (ToolTip = "If true, FNE builders use ScaleOverride when no chunk-world creator context is available. Runtime generation still prefers the exact chunk world passed to GetNoiseRef."))
	bool bUseScaleOverride = false;

	/** Explicit block-to-noise scale context for editor previews, tests, or non-runtime FNE builds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Scale", meta = (EditCondition = "bUseScaleOverride", ToolTip = "Explicit block-to-noise scale context for editor previews, tests, or non-runtime FNE builds. Runtime generation still prefers the exact chunk world passed to GetNoiseRef."))
	FWorldGenScaleSettings ScaleOverride;

	/** Fallback seed used by editor previews/tests when no chunk-world creator context is available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome Strategy|Seed", meta = (ToolTip = "Fallback deterministic strategy seed used by editor previews or tests when no chunk-world creator context is available. Runtime generation prefers the exact owning chunk world's Seed."))
	int32 FallbackStrategySeed = 0;

	/** Provider debug path targeted by simple editor authoring actions. Use validation/query paths such as RootFoundationProvider.ChildFoundations[0:Name]. */
	UPROPERTY(Transient)
	FString AuthoringTargetProviderPath = TEXT("RootFoundationProvider");

	/** Reservation index targeted inside Authoring Target Provider Path by reservation move actions. */
	UPROPERTY(Transient)
	int32 AuthoringTargetReservationIndex = 0;

	/** Last result reported by the simple editor authoring actions. */
	UPROPERTY(Transient)
	FString LastAuthoringToolResult;

	/** Moves the targeted reservation earlier within its provider's reservation list. */
	UFUNCTION(BlueprintCallable, Category = "Biome Strategy|Authoring Tools")
	bool MoveTargetReservationEarlier();

	/** Moves the targeted reservation later within its provider's reservation list. */
	UFUNCTION(BlueprintCallable, Category = "Biome Strategy|Authoring Tools")
	bool MoveTargetReservationLater();

	/** Moves the targeted child foundation earlier within its sibling child list. */
	UFUNCTION(BlueprintCallable, Category = "Biome Strategy|Authoring Tools")
	bool MoveTargetProviderEarlier();

	/** Moves the targeted child foundation later within its sibling child list. */
	UFUNCTION(BlueprintCallable, Category = "Biome Strategy|Authoring Tools")
	bool MoveTargetProviderLater();

	/** Promotes the targeted child foundation one level up, inserting it after its current parent when possible. */
	UFUNCTION(BlueprintCallable, Category = "Biome Strategy|Authoring Tools")
	bool PromoteTargetProviderOneLevel();

	/** Demotes the targeted child foundation into its previous sibling's child list. */
	UFUNCTION(BlueprintCallable, Category = "Biome Strategy|Authoring Tools")
	bool DemoteTargetProviderIntoPreviousSibling();

	/** Demotes the targeted child foundation into its next sibling's child list. */
	UFUNCTION(BlueprintCallable, Category = "Biome Strategy|Authoring Tools")
	bool DemoteTargetProviderIntoNextSibling();

	/** Finds the first root-provider reservation with the requested biome tag. */
	const FReservationDefinition* FindFirstReservationForBiomeTag(FGameplayTag BiomeTag) const;

	/** Collects root-provider reservations that contribute to the requested biome tag. */
	void GetReservationsForBiomeTag(FGameplayTag BiomeTag, TArray<const FReservationDefinition*>& OutReservations) const;

	/** Resolves scale and seed context from an explicit creator/chunk world or strategy fallbacks. */
	FResolvedStrategyContext ResolveStrategyContext(UObject* Creator) const;

	/** Resolves the current provider hierarchy into deterministic provider instances for one creator/chunk-world context. */
	bool ResolveBiomeStrategy(UObject* Creator, FResolvedBiomeStrategy& OutResolvedStrategy) const;

	/** Queries the current foundation provider's authored-block bounds. */
	bool QueryFoundationProviderBounds(UObject* Creator, FFoundationProviderBounds& OutBounds) const;

	/** Queries every resolved foundation provider's authored-block bounds. */
	void QueryFoundationProviderBounds(UObject* Creator, TArray<FFoundationProviderBounds>& OutBounds) const;

	/** Queries the associated foundation provider for a provider-local surface reference. */
	bool QueryFoundationSurface(UObject* Creator, const FFoundationSurfaceQuery& Query, FResolvedFoundationSurface& OutSurface) const;

	/** Queries tagged reservation/spawn fields intersecting AuthoredBlockBounds; invalid bounds return all resolved fields. */
	void QueryTaggedReservationFields(UObject* Creator, const FBox& AuthoredBlockBounds, TArray<FTaggedReservationField>& OutFields) const;

	/** Returns the generic surface/volume query capabilities for a provider definition. */
	static FFoundationProviderQueryCapabilities GetProviderQueryCapabilities(const FFoundationProviderDefinition& Provider);

	/** Queries authored-block bounds through the provider-family dispatch layer. */
	static bool QueryProviderBounds(const FFoundationProviderDefinition& Provider, const FString& DebugPath, FFoundationProviderBounds& OutBounds);

	/** Queries a provider-local foundation surface through the provider-family dispatch layer. */
	static bool QueryProviderSurface(
		const FFoundationProviderDefinition& Provider,
		const FString& DebugPath,
		const FFoundationSurfaceQuery& Query,
		const FResolvedWorldGenScaleContext& ScaleContext,
		FResolvedFoundationSurface& OutSurface);

	/** Validates provider/reservation configuration before runtime FNE construction. */
	FBiomeStrategyValidationResult ValidateStrategy() const;

	/** Builds one Porism biome noise slot for this strategy; subclasses can override to support new payload families. */
	virtual FNodeLink BuildBiomeNoise(UFastNoiseEditor* Editor, UObject* Creator, EBiomeNoiseSlot NoiseSlot, FGameplayTag BiomeTag) const;

#if WITH_EDITOR
	/** Normalizes editor-authored provider payloads so each selected Provider Type owns the matching payload struct. */
	void NormalizeProviderPayloadsForEditor();

	/** Keeps provider payload structs synchronized when Provider Type values are changed in the details panel. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Validates that the provider hierarchy is configured enough for FNE construction. */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
