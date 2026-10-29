// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChunkWorld/Hit/ChunkWorldBlockDamageTypes.h"
#include "ChunkWorldBlockPresentationSubsystem.generated.h"

class USoundBase;
class UNiagaraSystem;

/** Immutable result of one admitted authority operation, captured before the block can disappear.
 * This is cosmetic data only; consumers must never apply block state from it. */
struct FChunkWorldBlockPresentation
{
	FChunkWorldBlockHealthDeltaResult Result;
	FVector Point = FVector::ZeroVector;
	FVector Normal = FVector::UpVector;
	int32 HealthDelta = 0;
	USoundBase* Sound = nullptr;
	UNiagaraSystem* Niagara = nullptr;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FChunkWorldBlockPresentationEvent, const FChunkWorldBlockPresentation&);

/** World-scoped integration point for games supplying a unified replicated cosmetic channel.
 * A bound consumer owns audio/VFX on this process, including suppression of legacy predicted/settled playback.
 * Block state, removal, destruction actors and prediction reconciliation retain their existing owners. */
UCLASS()
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldBlockPresentationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	FChunkWorldBlockPresentationEvent OnCommitted;
	/** Latches external ownership for this world, so channel teardown cannot resurrect predicted/settled cosmetics. */
	void ClaimPresentation() { bClaimed = true; }
	/** Authority-only game-thread publication. One application produces at most one record. */
	void Publish(const FChunkWorldBlockPresentation& Event);
	/** True only in worlds with an installed unified presentation consumer. */
	static bool HasConsumer(const UWorld* World);
	virtual void Deinitialize() override;
private:
	bool bClaimed = false;
};
