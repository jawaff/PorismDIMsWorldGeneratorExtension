// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChunkWorldSwapRegistrationSubsystem.generated.h"

class UChunkWorldBlockSwapScannerComponent;
class UChunkWorldProximityComponent;

/**
 * World-scoped registration service that connects shared proximity sources to shared chunk-world swap scanners.
 */
UCLASS()
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldSwapRegistrationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Registers one active swap scanner and binds any already-active proximity sources to it. */
	void RegisterSwapScanner(UChunkWorldBlockSwapScannerComponent* Scanner);

	/** Unregisters one swap scanner from the world-scoped registration service. */
	void UnregisterSwapScanner(UChunkWorldBlockSwapScannerComponent* Scanner);

	/** Registers one active chunk-world proximity source and binds it to already-active swap scanners. */
	void RegisterProximitySource(UChunkWorldProximityComponent* ProximitySource);

	/** Unregisters one proximity source from already-active swap scanners. */
	void UnregisterProximitySource(UChunkWorldProximityComponent* ProximitySource);

private:
	void CleanupInvalidReferences();

	TArray<TWeakObjectPtr<UChunkWorldBlockSwapScannerComponent>> ActiveScanners;
	TArray<TWeakObjectPtr<UChunkWorldProximityComponent>> ActiveProximitySources;
};
