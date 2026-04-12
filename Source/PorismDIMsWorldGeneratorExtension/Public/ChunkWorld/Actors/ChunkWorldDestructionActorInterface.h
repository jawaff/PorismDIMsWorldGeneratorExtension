// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Interface.h"

#include "ChunkWorldDestructionActorInterface.generated.h"

class AChunkWorld;

/**
 * Compact destruction presentation request used when a damage-capable block is authoritatively removed.
 */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldBlockDestructionRequest
{
	GENERATED_BODY()

	/** Chunk world that owned the destroyed block when the request was created. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction")
	TObjectPtr<AChunkWorld> ChunkWorld = nullptr;

	/** Semantic block type that authored the destruction presentation. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction")
	FGameplayTag BlockTypeName;

	/** Block world position that was authoritatively removed. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction")
	FIntVector BlockWorldPos = FIntVector::ZeroValue;

	/** Presentation transform chosen for the destruction actor, usually the active swap transform when available. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction")
	FTransform SpawnTransform = FTransform::Identity;

	/** Representative world position captured for the destroyed block when available. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction")
	FVector RepresentativeWorldPos = FVector::ZeroVector;
};

/**
 * Replicated one-shot destruction trigger state so each net instance can execute the same local presentation setup once.
 */
USTRUCT()
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldReplicatedDestructionTriggerState
{
	GENERATED_BODY()

	/** Monotonic trigger serial used so initial replication and later replays both share one idempotent entry point. */
	UPROPERTY()
	int32 TriggerSerial = 0;

	/** Destruction request payload accepted on the authority before local presentation begins. */
	UPROPERTY()
	FChunkWorldBlockDestructionRequest Request;
};

/**
 * Optional destruction trigger contract for authored destruction presentation actors.
 */
UINTERFACE(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldDestructionActorInterface : public UInterface
{
	GENERATED_BODY()
};

class PORISMDIMSWORLDGENERATOREXTENSION_API IChunkWorldDestructionActorInterface
{
	GENERATED_BODY()

public:
	/**
	 * Framework-owned entry point that starts one authored destruction presentation for a block that was just removed.
	 * Override this only when the actor needs to change authoritative trigger handling and always call the parent/base implementation.
	 * Blueprint-authored local presentation effects should live in the actor-specific local hook instead of replacing this interface entry point.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Block|ChunkWorld|Destruction")
	void TriggerBlockDestruction(const FChunkWorldBlockDestructionRequest& Request);
};
