// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Types/LayoutTypes.h"

/**
 * Editor-facing diagnostics bridge for layout asset validation and runtime planning.
 */
namespace PorismLayoutEditorMessageLog
{
#if WITH_EDITOR
	/** Message-log listing used for layout asset validation and generation diagnostics. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FName GetListingName();

	/** Emits one warning to the layout Message Log and optionally opens the listing. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void ReportWarning(const UObject* ContextObject, const FString& MessageText, bool bOpenLog = false);

	/** Emits one error to the layout Message Log and optionally opens the listing. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void ReportError(const UObject* ContextObject, const FString& MessageText, bool bOpenLog = true);

	/** Emits every validation message from a result and opens the listing when warnings or errors exist. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void ReportValidationResult(const UObject* ContextObject, const FLayoutValidationResult& ValidationResult, const FString& SuccessMessage);
#endif
}
