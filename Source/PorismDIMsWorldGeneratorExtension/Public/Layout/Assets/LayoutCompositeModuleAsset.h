// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Layout/Types/LayoutTypes.h"
#include "Misc/DataValidation.h"

#include "LayoutCompositeModuleAsset.generated.h"

class ULayoutModuleAsset;

/** One referenced leaf module placed at a fixed local cell and relative yaw inside a composite module. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutCompositeModuleCell
{
	GENERATED_BODY()

	/** Referenced single-cell leaf module reused by this composite occupied cell. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Referenced single-cell leaf module reused by this composite occupied cell."))
	TObjectPtr<ULayoutModuleAsset> Module = nullptr;

	/** Occupied local cell where this leaf module is glued inside the composite. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Occupied local cell where this leaf module is glued inside the composite."))
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Fixed relative yaw rotation step applied to the referenced leaf module inside the composite. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0", ClampMax = "3", UIMin = "0", UIMax = "3", ToolTip = "Fixed relative yaw rotation step applied to the referenced leaf module inside the composite. 0=authored, 1=90 degrees, 2=180 degrees, 3=270 degrees."))
	int32 RelativeYawRotationSteps = 0;
};

/**
 * Authoring asset that glues existing single-cell modules into one deterministic multi-cell composite.
 * This keeps leaf modules reusable while the solver consumes one compiled composite placement bundle later.
 */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API ULayoutCompositeModuleAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Returns the shared fixed cell size used by the referenced leaf modules, or zero when no valid cells exist. */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FIntVector GetSharedCellSizeInBlocks() const;

	/** Returns occupied local cells referenced by this composite asset. */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	TArray<FIntVector> GetOccupiedLocalCells() const;

	/** Returns the exclusive local bounds required to contain every occupied composite cell. */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FIntVector GetBoundsCells() const;

	/** Returns true when any referenced leaf module supports the supplied planned-cell intent. */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	bool SupportsIntent(ELayoutCellIntent Intent) const;

	/** Returns the union of traversal channels exposed by the referenced leaf modules. */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FGameplayTagContainer GetEffectiveTraversalChannels() const;

	/** Validates the authored composite contract without requiring snapshot compilation or solving. */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FLayoutValidationResult ValidateCompositeModule() const;

	/**
	 * Validates the authored composite contract against an already-established shared cell-size contract.
	 * Use this from content/request validation once a broader planning surface has already chosen the shared metrics.
	 */
	FLayoutValidationResult ValidateCompositeModuleAgainstSharedCellSize(const FIntVector& SharedCellSizeInBlocks) const;

	/** Referenced leaf modules and their fixed local arrangement inside this composite. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Referenced leaf modules and their fixed local arrangement inside this composite. Each occupied local cell should appear once with one referenced single-cell module and one fixed relative yaw."))
	TArray<FLayoutCompositeModuleCell> Cells;

#if WITH_EDITOR
public:
	/** Runs composite-module validation immediately and publishes the result to the editor Message Log. */
	UFUNCTION(CallInEditor, Category = "Layout|Validation", meta = (DisplayName = "Validate Layout Composite Module", ToolTip = "Runs this composite module's validation immediately and reports warnings or errors to the Porism Layout Message Log."))
	void ValidateLayoutCompositeModuleInEditor() const;

	/** Reports editor validation failures early so invalid composite wiring is visible before runtime. */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
