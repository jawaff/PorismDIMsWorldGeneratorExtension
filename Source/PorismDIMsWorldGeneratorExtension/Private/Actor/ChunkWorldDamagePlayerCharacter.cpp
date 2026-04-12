// Copyright 2026 Spotted Loaf Studio

#include "Actor/ChunkWorldDamagePlayerCharacter.h"

#include "Actor/Components/PorismHealthInteractionComponent.h"
#include "Actor/Components/PorismPredictedBlockStateComponent.h"

AChunkWorldDamagePlayerCharacter::AChunkWorldDamagePlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DamageTraceInteractionComponent = CreateDefaultSubobject<UPorismHealthInteractionComponent>(TEXT("DamageTraceInteractionComponent"));
	PredictedBlockStateComponent = CreateDefaultSubobject<UPorismPredictedBlockStateComponent>(TEXT("PredictedBlockStateComponent"));
}
