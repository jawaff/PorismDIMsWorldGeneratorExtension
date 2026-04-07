// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Subsystems/ChunkWorldSwapRegistrationSubsystem.h"

#include "ChunkWorld/Components/ChunkWorldBlockSwapScannerComponent.h"
#include "ChunkWorld/Components/ChunkWorldProximityComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogChunkWorldSwapRegistration, Log, All);

void UChunkWorldSwapRegistrationSubsystem::RegisterSwapScanner(UChunkWorldBlockSwapScannerComponent* Scanner)
{
	if (Scanner == nullptr)
	{
		return;
	}

	CleanupInvalidReferences();
	ActiveScanners.AddUnique(Scanner);

	UE_LOG(
		LogChunkWorldSwapRegistration,
		Log,
		TEXT("SwapRegistration RegisterScanner Scanner=%s Owner=%s ActiveScanners=%d ActiveSources=%d"),
		*GetNameSafe(Scanner),
		*GetNameSafe(Scanner->GetOwner()),
		ActiveScanners.Num(),
		ActiveProximitySources.Num());

	for (const TWeakObjectPtr<UChunkWorldProximityComponent>& ProximitySourcePtr : ActiveProximitySources)
	{
		if (UChunkWorldProximityComponent* ProximitySource = ProximitySourcePtr.Get())
		{
			Scanner->RegisterProximitySource(ProximitySource);
		}
	}
}

void UChunkWorldSwapRegistrationSubsystem::UnregisterSwapScanner(UChunkWorldBlockSwapScannerComponent* Scanner)
{
	if (Scanner == nullptr)
	{
		return;
	}

	CleanupInvalidReferences();
	ActiveScanners.RemoveSingleSwap(Scanner);

	UE_LOG(
		LogChunkWorldSwapRegistration,
		Log,
		TEXT("SwapRegistration UnregisterScanner Scanner=%s Owner=%s ActiveScanners=%d ActiveSources=%d"),
		*GetNameSafe(Scanner),
		*GetNameSafe(Scanner->GetOwner()),
		ActiveScanners.Num(),
		ActiveProximitySources.Num());
}

void UChunkWorldSwapRegistrationSubsystem::RegisterProximitySource(UChunkWorldProximityComponent* ProximitySource)
{
	if (ProximitySource == nullptr)
	{
		return;
	}

	CleanupInvalidReferences();
	ActiveProximitySources.AddUnique(ProximitySource);

	UE_LOG(
		LogChunkWorldSwapRegistration,
		Log,
		TEXT("SwapRegistration RegisterSource Source=%s Owner=%s ActiveScanners=%d ActiveSources=%d"),
		*GetNameSafe(ProximitySource),
		*GetNameSafe(ProximitySource->GetOwner()),
		ActiveScanners.Num(),
		ActiveProximitySources.Num());

	for (const TWeakObjectPtr<UChunkWorldBlockSwapScannerComponent>& ScannerPtr : ActiveScanners)
	{
		if (UChunkWorldBlockSwapScannerComponent* Scanner = ScannerPtr.Get())
		{
			Scanner->RegisterProximitySource(ProximitySource);
		}
	}
}

void UChunkWorldSwapRegistrationSubsystem::UnregisterProximitySource(UChunkWorldProximityComponent* ProximitySource)
{
	if (ProximitySource == nullptr)
	{
		return;
	}

	CleanupInvalidReferences();
	ActiveProximitySources.RemoveSingleSwap(ProximitySource);

	UE_LOG(
		LogChunkWorldSwapRegistration,
		Log,
		TEXT("SwapRegistration UnregisterSource Source=%s Owner=%s ActiveScanners=%d ActiveSources=%d"),
		*GetNameSafe(ProximitySource),
		*GetNameSafe(ProximitySource->GetOwner()),
		ActiveScanners.Num(),
		ActiveProximitySources.Num());

	for (const TWeakObjectPtr<UChunkWorldBlockSwapScannerComponent>& ScannerPtr : ActiveScanners)
	{
		if (UChunkWorldBlockSwapScannerComponent* Scanner = ScannerPtr.Get())
		{
			Scanner->UnregisterProximitySource(ProximitySource);
		}
	}
}

void UChunkWorldSwapRegistrationSubsystem::CleanupInvalidReferences()
{
	const int32 PreviousScannerCount = ActiveScanners.Num();
	const int32 PreviousSourceCount = ActiveProximitySources.Num();

	ActiveScanners.RemoveAllSwap([](const TWeakObjectPtr<UChunkWorldBlockSwapScannerComponent>& ScannerPtr)
	{
		return !ScannerPtr.IsValid();
	});

	ActiveProximitySources.RemoveAllSwap([](const TWeakObjectPtr<UChunkWorldProximityComponent>& ProximitySourcePtr)
	{
		return !ProximitySourcePtr.IsValid();
	});

	if (ActiveScanners.Num() != PreviousScannerCount || ActiveProximitySources.Num() != PreviousSourceCount)
	{
		UE_LOG(
			LogChunkWorldSwapRegistration,
			Log,
			TEXT("SwapRegistration CleanupInvalidReferences RemovedScanners=%d RemovedSources=%d RemainingScanners=%d RemainingSources=%d"),
			PreviousScannerCount - ActiveScanners.Num(),
			PreviousSourceCount - ActiveProximitySources.Num(),
			ActiveScanners.Num(),
			ActiveProximitySources.Num());
	}
}
