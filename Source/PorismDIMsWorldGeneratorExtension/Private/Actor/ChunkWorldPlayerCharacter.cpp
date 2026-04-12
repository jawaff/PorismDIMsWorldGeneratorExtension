// Copyright 2026 Spotted Loaf Studio

#include "Actor/ChunkWorldPlayerCharacter.h"

#include "Actor/Components/PorismTraceInteractionComponent.h"

AChunkWorldPlayerCharacter::AChunkWorldPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TraceInteractionComponent = CreateDefaultSubobject<UPorismTraceInteractionComponent>(TEXT("TraceInteractionComponent"));
}
