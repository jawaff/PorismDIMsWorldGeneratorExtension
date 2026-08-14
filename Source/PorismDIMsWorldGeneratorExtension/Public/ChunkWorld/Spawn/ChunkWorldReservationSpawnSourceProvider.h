// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChunkWorld/Spawn/ChunkWorldSpawnSourceProvider.h"

#include "ChunkWorldReservationSpawnSourceProvider.generated.h"

class AChunkWorldExtended;
struct FBiomeStrategyReservationFieldBinding;
struct FResolvedWorldGenScaleContext;

/** Exposes only explicitly spawn-readable Porism reservation fields as bounded generic spawn candidates. */
UCLASS(ClassGroup = (Porism), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "Chunk World Reservation Spawn Source Provider"))
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldReservationSpawnSourceProvider : public UActorComponent, public IChunkWorldSpawnSourceProvider
{
	GENERATED_BODY()

public:
	/** Creates an enabled reservation-field provider with no background ticking. */
	UChunkWorldReservationSpawnSourceProvider();

protected:
	/** Registers this independently authored provider with its owner's optional spawn coordinator. */
	virtual void BeginPlay() override;

	/** Unregisters this provider before its actor owner tears down. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** Returns the reservation-field source family. */
	virtual EChunkWorldSpawnSourceFamily GetSpawnSourceFamily() const override;

	/** Returns whether the owning live chunk world can query spawn-readable reservation fields for this request. */
	virtual bool CanServeSpawnRequest(const FChunkWorldSpawnRequest& Request) const override;

	/** Returns whether level configuration enables this provider. */
	virtual bool IsSpawnSourceProviderEnabled() const override;

	/** Appends only spawn-readable reservation fields matching request tags and distance bounds. */
	virtual void GatherSpawnCandidates(
		const FChunkWorldSpawnRequest& Request,
		const FChunkWorldSpawnQueryContext& QueryContext,
		TArray<FChunkWorldSpawnCandidate>& OutCandidates) const override;

private:
	/** Returns the active chunk-world owner or null when this component is attached incorrectly. */
	AChunkWorldExtended* GetChunkWorld() const;

	/** Converts finite request search bounds into exact authored-block coordinates through the active scale context; rejects overflow before block conversion. */
	bool BuildAuthoredQueryBounds(
		const FChunkWorldSpawnRequest& Request,
		AChunkWorldExtended& ChunkWorld,
		const FResolvedWorldGenScaleContext& ScaleContext,
		FBox& OutAuthoredBounds) const;

	/** Converts an authored reservation field box into a world-space box through the active scale context and actor transform. */
	static FBox ConvertAuthoredFieldBoundsToWorld(
		const FBox& AuthoredBounds,
		const AChunkWorldExtended& ChunkWorld,
		const FResolvedWorldGenScaleContext& ScaleContext);

	/** Finds the uppermost theoretical GenA terrain crossing at one authored XY without loading destination chunks. */
	bool TryResolveTopTheoreticalSurface(
		const FBiomeStrategyReservationFieldBinding& Binding,
		AChunkWorldExtended& ChunkWorld,
		const FResolvedWorldGenScaleContext& ScaleContext,
		const FVector2D& AuthoredXY,
		double& OutSurfaceZBlock) const;

	/** Enables this provider for level-authored reservation-field spawning. */
	UPROPERTY(EditAnywhere, Category = "Chunk World|Spawn", meta = (ToolTip = "Enables this provider for level-authored reservation-field spawning. Disabled providers never contribute candidates."))
	bool bEnabled = true;

	/** Maximum reservation candidates this provider may append for one component query. */
	UPROPERTY(EditAnywhere, Category = "Chunk World|Spawn", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Maximum reservation candidates this provider may append for one component query. This prevents one authored world from producing an unbounded candidate list."))
	int32 MaximumCandidatesPerQuery = 32;

	/** Maximum theoretical vertical GenA samples allowed while finding a field's topmost pre-realization terrain surface. Wider fields are rejected rather than approximated. */
	UPROPERTY(EditAnywhere, Category = "Chunk World|Spawn", meta = (ClampMin = "2", UIMin = "2", ToolTip = "Maximum theoretical vertical GenA samples allowed while finding a field's topmost pre-realization terrain surface. Wider fields are rejected rather than approximated."))
	int32 MaximumTheoreticalSurfaceSamples = 257;
};
