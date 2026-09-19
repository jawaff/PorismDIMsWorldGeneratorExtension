// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Layout/Types/LayoutTypes.h"
#include "Misc/DataValidation.h"

#include "LayoutRegionContentSetAsset.generated.h"

class ULayoutModuleAsset;

/** Unified weighted content set that can reference either modules or child region profiles. */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API ULayoutRegionContentSetAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Returns the active shared cell size derivable from referenced module/composite content only, or zero when this set has no direct structural source. */
	FIntVector GetDerivedSharedCellSizeInBlocks() const;

	/** Returns the shared cell size derivable from referenced module/composite content, or zero when this set has no direct structural source. */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FIntVector GetSharedCellSizeInBlocks() const;

	/** Returns all module assets referenced by module entries that declare the supplied solver role. */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	TArray<ULayoutModuleAsset*> GetModulesForRole(ELayoutModuleRole Role) const;

	/** Validates the content-set contract and its referenced assets. */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FLayoutValidationResult ValidateContentSet() const;

	/** Weighted module-or-child content entries used by this region family. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Weighted module-or-child content entries used by this region family."))
	TArray<FLayoutRegionContentEntry> Entries;

#if WITH_EDITOR
public:
	/** Runs content-set validation immediately and publishes the result to the editor Message Log. */
	UFUNCTION(CallInEditor, Category = "Layout|Validation", meta = (DisplayName = "Validate Layout Region Content Set", ToolTip = "Runs this content set's validation immediately and reports warnings or errors to the Porism Layout Message Log."))
	void ValidateLayoutRegionContentSetInEditor() const;

	/** Reports editor validation failures early so invalid content-set wiring is visible before runtime. */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
