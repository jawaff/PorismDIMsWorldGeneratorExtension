// Copyright 2026 Spotted Loaf Studio

#include "PorismDIMsWorldGeneratorExtension.h"

#include "PorismDIMsWorldGenerator.h"

DEFINE_LOG_CATEGORY(LogPorismDIMsWorldGeneratorExtension);

void FPorismDIMsWorldGeneratorExtensionModule::StartupModule()
{
	FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorModule>("PorismDIMsWorldGenerator");
	UE_LOG(LogPorismDIMsWorldGeneratorExtension, Log, TEXT("PorismDIMsWorldGeneratorExtension started."));
}

void FPorismDIMsWorldGeneratorExtensionModule::ShutdownModule()
{
	UE_LOG(LogPorismDIMsWorldGeneratorExtension, Log, TEXT("PorismDIMsWorldGeneratorExtension shutting down."));
}

IMPLEMENT_MODULE(FPorismDIMsWorldGeneratorExtensionModule, PorismDIMsWorldGeneratorExtension)
