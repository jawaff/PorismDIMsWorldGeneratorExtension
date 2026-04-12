// Copyright 2026 Spotted Loaf Studio

#include "Actor/ChunkWorldStandalonePlayerCharacterBase.h"

#include "Actor/Components/PorismStartupFreezeComponent.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Actors/Components/ChunkWorldProximityComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogChunkWorldStandalonePlayerCharacter, Log, All);

AChunkWorldStandalonePlayerCharacterBase::AChunkWorldStandalonePlayerCharacterBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);

	ChunkWorldProximityComponent = CreateDefaultSubobject<UChunkWorldProximityComponent>(TEXT("ChunkWorldProximityComponent"));
	StartupFreezeComponent = CreateDefaultSubobject<UPorismStartupFreezeComponent>(TEXT("StartupFreezeComponent"));
}

FVector AChunkWorldStandalonePlayerCharacterBase::GetTracingLocation_Implementation() const
{
	return GetActorLocation() + WalkerTraceLocationOffset;
}

FVector AChunkWorldStandalonePlayerCharacterBase::GetTracingVector_Implementation() const
{
	return GetActorForwardVector().GetSafeNormal();
}

TArray<double> AChunkWorldStandalonePlayerCharacterBase::GetViewDistanceMultiplier_Implementation() const
{
	return ViewDistanceMultiplier;
}

void AChunkWorldStandalonePlayerCharacterBase::WalkerPositionInfo_Implementation(FChunkWorldWalkerInfo ChunkWorldWalkerInfo)
{
	LastWalkerPositionInfo = ChunkWorldWalkerInfo;
}

void AChunkWorldStandalonePlayerCharacterBase::BeginPlay()
{
	Super::BeginPlay();

	ChunkWorldSetupRetryAttempts = 0;
	UE_LOG(
		LogChunkWorldStandalonePlayerCharacter,
		Log,
		TEXT("BeginPlay Character=%s NetMode=%s World=%s"),
		*GetNameSafe(this),
		GetNetModeName(GetWorld()),
		*GetNameSafe(GetWorld()));

	InitializeChunkWorldPlayerSetup();
}

void AChunkWorldStandalonePlayerCharacterBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(
		LogChunkWorldStandalonePlayerCharacter,
		Log,
		TEXT("EndPlay Character=%s Reason=%d"),
		*GetNameSafe(this),
		static_cast<int32>(EndPlayReason));

	CancelChunkWorldSetupRetry();
	UnregisterFromChunkWorlds();
	BoundChunkWorlds.Reset();

	Super::EndPlay(EndPlayReason);
}

void AChunkWorldStandalonePlayerCharacterBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitializeChunkWorldPlayerSetup();
}

void AChunkWorldStandalonePlayerCharacterBase::OnRep_Controller()
{
	Super::OnRep_Controller();
	InitializeChunkWorldPlayerSetup();
}

void AChunkWorldStandalonePlayerCharacterBase::InitializeChunkWorldPlayerSetup()
{
	const bool bRegistered = RegisterWithChunkWorlds();
	const bool bBound = BindChunkWorldClients();
	const bool bNeedsAuthorityBinding = HasAuthority() && GetNetMode() != NM_Standalone;
	const bool bRequiresRetryForBinding = bNeedsAuthorityBinding && Cast<APlayerController>(GetController()) == nullptr;

	if (!bRegistered || !bBound || bRequiresRetryForBinding)
	{
		ScheduleChunkWorldSetupRetry();
		return;
	}

	CancelChunkWorldSetupRetry();
}

bool AChunkWorldStandalonePlayerCharacterBase::RegisterWithChunkWorlds()
{
	const TArray<AChunkWorldExtended*> ChunkWorlds = GatherChunkWorlds();
	UE_LOG(
		LogChunkWorldStandalonePlayerCharacter,
		Log,
		TEXT("RegisterWithChunkWorlds Character=%s Attempt=%d NetMode=%s FoundChunkWorlds=%d"),
		*GetNameSafe(this),
		ChunkWorldSetupRetryAttempts + 1,
		GetNetModeName(GetWorld()),
		ChunkWorlds.Num());

	if (ChunkWorlds.IsEmpty())
	{
		return false;
	}

	bool bRegisteredAnyChunkWorld = false;
	bool bAllChunkWorldsReadyForRegistration = true;
	for (AChunkWorldExtended* ChunkWorld : ChunkWorlds)
	{
		if (!IsValid(ChunkWorld))
		{
			continue;
		}

		if (!ChunkWorld->IsRunning())
		{
			bAllChunkWorldsReadyForRegistration = false;
			continue;
		}

		const bool bAlreadyRegistered = RegisteredChunkWorlds.ContainsByPredicate(
			[ChunkWorld](const TWeakObjectPtr<AChunkWorldExtended>& ExistingWorld)
			{
				return ExistingWorld.Get() == ChunkWorld;
			});
		if (!bAlreadyRegistered)
		{
			UE_LOG(
				LogChunkWorldStandalonePlayerCharacter,
				Log,
				TEXT("RegisterWithChunkWorlds Character=%s ChunkWorld=%s HasAuthority=%d"),
				*GetNameSafe(this),
				*GetNameSafe(ChunkWorld),
				ChunkWorld->HasAuthority() ? 1 : 0);
			ChunkWorld->AddChunkWorldWalker(this);
			RegisteredChunkWorlds.Add(ChunkWorld);
		}

		bRegisteredAnyChunkWorld = true;
	}

	RegisteredChunkWorlds.RemoveAll(
		[](const TWeakObjectPtr<AChunkWorldExtended>& ExistingWorld)
		{
			return !ExistingWorld.IsValid();
		});

	return bRegisteredAnyChunkWorld && bAllChunkWorldsReadyForRegistration;
}

void AChunkWorldStandalonePlayerCharacterBase::UnregisterFromChunkWorlds()
{
	UE_LOG(
		LogChunkWorldStandalonePlayerCharacter,
		Log,
		TEXT("UnregisterFromChunkWorlds Character=%s NetMode=%s RegisteredChunkWorlds=%d"),
		*GetNameSafe(this),
		GetNetModeName(GetWorld()),
		RegisteredChunkWorlds.Num());

	for (const TWeakObjectPtr<AChunkWorldExtended>& ChunkWorldPtr : RegisteredChunkWorlds)
	{
		if (AChunkWorldExtended* ChunkWorld = ChunkWorldPtr.Get())
		{
			if (ChunkWorld->IsRunning())
			{
				ChunkWorld->RemoveChunkWorldWalker(this);
			}
		}
	}

	RegisteredChunkWorlds.Reset();
}

bool AChunkWorldStandalonePlayerCharacterBase::BindChunkWorldClients()
{
	if (!HasAuthority() || GetNetMode() == NM_Standalone)
	{
		return true;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (PlayerController == nullptr)
	{
		return false;
	}

	const TArray<AChunkWorldExtended*> ChunkWorlds = GatherChunkWorlds();
	if (ChunkWorlds.IsEmpty())
	{
		return false;
	}

	bool bBoundAnyChunkWorld = false;
	bool bAllChunkWorldsBound = true;
	for (AChunkWorldExtended* ChunkWorld : ChunkWorlds)
	{
		if (!IsValid(ChunkWorld))
		{
			continue;
		}

		if (!ChunkWorld->IsRunning())
		{
			bAllChunkWorldsBound = false;
			continue;
		}

		const bool bAlreadyBound = BoundChunkWorlds.ContainsByPredicate(
			[ChunkWorld](const TWeakObjectPtr<AChunkWorldExtended>& ExistingWorld)
			{
				return ExistingWorld.Get() == ChunkWorld;
			});
		if (!bAlreadyBound)
		{
			if (ChunkWorld->BindNewClientGenerator(PlayerController) == nullptr)
			{
				bAllChunkWorldsBound = false;
				continue;
			}

			UE_LOG(
				LogChunkWorldStandalonePlayerCharacter,
				Log,
				TEXT("BindChunkWorldClients Character=%s ChunkWorld=%s Controller=%s"),
				*GetNameSafe(this),
				*GetNameSafe(ChunkWorld),
				*GetNameSafe(PlayerController));
			BoundChunkWorlds.Add(ChunkWorld);
		}

		bBoundAnyChunkWorld = true;
	}

	BoundChunkWorlds.RemoveAll(
		[](const TWeakObjectPtr<AChunkWorldExtended>& ExistingWorld)
		{
			return !ExistingWorld.IsValid();
		});

	return bBoundAnyChunkWorld && bAllChunkWorldsBound;
}

void AChunkWorldStandalonePlayerCharacterBase::ScheduleChunkWorldSetupRetry()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	if (ChunkWorldSetupRetryAttempts >= MaxChunkWorldSetupRetryAttempts)
	{
		UE_LOG(
			LogChunkWorldStandalonePlayerCharacter,
			Warning,
			TEXT("Chunk-world startup setup for Character=%s exhausted retries after %d attempts."),
			*GetNameSafe(this),
			ChunkWorldSetupRetryAttempts);
		return;
	}

	if (World->GetTimerManager().IsTimerActive(ChunkWorldSetupRetryTimerHandle))
	{
		return;
	}

	++ChunkWorldSetupRetryAttempts;
	UE_LOG(
		LogChunkWorldStandalonePlayerCharacter,
		Log,
		TEXT("Chunk-world startup setup for Character=%s scheduling retry %d/%d in %.2fs"),
		*GetNameSafe(this),
		ChunkWorldSetupRetryAttempts,
		MaxChunkWorldSetupRetryAttempts,
		ChunkWorldSetupRetryIntervalSeconds);

	World->GetTimerManager().SetTimer(
		ChunkWorldSetupRetryTimerHandle,
		this,
		&AChunkWorldStandalonePlayerCharacterBase::HandleChunkWorldSetupRetry,
		ChunkWorldSetupRetryIntervalSeconds,
		false);
}

void AChunkWorldStandalonePlayerCharacterBase::CancelChunkWorldSetupRetry()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ChunkWorldSetupRetryTimerHandle);
	}
}

void AChunkWorldStandalonePlayerCharacterBase::HandleChunkWorldSetupRetry()
{
	InitializeChunkWorldPlayerSetup();
}

TArray<AChunkWorldExtended*> AChunkWorldStandalonePlayerCharacterBase::GatherChunkWorlds() const
{
	TArray<AActor*> ChunkWorldActors;
	TArray<AChunkWorldExtended*> ChunkWorlds;

	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::GetAllActorsOfClass(World, AChunkWorldExtended::StaticClass(), ChunkWorldActors);
		ChunkWorlds.Reserve(ChunkWorldActors.Num());
		for (AActor* ChunkWorldActor : ChunkWorldActors)
		{
			if (AChunkWorldExtended* ChunkWorld = Cast<AChunkWorldExtended>(ChunkWorldActor))
			{
				ChunkWorlds.Add(ChunkWorld);
			}
		}
	}

	return ChunkWorlds;
}

const TCHAR* AChunkWorldStandalonePlayerCharacterBase::GetNetModeName(const UWorld* World)
{
	if (!World)
	{
		return TEXT("None");
	}

	switch (World->GetNetMode())
	{
	case NM_Standalone:
		return TEXT("Standalone");
	case NM_DedicatedServer:
		return TEXT("DedicatedServer");
	case NM_ListenServer:
		return TEXT("ListenServer");
	case NM_Client:
		return TEXT("Client");
	default:
		return TEXT("Unknown");
	}
}
