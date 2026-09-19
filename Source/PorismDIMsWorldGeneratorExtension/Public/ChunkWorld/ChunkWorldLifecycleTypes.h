// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"

#include "ChunkWorldLifecycleTypes.generated.h"

/** Source Porism chunk callback observed by the extension after chunk data has entered the world. */
UENUM(BlueprintType)
enum class EChunkWorldChunkLifecycleEventType : uint8
{
	/** Porism created a chunk for the first time in the current generation path. */
	Created UMETA(DisplayName = "Created"),

	/** Porism updated or reloaded an existing chunk representation. Treat this as existing world data. */
	Updated UMETA(DisplayName = "Updated")
};

/** Server-side chunk lifecycle payload forwarded from Porism chunk create/update callbacks. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldObservedChunkLifecycleEvent
{
	GENERATED_BODY()

	/** Chunk origin in block-world coordinates. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Lifecycle", meta = (ToolTip = "Chunk origin in block-world coordinates."))
	FIntVector ChunkBlockWorldPos = FIntVector::ZeroValue;

	/** Porism detail layer for the observed chunk. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Lifecycle", meta = (ToolTip = "Porism detail layer for the observed chunk."))
	int32 DetailLevel = INDEX_NONE;

	/** Callback kind that produced this observation. Created is the first-generation signal. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Lifecycle", meta = (ToolTip = "Callback kind that produced this observation. Created is the first-generation signal."))
	EChunkWorldChunkLifecycleEventType EventType = EChunkWorldChunkLifecycleEventType::Updated;

	/** True when this event was emitted by the authority instance that owns generation. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Lifecycle", meta = (ToolTip = "True when this event was emitted by the authority instance that owns generation."))
	bool bServerAuthority = false;

	/** True when the detail level is the finest active chunk layer for this world. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Lifecycle", meta = (ToolTip = "True when the detail level is the finest active chunk layer for this world."))
	bool bFinestDetail = false;

	/** True only for created chunks observed on the authority generation path. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Lifecycle", meta = (ToolTip = "True only for created chunks observed on the authority generation path. Use this to run one-time chunk generation work."))
	bool bGeneratedForFirstTime = false;
};
