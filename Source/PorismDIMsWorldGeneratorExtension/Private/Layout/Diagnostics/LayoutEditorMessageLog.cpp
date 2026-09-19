// Copyright 2026 Spotted Loaf Studio

#include "Layout/Diagnostics/LayoutEditorMessageLog.h"

#if WITH_EDITOR
#include "Logging/MessageLog.h"

namespace PorismLayoutEditorMessageLog
{
	FName GetListingName()
	{
		static const FName ListingName(TEXT("PorismLayout"));
		return ListingName;
	}

	FString BuildContextMessage(const UObject* const ContextObject, const FString& MessageText)
	{
		if (ContextObject == nullptr)
		{
			return MessageText;
		}

		return FString::Printf(
			TEXT("%s: %s"),
			*ContextObject->GetPathName(),
			*MessageText);
	}

	void ReportWarning(const UObject* const ContextObject, const FString& MessageText, const bool bOpenLog)
	{
		FMessageLog MessageLog(GetListingName());
		MessageLog.Warning(FText::FromString(BuildContextMessage(ContextObject, MessageText)));
		if (bOpenLog)
		{
			MessageLog.Open(EMessageSeverity::Warning);
		}
	}

	void ReportError(const UObject* const ContextObject, const FString& MessageText, const bool bOpenLog)
	{
		FMessageLog MessageLog(GetListingName());
		MessageLog.Error(FText::FromString(BuildContextMessage(ContextObject, MessageText)));
		if (bOpenLog)
		{
			MessageLog.Open(EMessageSeverity::Error);
		}
	}

	void ReportValidationResult(const UObject* const ContextObject, const FLayoutValidationResult& ValidationResult, const FString& SuccessMessage)
	{
		FMessageLog MessageLog(GetListingName());
		bool bHasErrors = false;
		bool bHasWarnings = false;

		for (const FLayoutValidationMessage& Message : ValidationResult.Messages)
		{
			const FString ContextMessage = BuildContextMessage(ContextObject, Message.Message);
			if (Message.Severity == ELayoutValidationSeverity::Error)
			{
				MessageLog.Error(FText::FromString(ContextMessage));
				bHasErrors = true;
			}
			else
			{
				MessageLog.Warning(FText::FromString(ContextMessage));
				bHasWarnings = true;
			}
		}

		if (!bHasErrors && !bHasWarnings)
		{
			MessageLog.Info(FText::FromString(BuildContextMessage(ContextObject, SuccessMessage)));
			MessageLog.Open(EMessageSeverity::Info);
			return;
		}

		MessageLog.Open(bHasErrors ? EMessageSeverity::Error : EMessageSeverity::Warning);
	}
}
#endif
