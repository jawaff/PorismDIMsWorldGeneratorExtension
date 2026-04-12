// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldPlayerCharacters.h"

#include "Actor/Components/PorismHealthInteractionComponent.h"
#include "Actor/Components/PorismPredictedBlockStateComponent.h"
#include "Actor/Components/PorismTraceInteractionComponent.h"
#include "ChunkWorld/Actors/Components/ChunkWorldProximityComponent.h"
#include "Components/CapsuleComponent.h"

AChunkWorldPlayerCharacter::AChunkWorldPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);

	TraceInteractionComponent = CreateDefaultSubobject<UPorismTraceInteractionComponent>(TEXT("TraceInteractionComponent"));
	ChunkWorldProximityComponent = CreateDefaultSubobject<UChunkWorldProximityComponent>(TEXT("ChunkWorldProximityComponent"));
}

AChunkWorldDamagePlayerCharacter::AChunkWorldDamagePlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);

	DamageTraceInteractionComponent = CreateDefaultSubobject<UPorismHealthInteractionComponent>(TEXT("DamageTraceInteractionComponent"));
	PredictedBlockStateComponent = CreateDefaultSubobject<UPorismPredictedBlockStateComponent>(TEXT("PredictedBlockStateComponent"));
	ChunkWorldProximityComponent = CreateDefaultSubobject<UChunkWorldProximityComponent>(TEXT("ChunkWorldProximityComponent"));
}
