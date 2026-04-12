// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "ChunkWorldExtended/ChunkWorldWalker.h"
#include "GameFramework/Character.h"
#include "ChunkWorldStandalonePlayerCharacterBase.generated.h"

class AChunkWorldExtended;
class UChunkWorldProximityComponent;
class UPorismStartupFreezeComponent;

/**
 * Shared standalone player-character base that owns chunk-world walker registration, client binding, and startup freeze.
 */
UCLASS(Abstract, BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API AChunkWorldStandalonePlayerCharacterBase : public ACharacter, public IChunkWorldWalker
{
	GENERATED_BODY()

public:
	/** Creates a minimal standalone player character that participates in chunk-world startup orchestration. */
	AChunkWorldStandalonePlayerCharacterBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Returns the shared proximity source used by plugin-owned chunk-world swap scanning. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Character")
	UChunkWorldProximityComponent* GetChunkWorldProximityComponent() const { return ChunkWorldProximityComponent; }

	/** Returns the plugin-owned startup freeze component used to hold this actor in place until chunk-world startup is safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Character")
	UPorismStartupFreezeComponent* GetStartupFreezeComponent() const { return StartupFreezeComponent; }

	/** Returns the most recent chunk-world walker info reported by Porism for this actor. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Character")
	const FChunkWorldWalkerInfo& GetLastWalkerPositionInfo() const { return LastWalkerPositionInfo; }

	/** Returns the chunk-world walker trace location used to drive startup loading around this actor. */
	virtual FVector GetTracingLocation_Implementation() const override;

	/** Returns the chunk-world walker trace vector used to drive startup loading around this actor. */
	virtual FVector GetTracingVector_Implementation() const override;

	/** Returns the chunk-world loading distance multipliers used by Porism for this actor. */
	virtual TArray<double> GetViewDistanceMultiplier_Implementation() const override;

	/** Caches the most recent walker position info dispatched by Porism for startup diagnostics and readiness tracking. */
	virtual void WalkerPositionInfo_Implementation(FChunkWorldWalkerInfo ChunkWorldWalkerInfo) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_Controller() override;

	/** Attempts to register this actor with chunk worlds and bind networked chunk-world clients when required. */
	void InitializeChunkWorldPlayerSetup();

	/** Registers this actor as a chunk-world walker on every available running chunk world. */
	bool RegisterWithChunkWorlds();

	/** Unregisters this actor from all chunk worlds it previously registered with. */
	void UnregisterFromChunkWorlds();

	/** Binds every available running chunk world to this actor's owning player controller on the authority side. */
	bool BindChunkWorldClients();

	/** Schedules a short retry when chunk worlds or player-controller ownership are not ready yet. */
	void ScheduleChunkWorldSetupRetry();

	/** Cancels any pending startup setup retry timer. */
	void CancelChunkWorldSetupRetry();

	/** Executes one delayed retry of the startup chunk-world player setup. */
	UFUNCTION()
	void HandleChunkWorldSetupRetry();

	/** Gathers all chunk-world actors currently available in this world. */
	TArray<AChunkWorldExtended*> GatherChunkWorlds() const;

	/** Returns a readable net mode label for startup logging. */
	static const TCHAR* GetNetModeName(const UWorld* World);

	/** Shared proximity source used by plugin-owned chunk-world swap scanning. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld|Character", meta = (AllowPrivateAccess = "true", ToolTip = "Shared proximity source used by plugin-owned chunk-world swap scanning."))
	TObjectPtr<UChunkWorldProximityComponent> ChunkWorldProximityComponent = nullptr;

	/** Plugin-owned startup freeze component used to keep this actor pinned until relevant chunk worlds are ready. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld|Character", meta = (AllowPrivateAccess = "true", ToolTip = "Plugin-owned startup freeze component used to keep this actor pinned until relevant chunk worlds are ready."))
	TObjectPtr<UPorismStartupFreezeComponent> StartupFreezeComponent = nullptr;

	/** Optional offset applied to the actor location before Porism evaluates startup loading around this walker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Character", meta = (ToolTip = "Optional offset applied to the actor location before Porism evaluates startup loading around this walker."))
	FVector WalkerTraceLocationOffset = FVector::ZeroVector;

	/** Optional loading distance multipliers passed through to Porism for this standalone player character. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Character", meta = (ToolTip = "Optional loading distance multipliers passed through to Porism for this standalone player character."))
	TArray<double> ViewDistanceMultiplier = { 1.0 };

	/** Delay between retries while this actor waits for chunk worlds or player-controller ownership to become available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Character", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Delay between retries while this actor waits for chunk worlds or player-controller ownership to become available."))
	float ChunkWorldSetupRetryIntervalSeconds = 0.25f;

	/** Maximum number of delayed retries while startup chunk-world setup waits for chunk worlds or controller ownership. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Character", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Maximum number of delayed retries while startup chunk-world setup waits for chunk worlds or controller ownership."))
	int32 MaxChunkWorldSetupRetryAttempts = 20;

private:
	/** Most recent walker info reported by Porism for this actor. */
	FChunkWorldWalkerInfo LastWalkerPositionInfo;

	/** Chunk worlds this actor has already registered itself with as a walker. */
	TArray<TWeakObjectPtr<AChunkWorldExtended>> RegisteredChunkWorlds;

	/** Chunk worlds this actor has already bound to its owning player controller on the authority side. */
	TArray<TWeakObjectPtr<AChunkWorldExtended>> BoundChunkWorlds;

	/** Timer used to retry startup chunk-world setup when worlds or player ownership are not ready yet. */
	FTimerHandle ChunkWorldSetupRetryTimerHandle;

	/** Number of startup setup attempts made so far for this actor. */
	int32 ChunkWorldSetupRetryAttempts = 0;
};
