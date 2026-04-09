// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/Components/ChunkWorldProximityComponent.h"

#include "ChunkWorld/Subsystems/ChunkWorldSwapRegistrationSubsystem.h"
#include "DrawDebugHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogChunkWorldProximity, Log, All);

UChunkWorldProximityComponent::UChunkWorldProximityComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UChunkWorldProximityComponent::BeginPlay()
{
	Super::BeginPlay();
	SetComponentTickEnabled(bDebugDrawProximitySphere);

	UE_LOG(
		LogChunkWorldProximity,
		Log,
		TEXT("ChunkWorldProximity BeginPlay Component=%s Owner=%s SwapInDistance=%.2f SwapOutDistance=%.2f Offset=%s CollisionChannel=%d DebugDraw=%d DebugActiveSwaps=%d"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		SwapInDistance,
		SwapOutDistance,
		*ScanOriginOffset.ToString(),
		static_cast<int32>(ProximityCollisionChannel.GetValue()),
		bDebugDrawProximitySphere ? 1 : 0,
		bDebugDrawActiveSwapBlocks ? 1 : 0);

	ensureMsgf(
		SwapInDistance < SwapOutDistance,
		TEXT("ChunkWorldProximity on %s requires SwapInDistance %.2f to be strictly less than SwapOutDistance %.2f."),
		*GetNameSafe(GetOwner()),
		SwapInDistance,
		SwapOutDistance);

	if (UWorld* World = GetWorld())
	{
		if (UChunkWorldSwapRegistrationSubsystem* SwapRegistrationSubsystem = World->GetSubsystem<UChunkWorldSwapRegistrationSubsystem>())
		{
			SwapRegistrationSubsystem->RegisterProximitySource(this);
		}
	}
}

void UChunkWorldProximityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(
		LogChunkWorldProximity,
		Log,
		TEXT("ChunkWorldProximity EndPlay Component=%s Owner=%s Reason=%d"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		static_cast<int32>(EndPlayReason));

	if (UWorld* World = GetWorld())
	{
		if (UChunkWorldSwapRegistrationSubsystem* SwapRegistrationSubsystem = World->GetSubsystem<UChunkWorldSwapRegistrationSubsystem>())
		{
			SwapRegistrationSubsystem->UnregisterProximitySource(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void UChunkWorldProximityComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bDebugDrawProximitySphere)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr || !World->IsGameWorld())
	{
		return;
	}

	const FVector ScanOrigin = GetScanOrigin();
	DrawDebugSphere(
		World,
		ScanOrigin,
		SwapOutDistance,
		FMath::Max(4, DebugProximitySphereSegments),
		DebugSwapOutSphereColor,
		false,
		0.0f,
		0,
		FMath::Max(0.0f, DebugProximitySphereThickness));
	DrawDebugSphere(
		World,
		ScanOrigin,
		SwapInDistance,
		FMath::Max(4, DebugProximitySphereSegments),
		DebugSwapInSphereColor,
		false,
		0.0f,
		0,
		FMath::Max(0.0f, DebugProximitySphereThickness));
}

FVector UChunkWorldProximityComponent::GetScanOrigin() const
{
	return GetOwner() != nullptr ? GetOwner()->GetActorLocation() + ScanOriginOffset : ScanOriginOffset;
}
