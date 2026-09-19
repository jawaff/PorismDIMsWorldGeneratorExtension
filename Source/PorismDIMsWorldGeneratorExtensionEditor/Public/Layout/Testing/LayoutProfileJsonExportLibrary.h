// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "LayoutProfileJsonExportLibrary.generated.h"

class UObject;
class ULayoutProfileAsset;
class ULayoutRegionContentSetAsset;

/** Editor helpers for exporting solver-focused layout JSON fixtures from authored assets. */
UCLASS()
class PORISMDIMSWORLDGENERATOREXTENSIONEDITOR_API ULayoutProfileJsonExportLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Writes a layout profile plus its unified content set to a JSON fixture file for automation repros. */
	UFUNCTION(BlueprintCallable, Category = "Porism Layout|Testing", meta = (ToolTip = "Exports the solver-relevant profile and unified content-set data to a JSON fixture file that automation tests can import as transient assets."))
	static bool ExportLayoutProfileJsonFixtureFromContentSet(
		ULayoutProfileAsset* Profile,
		ULayoutRegionContentSetAsset* ContentSet,
		const FString& FilePath,
		FString& OutError);
};
