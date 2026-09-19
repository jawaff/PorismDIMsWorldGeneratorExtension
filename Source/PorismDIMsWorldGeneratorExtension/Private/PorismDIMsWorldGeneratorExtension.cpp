// Copyright 2026 Spotted Loaf Studio

#include "PorismDIMsWorldGeneratorExtension.h"

#include "Layout/Diagnostics/LayoutEditorMessageLog.h"
#include "PorismDIMsWorldGenerator.h"

#if WITH_EDITOR
#include "MessageLogModule.h"
#endif

DEFINE_LOG_CATEGORY(LogPorismDIMsWorldGeneratorExtension);

void FPorismDIMsWorldGeneratorExtensionModule::StartupModule()
{
	FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorModule>("PorismDIMsWorldGenerator");
#if WITH_EDITOR
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	FMessageLogInitializationOptions InitOptions;
	InitOptions.bShowPages = true;
	MessageLogModule.RegisterLogListing(
		PorismLayoutEditorMessageLog::GetListingName(),
		NSLOCTEXT("PorismDIMsWorldGeneratorExtension", "PorismLayoutMessageLog", "Porism Layout"),
		InitOptions);
#endif
	UE_LOG(LogPorismDIMsWorldGeneratorExtension, Log, TEXT("PorismDIMsWorldGeneratorExtension started."));
}

void FPorismDIMsWorldGeneratorExtensionModule::ShutdownModule()
{
#if WITH_EDITOR
	if (FModuleManager::Get().IsModuleLoaded("MessageLog"))
	{
		FMessageLogModule& MessageLogModule = FModuleManager::GetModuleChecked<FMessageLogModule>("MessageLog");
		MessageLogModule.UnregisterLogListing(PorismLayoutEditorMessageLog::GetListingName());
	}
#endif
	UE_LOG(LogPorismDIMsWorldGeneratorExtension, Log, TEXT("PorismDIMsWorldGeneratorExtension shutting down."));
}

IMPLEMENT_MODULE(FPorismDIMsWorldGeneratorExtensionModule, PorismDIMsWorldGeneratorExtension)
