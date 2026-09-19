// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Layout/Types/LayoutTypes.h"
#include "Misc/DataValidation.h"

#include "LayoutModuleAsset.generated.h"

class UChunkStructureTemplate;

/**
 * Project-owned metadata wrapper for one Porism template that participates in
 * the fixed-cell layout module system.
 */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API ULayoutModuleAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** UObject hook that corrects serialized face-rule directions after load. */
	virtual void PostLoad() override;

	/** Returns the authored face rule for the supplied direction, or nullptr when it is missing. */
	const FLayoutFaceRule* FindFaceRule(ELayoutFaceDirection Direction) const;

	/** Returns the expanded face rule after applying authored face symmetry. */
	bool GetEffectiveFaceRule(ELayoutFaceDirection Direction, FLayoutFaceRule& OutFaceRule) const;

	/** Returns the six face rules after applying authored face symmetry. */
	FLayoutModuleFaceRules GetEffectiveFaceRules() const;

	/** Returns the effective normalized yaw rotation steps the current solver path may use for this module. */
	TArray<int32> GetEffectiveYawRotationSteps() const;

	/** Forces each named face-rule slot to carry the matching direction. */
	void NormalizeFaceRuleDirections();

	/** Validates the authored module metadata without requiring a layout solve. */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FLayoutValidationResult ValidateModule() const;

#if WITH_EDITOR
	/** Runs module validation immediately and publishes the result to the editor Message Log. */
	UFUNCTION(CallInEditor, Category = "Layout|Validation", meta = (DisplayName = "Validate Layout Module", ToolTip = "Runs this module's validation immediately and reports warnings or errors to the Porism Layout Message Log."))
	void ValidateLayoutModuleInEditor() const;
#endif

	/** Returns true when this module explicitly supports the supplied broad planner intent. */
	bool SupportsIntent(ELayoutCellIntent Intent) const;

	/** Returns fixed solver roles authored for this module. */
	TArray<ELayoutModuleRole> GetEffectiveRoles() const;

	/** Returns planned cell intents this module can satisfy from its authored roles. */
	TArray<ELayoutCellIntent> GetEffectiveSupportedCellIntents() const;

	/** Returns true when the effective role list contains the supplied role. */
	bool HasEffectiveRole(ELayoutModuleRole Role) const;

	/** Returns true when this module exposes the supplied traversal channel. */
	bool ExposesTraversalChannel(const FGameplayTag& TraversalChannel) const;

	/** Returns traversal channels exposed somewhere inside this module. */
	FGameplayTagContainer GetEffectiveTraversalChannels() const;

	/** Returns internal traversal links exposed by this module. */
	TArray<FLayoutInternalAccessLink> GetEffectiveInternalAccessLinks() const;

	/** Returns the effective base cell size the current asset/snapshot path uses for this module. */
	FIntVector GetEffectiveCellSizeInBlocks() const;

	/** Returns the effective template dimensions used for validation and snapshot compilation. */
	FIntVector GetEffectiveTemplateDimensionsBlocks() const;

	/** Returns the occupied local cells implied by the active one-cell leaf contract. */
	TArray<FIntVector> GetOccupiedLocalCells() const;

	/** Returns the occupied local-cell bounds implied by the effective occupied-cell contract. */
	FIntVector GetOccupiedBoundsCells() const;

	/** Template payload stamped when the solver chooses this module for one cell. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (AllowedClasses = "/Script/PorismDIMsWorldGenerator.ChunkStructureTemplate", ToolTip = "Porism structure template stamped when the solver places this module. The template size must match the effective template dimensions for this module contract."))
	TSoftObjectPtr<UChunkStructureTemplate> Template;

	/** Fixed solver roles this module can satisfy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Fixed solver roles this module can satisfy. Use Entry for doors and gates, Boundary for perimeter shell pieces, Interior for inside fill, or Vertical Access for stairs, ramps, and ladders."))
	TArray<ELayoutModuleRole> Roles;

	/** Face-rule symmetry used to infer non-authoritative horizontal faces. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Symmetry", meta = (ToolTip = "Controls whether some horizontal face rules are inferred from canonical faces. The solver always consumes fully expanded face rules after this symmetry is applied."))
	ELayoutFaceSymmetryMode FaceSymmetryMode = ELayoutFaceSymmetryMode::Independent;

	/** Named face rules for all six cell faces. Directions are derived from slot names. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Rules for all six authored cell faces. Each face defines what it is, what neighbor tags it can accept, whether the neighbor should be filled or empty, and which traversal channels connect through that side."))
	FLayoutModuleFaceRules FaceRules;

	/** Internal authored access links between traversal channels in this module. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Traversal links between traversal channels inside this module. Use these for stairs, ramps, ladders, or doorway-like connections that let strict reachability move between different traversal tags within one cell."))
	TArray<FLayoutInternalAccessLink> InternalAccessLinks;

	/** Minimum number of traversal-channel faces that must connect to compatible filled neighbors. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0", DisplayName = "Min Traversable Neighbor Faces", ToolTip = "Minimum number of this module's faces with Connected Traversal Channels that must connect to compatible filled neighbors sharing at least one traversal channel. The solver caps this value to the number of faces that expose traversal and can accept a filled neighbor, so modules with fewer eligible faces do not become impossible. Leave at 0 when traversal-channel face connections are only advisory."))
	int32 MinWalkableFaces = 0;

#if WITH_EDITOR
public:
	/** Editor hook that keeps named face slots normalized after property edits. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Reports editor validation failures early so invalid authored layout metadata is visible before runtime. */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
