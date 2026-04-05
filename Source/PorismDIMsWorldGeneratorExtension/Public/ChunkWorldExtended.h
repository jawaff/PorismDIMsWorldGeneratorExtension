// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "ChunkWorld/ChunkWorld.h"
#include "ChunkWorldExtended.generated.h"

class UBlockTypeSchemaComponent;

/**
 * Basic extension chunk world that hosts the reusable block type schema component used by project-specific block systems.
 */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API AChunkWorldExtended : public AChunkWorld
{
	GENERATED_BODY()

public:
	/**
	 * Creates a chunk world that includes the reusable block type schema component.
	 */
	AChunkWorldExtended();

	/**
	 * Starts generation and then rebuilds the schema lookup maps once the world indexes are available.
	 */
	virtual void StartGen() override;

	/**
	 * Returns the reusable schema component that owns the block-definition lookup maps.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld")
	UBlockTypeSchemaComponent* GetBlockTypeSchemaComponent() const;

private:
	/**
	 * Reusable schema component that resolves block type schemas and owns the runtime lookup maps.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld", meta = (AllowPrivateAccess = "true", ToolTip = "Reusable schema component that resolves block type schemas."))
	TObjectPtr<UBlockTypeSchemaComponent> BlockTypeSchemaComponent = nullptr;
};
