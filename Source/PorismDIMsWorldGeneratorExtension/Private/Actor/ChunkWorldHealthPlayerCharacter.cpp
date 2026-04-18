// Copyright 2026 Spotted Loaf Studio

#include "Actor/ChunkWorldHealthPlayerCharacter.h"

#include "Actor/Components/PorismHealthInteractionComponent.h"
#include "Actor/Components/PorismPredictedBlockStateComponent.h"

AChunkWorldHealthPlayerCharacter::AChunkWorldHealthPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	HealthTraceInteractionComponent = CreateDefaultSubobject<UPorismHealthInteractionComponent>(TEXT("HealthTraceInteractionComponent"));
	PredictedBlockStateComponent = CreateDefaultSubobject<UPorismPredictedBlockStateComponent>(TEXT("PredictedBlockStateComponent"));
}
