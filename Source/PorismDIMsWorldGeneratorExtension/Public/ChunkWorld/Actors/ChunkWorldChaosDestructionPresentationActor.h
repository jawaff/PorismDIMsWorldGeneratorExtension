// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Field/FieldSystemTypes.h"
#include "GameFramework/Actor.h"
#include "ChunkWorld/Actors/ChunkWorldDestructionActorInterface.h"

#include "ChunkWorldChaosDestructionPresentationActor.generated.h"

class UFieldSystemComponent;
class UGeometryCollection;
class UGeometryCollectionComponent;
class USceneComponent;

/** Fracture trigger shape used by the destruction presentation actor. */
UENUM(BlueprintType)
enum class EChunkWorldChaosFractureFieldMode : uint8
{
	/** Uses a radial falloff strain field centered on the configured field origin. */
	RadialFalloff UMETA(DisplayName = "Radial Falloff"),

	/** Uses one uniform strain field so the collection releases more evenly and gravity drives the visible motion. */
	Uniform UMETA(DisplayName = "Uniform")
};

/**
 * Runtime diagnostic configuration used to observe one destruction presentation and suggest safer tuning values.
 */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldChaosDestructionDiagnosticsConfig
{
	GENERATED_BODY()

	/** If true, samples the destruction presentation after trigger and builds tuning suggestions from the observed motion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction|Diagnostics", meta = (ToolTip = "If true, samples the destruction presentation after trigger and builds tuning suggestions from the observed motion."))
	bool bEnableRuntimeDiagnostics = false;

	/** If true, prints the generated tuning suggestions to the log after the evaluation window completes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction|Diagnostics", meta = (ToolTip = "If true, prints the generated tuning suggestions to the log after the evaluation window completes."))
	bool bLogSuggestions = false;

	/** How often the actor samples its presentation state while diagnostics are active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction|Diagnostics", meta = (ClampMin = "0.01", UIMin = "0.01", ToolTip = "How often the actor samples its presentation state while runtime diagnostics are active."))
	float SampleIntervalSeconds = 0.05f;

	/** Total time window used to observe the destruction before suggestions are generated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction|Diagnostics", meta = (ClampMin = "0.05", UIMin = "0.05", ToolTip = "Total time window used to observe the destruction before tuning suggestions are generated."))
	float EvaluationWindowSeconds = 0.6f;

	/** Maximum horizontal drift still considered a gentle slump before the actor recommends reducing launch energy or recentering the field origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction|Diagnostics", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Maximum horizontal drift still considered a gentle slump before the actor recommends reducing launch energy or recentering the field origin."))
	float TargetMaxHorizontalDrift = 55.0f;

	/** Maximum upward displacement still considered a heavy slump before the actor recommends lowering launch energy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction|Diagnostics", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Maximum upward displacement still considered a heavy slump before the actor recommends lowering launch energy."))
	float TargetMaxUpwardDisplacement = 20.0f;

	/** Maximum component speed still considered gentle before the actor recommends reducing the separation impulse or strain radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction|Diagnostics", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Maximum component speed still considered gentle before the actor recommends reducing the separation impulse or strain radius."))
	float TargetMaxComponentSpeed = 250.0f;

	/** Minimum increase in bounds radius that counts as a visible breakup rather than an unbroken collection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction|Diagnostics", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Minimum increase in bounds radius that counts as a visible breakup rather than an unbroken collection."))
	float MinimumExpectedSpreadIncrease = 8.0f;

	/** Maximum normalized field-origin offset still considered centered well enough before the actor recommends recentering the field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction|Diagnostics", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Maximum normalized field-origin offset still considered centered well enough before the actor recommends recentering the field."))
	float TargetMaxNormalizedFieldOriginOffset = 0.2f;
};

/**
 * Runtime summary captured from one Chaos destruction presentation.
 */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldChaosDestructionDiagnosticsMetrics
{
	GENERATED_BODY()

	/** Bounds origin when diagnostics started sampling. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	FVector InitialBoundsOrigin = FVector::ZeroVector;

	/** Bounds sphere radius when diagnostics started sampling. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	float InitialBoundsSphereRadius = 0.0f;

	/** Largest observed bounds sphere radius while diagnostics were active. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	float PeakBoundsSphereRadius = 0.0f;

	/** Largest observed horizontal drift of the collection bounds origin from the initial sampled origin. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	float MaxHorizontalDrift = 0.0f;

	/** Largest observed positive upward displacement of the collection bounds origin from the initial sampled origin. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	float MaxUpwardDisplacement = 0.0f;

	/** Largest observed component speed reported while diagnostics were active. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	float MaxComponentSpeed = 0.0f;

	/** Largest observed positive upward velocity reported while diagnostics were active. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	float MaxUpwardVelocity = 0.0f;

	/** Field origin resolved for the current destruction after centering and local offset logic was applied. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	FVector ResolvedFieldOrigin = FVector::ZeroVector;

	/** Geometry collection bounds origin captured when the trigger began. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	FVector TriggerBoundsOrigin = FVector::ZeroVector;

	/** External strain radius resolved for the current destruction. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	float ResolvedExternalStrainRadius = 0.0f;

	/** Separation impulse radius resolved for the current destruction. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	float ResolvedSeparationImpulseRadius = 0.0f;

	/** Distance from the resolved field origin to the current bounds center when the trigger began. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	float FieldOriginOffsetFromBoundsCenter = 0.0f;

	/** Field-origin distance normalized by the resolved strain radius. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	float NormalizedFieldOriginOffset = 0.0f;

	/** Duration actually sampled before the final recommendation pass ran. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	float SampledDurationSeconds = 0.0f;

	/** True once the observed bounds spread exceeded the configured minimum expected breakup amount. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	bool bObservedVisibleBreakup = false;
};

/**
 * Tunable Chaos destruction settings for one authored block-destruction presentation actor.
 */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldChaosDestructionPresentationTuning
{
	GENERATED_BODY()

	/** Geometry collection asset used for the destruction presentation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ToolTip = "Geometry collection asset used by this destruction presentation actor."))
	TObjectPtr<UGeometryCollection> GeometryCollectionAsset = nullptr;

	/** Local offset applied to the geometry collection component under the shared scene root. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ToolTip = "Local offset applied to the geometry collection component under the shared scene root."))
	FVector GeometryCollectionRelativeOffset = FVector::ZeroVector;

	/** Local-space offset used to position the external strain field relative to the destruction actor transform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ToolTip = "Local-space offset applied to the destruction actor transform when choosing the external strain field origin."))
	FVector StrainFieldLocalOffset = FVector::ZeroVector;

	/** If true, centers the field origin on the current geometry collection bounds before applying any local offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ToolTip = "If true, centers the field origin on the current geometry collection bounds before applying any local offset. This usually reduces sideways launch variance for oddly pivoted collections."))
	bool bCenterFieldOriginOnCollectionBounds = true;

	/** Delay before the external strain field is applied after the actor receives its destruction request. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Delay before the external strain field is applied after the actor receives its destruction request."))
	float TriggerDelaySeconds = 0.0f;

	/** If true, collision stays disabled until after the initial fracture has already started so overlap kickout does not dominate the motion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ToolTip = "If true, world collision responses are temporarily ignored until after the initial fracture has already started so overlap kickout does not dominate the motion."))
	bool bDelayCollisionUntilAfterFracture = true;

	/** Delay before world collision responses are restored when staged collision is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Delay before world collision responses are restored when staged collision is active. Increase this if the collection still appears to launch from overlap resolution at spawn."))
	float CollisionEnableDelaySeconds = 0.15f;

	/** If true, temporarily anchors the bottom portion of the collection so fracture can begin with support before the base is released. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ToolTip = "If true, temporarily anchors the bottom portion of the collection so fracture can begin with support before the base is released."))
	bool bUseTemporaryBottomAnchor = true;

	/** Fraction of the collection height anchored from the bottom when temporary support is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", ToolTip = "Fraction of the collection height anchored from the bottom when temporary support is enabled. Small values usually work best for heavy rocks."))
	float BottomAnchorHeightFraction = 0.15f;

	/** If true, releases the temporary bottom anchor after the configured delay. Disable this when the lower support should remain pinned for the whole presentation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ToolTip = "If true, releases the temporary bottom anchor after the configured delay. Disable this for tree-chop style presentations where the lower stump should remain pinned for the full lifetime of the actor."))
	bool bReleaseTemporaryBottomAnchor = true;

	/** Delay before the temporary bottom anchor is released. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Delay before the temporary bottom anchor is released. Increase this when you want the rock to start cracking before the base fully gives way."))
	float BottomAnchorReleaseDelaySeconds = 0.12f;

	/** Highest cluster level affected by the temporary bottom anchor. Use -1 to allow the engine default behavior. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ClampMin = "-1", UIMin = "-1", ToolTip = "Highest cluster level affected by the temporary bottom anchor. Use -1 to allow the engine default behavior."))
	int32 BottomAnchorMaxLevel = -1;

	/** Base external strain magnitude applied to the geometry collection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Base external strain magnitude applied to the geometry collection. Compare this against the Geometry Collection Damage Threshold array and keep it above the first cluster level you expect to break, with enough headroom to account for radial falloff across the collection."))
	float ExternalStrainMagnitude = 500000.0f;

	/** Fracture field mode used when applying the initial external strain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ToolTip = "Fracture field mode used when applying the initial external strain. Uniform is recommended for heavy break-and-slump behavior because it avoids the blast-like bias of a localized radial field."))
	EChunkWorldChaosFractureFieldMode FractureFieldMode = EChunkWorldChaosFractureFieldMode::RadialFalloff;

	/** If true, derives the strain radius from the current collection bounds so differently sized actors can reuse this class. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ToolTip = "If true, derives the strain radius from the current geometry collection bounds so differently sized destruction actors can share this class."))
	bool bScaleStrainRadiusFromCollectionBounds = true;

	/** Multiplier applied to the current geometry collection sphere radius when the strain radius is bounds-driven. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Multiplier applied to the current geometry collection sphere radius when the strain radius is derived from the collection bounds."))
	float StrainRadiusBoundsMultiplier = 0.55f;

	/** Fixed strain radius used when bounds-driven sizing is disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fixed strain radius used when bounds-driven sizing is disabled."))
	float FixedStrainRadius = 45.0f;

	/** Falloff curve used by the radial external strain field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ToolTip = "Falloff curve used by the radial external strain field. Linear falloff usually gives the most controllable gentle break behavior."))
	TEnumAsByte<EFieldFalloffType> StrainFalloff = EFieldFalloffType::Field_Falloff_Linear;

	/** If true, applies a small post-break radial impulse so fragments separate slightly before gravity takes over. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ToolTip = "If true, applies a small post-break radial impulse so fragments separate slightly before gravity becomes the dominant motion."))
	bool bApplyGentleSeparationImpulse = true;

	/** If true, runtime diagnostics temporarily skip the separation impulse so strain and field placement can be evaluated in isolation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction|Diagnostics", meta = (ToolTip = "If true, runtime diagnostics temporarily skip the separation impulse so strain and field placement can be evaluated in isolation."))
	bool bDisableSeparationImpulseDuringDiagnostics = false;

	/** Delay between the external strain field and the optional gentle separation impulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Delay between the external strain field and the optional gentle separation impulse. A short delay helps the collection register the break before the separation nudge is applied."))
	float SeparationImpulseDelaySeconds = 0.08f;

	/** Radial impulse magnitude used to gently separate newly broken chunks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Radial impulse magnitude used to gently separate newly broken chunks. Keep this low so the result reads as a slump instead of an explosion."))
	float SeparationImpulseStrength = 8.0f;

	/** Radius used by the optional post-break separation impulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Radius used by the optional post-break separation impulse. Keep this near the collection bounds so only the presentation chunks are nudged."))
	float SeparationImpulseRadius = 32.0f;

	/** Radial falloff policy used by the optional post-break separation impulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ToolTip = "Radial falloff policy used by the optional post-break separation impulse. Linear falloff usually produces the softest separation."))
	TEnumAsByte<ERadialImpulseFalloff> SeparationImpulseFalloff = ERadialImpulseFalloff::RIF_Linear;

	/** Additional lifetime after triggering destruction before this transient presentation actor destroys itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Additional lifetime after destruction begins before this transient presentation actor destroys itself."))
	float LifeSpanAfterTriggerSeconds = 6.0f;
};

/**
 * Reusable Chaos-driven destruction presentation actor that reacts to chunk-world block removals through the shared destruction interface.
 */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API AChunkWorldChaosDestructionPresentationActor
	: public AActor
	, public IChunkWorldDestructionActorInterface
{
	GENERATED_BODY()

public:
	/** Creates a reusable destruction presentation actor with a scene root, geometry collection, and field system host. */
	AChunkWorldChaosDestructionPresentationActor();

	/** Starts one destruction presentation after the authoritative chunk-world block removal request is received. */
	virtual void TriggerBlockDestruction_Implementation(const FChunkWorldBlockDestructionRequest& Request) override;

	/** Returns the geometry collection component used for the destruction presentation. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Destruction")
	UGeometryCollectionComponent* GetGeometryCollectionComponent() const { return GeometryCollectionComponent; }

	/** Returns the field system component used to dispatch transient Chaos fields for the presentation. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Destruction")
	UFieldSystemComponent* GetFieldSystemComponent() const { return FieldSystemComponent; }

	/** Returns the currently active destruction tuning values for this actor. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Destruction")
	const FChunkWorldChaosDestructionPresentationTuning& GetDestructionTuning() const { return DestructionTuning; }

	/** Returns the currently active diagnostics configuration for this actor. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	const FChunkWorldChaosDestructionDiagnosticsConfig& GetDiagnosticsConfig() const { return DiagnosticsConfig; }

	/** Returns the most recent destruction diagnostics metrics captured by this actor. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	const FChunkWorldChaosDestructionDiagnosticsMetrics& GetLastDiagnosticsMetrics() const { return LastDiagnosticsMetrics; }

	/** Returns the most recent generated diagnostics suggestion text. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Destruction|Diagnostics")
	const FString& GetLastDiagnosticsSuggestionSummary() const { return LastDiagnosticsSuggestionSummary; }

protected:
	/** Synchronizes the geometry collection component with the current tunable asset and local offset values. */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Applies the actual destruction behavior once any configured startup delay has elapsed. */
	virtual void ExecuteDestructionPresentation();

	/** Returns the world-space field origin used for external strain and the optional separation impulse. */
	virtual FVector ResolveFieldOrigin(const FChunkWorldBlockDestructionRequest& Request) const;

	/** Resolves the radial strain radius from either bounds-driven or fixed-size tuning. */
	virtual float ResolveExternalStrainRadius() const;

	/** Resolves the radial separation impulse radius from the current tuning. */
	virtual float ResolveSeparationImpulseRadius() const;

	/** Applies the configured radial external strain field to the geometry collection. */
	virtual void ApplyExternalStrainField(const FVector& FieldOrigin);

	/** Applies the configured gentle post-break radial impulse when enabled. */
	virtual void ApplyGentleSeparationImpulse(const FVector& FieldOrigin);

	/** Applies the staged world-collision response profile before fracture begins. */
	virtual void ApplyPreFractureCollisionStaging();

	/** Restores the normal world-collision response profile after any configured staging delay has elapsed. */
	virtual void RestorePostFractureCollisionResponses();

	/** Applies a temporary bottom anchor so the collection can begin fracturing with short-lived support. */
	virtual void ApplyTemporaryBottomAnchor();

	/** Releases the temporary bottom anchor after the configured delay. */
	virtual void ReleaseTemporaryBottomAnchor();

	/** Allows subclasses to react immediately after the request is accepted and the actor transform is updated. */
	virtual void HandleDestructionTriggered(const FChunkWorldBlockDestructionRequest& Request);

	/** Starts one short runtime diagnostics pass when enabled so the actor can recommend safer tuning values. */
	virtual void StartRuntimeDiagnostics();

	/** Captures one diagnostics sample while the actor observes its current destruction motion. */
	virtual void CaptureRuntimeDiagnosticsSample();

	/** Finalizes the active diagnostics pass and updates the exposed tuning recommendations. */
	virtual void FinalizeRuntimeDiagnostics();

	/** Builds human-readable tuning suggestions from the currently captured runtime metrics. */
	virtual void BuildDiagnosticsSuggestions(TArray<FString>& OutSuggestions) const;

	/** Returns true when the current trigger should suppress the separation impulse for diagnostics. */
	virtual bool ShouldSkipSeparationImpulseForCurrentTrigger() const;

	/** Shared tunable settings exposed to Blueprint subclasses and placed instances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ShowOnlyInnerProperties, ToolTip = "Tunable Chaos destruction settings for this presentation actor."))
	FChunkWorldChaosDestructionPresentationTuning DestructionTuning;

	/** Runtime diagnostics configuration used to observe one break and suggest better tuning values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction|Diagnostics", meta = (ShowOnlyInnerProperties, ToolTip = "Runtime diagnostics configuration used to observe one break and suggest better tuning values."))
	FChunkWorldChaosDestructionDiagnosticsConfig DiagnosticsConfig;

	/** Shared scene root that allows subclasses and placed instances to offset the geometry collection locally. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction", meta = (AllowPrivateAccess = "true", ToolTip = "Scene root used so the geometry collection can be offset locally without moving the actor root."))
	TObjectPtr<USceneComponent> DestructionRoot = nullptr;

	/** Geometry collection component used to present the transient destruction pieces. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction", meta = (AllowPrivateAccess = "true", ToolTip = "Geometry collection component used to present the transient destruction pieces."))
	TObjectPtr<UGeometryCollectionComponent> GeometryCollectionComponent = nullptr;

	/** Field system component used to dispatch transient Chaos fields for this presentation actor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction", meta = (AllowPrivateAccess = "true", ToolTip = "Field system component used to dispatch transient Chaos fields for this presentation actor."))
	TObjectPtr<UFieldSystemComponent> FieldSystemComponent = nullptr;

	/** Last destruction request accepted by this transient presentation actor. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction", meta = (ToolTip = "Most recent chunk-world destruction request accepted by this presentation actor."))
	FChunkWorldBlockDestructionRequest LastDestructionRequest;

	/** Most recent diagnostics metrics captured from the active or last completed destruction. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics", meta = (ToolTip = "Most recent diagnostics metrics captured from the active or last completed destruction."))
	FChunkWorldChaosDestructionDiagnosticsMetrics LastDiagnosticsMetrics;

	/** Most recent generated diagnostics suggestion summary for this actor. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction|Diagnostics", meta = (ToolTip = "Most recent generated diagnostics suggestion summary for this actor."))
	FString LastDiagnosticsSuggestionSummary;

private:
	/** Updates the geometry collection component from the current asset and local offset tuning. */
	void RefreshGeometryCollectionPresentation();

	/** Executes the delayed destruction callback after the actor has already stored its request. */
	UFUNCTION()
	void HandleDelayedDestructionExecution();

	/** Executes the delayed separation-impulse callback after the external strain field has already fired. */
	UFUNCTION()
	void HandleDelayedSeparationImpulse();

	/** Executes the delayed collision-response restore callback after the initial fracture window. */
	UFUNCTION()
	void HandleDelayedCollisionEnable();

	/** Executes the delayed temporary-anchor release callback. */
	UFUNCTION()
	void HandleDelayedBottomAnchorRelease();

	/** Samples the current presentation state for runtime diagnostics. */
	UFUNCTION()
	void HandleRuntimeDiagnosticsSample();

	/** Finishes the active runtime diagnostics pass and emits the final suggestion text. */
	UFUNCTION()
	void HandleFinalizeRuntimeDiagnostics();

	/** True once the actor has already accepted one trigger request. */
	bool bHasTriggeredDestruction = false;

	/** True while one runtime diagnostics pass is currently active. */
	bool bRuntimeDiagnosticsActive = false;

	/** True when the current diagnostics pass intentionally suppressed the separation impulse. */
	bool bSkippedSeparationImpulseForDiagnostics = false;

	/** World time when the current diagnostics pass started sampling. */
	float RuntimeDiagnosticsStartTimeSeconds = 0.0f;

	/** Timer used to defer actual destruction field execution after the request is accepted. */
	FTimerHandle DestructionExecutionTimerHandle;

	/** Timer used to delay the optional gentle separation impulse until after the break begins. */
	FTimerHandle SeparationImpulseTimerHandle;

	/** Timer used to enable geometry collection collision after the initial fracture window. */
	FTimerHandle CollisionEnableTimerHandle;

	/** Timer used to release the temporary bottom anchor after the initial fracture window. */
	FTimerHandle BottomAnchorReleaseTimerHandle;

	/** Timer used to capture repeated runtime diagnostics samples while the current destruction is active. */
	FTimerHandle RuntimeDiagnosticsSampleTimerHandle;

	/** Timer used to finalize the current diagnostics pass after the configured evaluation window. */
	FTimerHandle RuntimeDiagnosticsFinalizeTimerHandle;
};
