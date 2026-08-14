// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"

#include "ChunkWorldSpawnTypes.generated.h"

class AActor;
class AChunkWorldExtended;
class AController;
class UObject;

DECLARE_LOG_CATEGORY_EXTERN(LogChunkWorldSpawn, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogChunkWorldSpawnOutOfWorld, Log, All);

/** Identifies the generic binding form a caller uses to correlate a spawn request with project state. */
UENUM(BlueprintType)
enum class EChunkWorldSpawnSubjectKind : uint8
{
	ExistingActor UMETA(DisplayName = "Existing Actor"),
	ControllerOwned UMETA(DisplayName = "Controller Owned"),
	ExternalStableId UMETA(DisplayName = "External Stable Id")
};

/** Identifies the generic source family that contributes a candidate spawn region. */
UENUM(BlueprintType)
enum class EChunkWorldSpawnSourceFamily : uint8
{
	ReservationField UMETA(DisplayName = "Reservation Field"),
	AnchorRegion UMETA(DisplayName = "Anchor Region"),
	SubjectProximity UMETA(DisplayName = "Subject Proximity"),
	NoisePlanned UMETA(DisplayName = "Noise Planned"),
	ExplicitFixture UMETA(DisplayName = "Explicit Fixture")
};

/** Identifies why a caller requested one generic spawn operation. */
UENUM(BlueprintType)
enum class EChunkWorldSpawnReason : uint8
{
	InitialMatchSpawn UMETA(DisplayName = "Initial Match Spawn"),
	Respawn UMETA(DisplayName = "Respawn"),
	OutOfWorldRecovery UMETA(DisplayName = "Out Of World Recovery"),
	RuntimeSpawn UMETA(DisplayName = "Runtime Spawn"),
	PersistentAnchorRespawn UMETA(DisplayName = "Persistent Anchor Respawn"),
	Relocate UMETA(DisplayName = "Relocate"),
	TestPlacement UMETA(DisplayName = "Test Placement")
};

/** Identifies the settled state of a server-authoritative spawn ticket. */
UENUM(BlueprintType)
enum class EChunkWorldSpawnTicketState : uint8
{
	Pending UMETA(DisplayName = "Pending"),
	Ready UMETA(DisplayName = "Ready"),
	Failed UMETA(DisplayName = "Failed"),
	Canceled UMETA(DisplayName = "Canceled")
};

/** Explains why the component could not approve or dispatch a spawn ticket. */
UENUM(BlueprintType)
enum class EChunkWorldSpawnFailureCategory : uint8
{
	None UMETA(DisplayName = "None"),
	NoEligibleSource UMETA(DisplayName = "No Eligible Source"),
	NoEligibleSubjectDefinition UMETA(DisplayName = "No Eligible Subject Definition"),
	SubjectInvalid UMETA(DisplayName = "Subject Invalid"),
	OwnerInvalid UMETA(DisplayName = "Owner Invalid"),
	WorldInvalid UMETA(DisplayName = "World Invalid"),
	UnsafePlacement UMETA(DisplayName = "Unsafe Placement"),
	RequestCanceled UMETA(DisplayName = "Request Canceled"),
	ImplementationUnavailable UMETA(DisplayName = "Implementation Unavailable"),
	RequestInvalid UMETA(DisplayName = "Request Invalid")
};

/** Carries generic project correlation references without exposing project participant types to the plugin. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldSpawnSubjectBinding
{
	GENERATED_BODY()

	/** Generic form used to correlate this request with project state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "Generic form used to correlate this request with project state. The spawn plugin does not infer project meaning from it."))
	EChunkWorldSpawnSubjectKind SubjectKind = EChunkWorldSpawnSubjectKind::ExternalStableId;

	/** Opaque stable identifier supplied by the caller for the ticket lifetime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "Opaque stable identifier supplied by the caller for the ticket lifetime. The spawn plugin does not interpret it."))
	FGuid StableSubjectId;

	/** Existing actor to reposition when this request represents relocation rather than creation. */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "Existing actor to reposition when this request represents relocation rather than creation."))
	TWeakObjectPtr<AActor> ExistingActor;

	/** Optional controller whose lifetime owns this request. */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "Optional controller whose lifetime owns this request. The plugin uses it only for invalidation."))
	TWeakObjectPtr<AController> OwningController;

	/** Optional object whose lifetime cancels a pending request. */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "Optional object whose lifetime cancels a pending request. The plugin uses it only for invalidation."))
	TWeakObjectPtr<UObject> RequestOwner;
};

/** Plugin-owned base for one weighted subject definition. Derived structs remain owned by the consuming project. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldSpawnSubjectDefinitionBase
{
	GENERATED_BODY()

	/** Relative probability used after source-compatible subject definitions have been gathered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Relative probability used after source-compatible subject definitions have been gathered. Zero excludes this definition from weighted selection."))
	float SelectionWeight = 1.0f;

	/** Opaque tags that a candidate source must contain before this definition is eligible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "Opaque tags that a candidate source must contain before this definition is eligible."))
	FGameplayTagContainer RequiredSpawnTags;
};

/** Represents one project-supplied origin snapshot retained for future separately-scoped population policy. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldSpawnOrigin
{
	GENERATED_BODY()

	/** Stable opaque identifier for this origin snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "Stable opaque identifier for this origin snapshot."))
	FGuid StableOriginId;

	/** World location used by source providers that search around an origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "World location used by source providers that search around an origin."))
	FVector WorldLocation = FVector::ZeroVector;

	/** Opaque project tags retained for future population-policy queries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "Opaque project tags retained for future population-policy queries. The plugin does not assign project-specific meaning to them."))
	FGameplayTagContainer Tags;
};

/** Contains one generic direct spawn request. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldSpawnRequest
{
	GENERATED_BODY()

	/** Caller-supplied request id. The component generates one when this value is invalid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "Caller-supplied request id. The component generates one when this value is invalid."))
	FGuid RequestId;

	/** Generic reason for this request. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "Generic reason for this request."))
	EChunkWorldSpawnReason SpawnReason = EChunkWorldSpawnReason::InitialMatchSpawn;

	/** Opaque project correlation binding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "Opaque project correlation binding."))
	FChunkWorldSpawnSubjectBinding SubjectBinding;

	/** Project-derived definitions that the extension validates only through its shared base fields. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.ChunkWorldSpawnSubjectDefinitionBase", ExcludeBaseStruct, ToolTip = "Project-derived definitions that the extension validates only through its shared base fields."))
	TArray<FInstancedStruct> SubjectDefinitions;

	/** Source families in strict request priority order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "Source families in strict request priority order."))
	TArray<EChunkWorldSpawnSourceFamily> AllowedSourceFamiliesInPriorityOrder;

	/** Opaque tags that a selected source must contain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "Opaque tags that a selected source must contain."))
	FGameplayTagContainer RequiredSpawnTags;

	/** Opaque biome tags that a selected source must contain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "Opaque biome tags that a selected source must contain."))
	FGameplayTagContainer RequiredBiomeTags;

	/** Optional world location used by origin-relative providers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "Optional world location used by origin-relative providers."))
	FVector PreferredSearchOrigin = FVector::ZeroVector;

	/** Minimum distance from PreferredSearchOrigin used by origin-relative providers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Minimum distance from PreferredSearchOrigin used by origin-relative providers."))
	float MinimumSearchDistance = 0.0f;

	/** Maximum search radius around PreferredSearchOrigin. Zero uses provider source bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Maximum search radius around PreferredSearchOrigin. Zero uses provider source bounds."))
	float PreferredSearchRadius = 0.0f;

	/** Allows lower-priority source fallback after a higher-priority source family has no valid candidate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk World|Spawn", meta = (ToolTip = "Allows lower-priority source fallback after a higher-priority source family has no valid candidate."))
	bool bAllowLowerPrioritySourceFallback = false;
};

/** Preserves the finite source region approved for one spawn result and later runtime settlement. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldSpawnRegion
{
	GENERATED_BODY()

	/** Inclusive minimum world-space corner of the bounded source region. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "Inclusive minimum world-space corner of the bounded source region."))
	FVector Minimum = FVector::ZeroVector;

	/** Inclusive maximum world-space corner of the bounded source region. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "Inclusive maximum world-space corner of the bounded source region."))
	FVector Maximum = FVector::ZeroVector;
};

/** Captures the generic location-only outcome selected by the extension. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldSpawnResult
{
	GENERATED_BODY()

	/** Request identifier associated with this result. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "Request identifier associated with this result."))
	FGuid RequestId;

	/** Whether the extension approved a destination for execution. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "Whether the extension approved a destination for execution."))
	bool bSuccess = false;

	/** Compact generic reason for an unsuccessful result. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "Compact generic reason for an unsuccessful result."))
	EChunkWorldSpawnFailureCategory FailureCategory = EChunkWorldSpawnFailureCategory::None;

	/** One provisional theoretical location supplied to gameplay for actor creation or reactivation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "One provisional theoretical location supplied to gameplay for actor creation or reactivation. Runtime readiness may later snap the frozen subject to current terrain."))
	FTransform ApprovedTransform = FTransform::Identity;

	/** Opaque project-derived definition selected by the extension without casting it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "Opaque project-derived definition selected by the extension without casting it."))
	FInstancedStruct SelectedSubjectDefinition;

	/** Source family that supplied the selected candidate. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "Source family that supplied the selected candidate."))
	EChunkWorldSpawnSourceFamily SelectedSourceFamily = EChunkWorldSpawnSourceFamily::ReservationField;

	/** Provider-defined source kind used for diagnostics. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "Provider-defined source kind used for diagnostics."))
	FName SelectedSourceKind;

	/** Provider-defined source id used for diagnostics. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "Provider-defined source id used for diagnostics."))
	FName SelectedSourceId;

	/** Finite candidate region handed to game-owned runtime readiness settlement. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "Finite candidate region handed to game-owned runtime readiness settlement."))
	FChunkWorldSpawnRegion ApprovedCandidateRegion;

	/** Human-readable supplemental detail intended for logs and diagnostics. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "Human-readable supplemental detail intended for logs and diagnostics."))
	FString DebugReason;
};

/** Identifies one component-owned spawn ticket. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldSpawnTicketHandle
{
	GENERATED_BODY()

	/** Stable identifier for one component-owned spawn ticket. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "Stable identifier for one component-owned spawn ticket."))
	FGuid TicketId;

	/** Returns true when this handle identifies a component ticket. */
	bool IsValid() const { return TicketId.IsValid(); }
};

/** Reports a generic tracked actor crossing the chunk-world out-of-world boundary. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldOutOfWorldEvent
{
	GENERATED_BODY()

	/** Tracked actor that crossed the configured generic boundary. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "Tracked actor that crossed the configured generic boundary."))
	TWeakObjectPtr<AActor> SubjectActor;

	/** World location observed when the boundary was crossed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "World location observed when the boundary was crossed."))
	FVector WorldLocation = FVector::ZeroVector;

	/** Boundary height used by the generic observer. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "Boundary height used by the generic observer."))
	float KillZ = 0.0f;

	/** World time recorded when the boundary was crossed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "World time recorded when the boundary was crossed."))
	double EventTimeSeconds = 0.0;

	/** Generic diagnostic tag for the boundary observation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk World|Spawn", meta = (ToolTip = "Generic diagnostic tag for the boundary observation."))
	FName ReasonTag = TEXT("BelowKillZ");
};

/** Supplies shared information to one bounded provider candidate query. */
USTRUCT()
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldSpawnQueryContext
{
	GENERATED_BODY()

	/** Owning chunk world that services this query. */
	UPROPERTY(Transient, meta = (ToolTip = "Owning chunk world that services this query."))
	TObjectPtr<AChunkWorldExtended> ChunkWorld = nullptr;

	/** Preferred world-space origin supplied by the request. */
	UPROPERTY(Transient, meta = (ToolTip = "Preferred world-space origin supplied by the request."))
	FVector QueryOrigin = FVector::ZeroVector;

	/** Maximum number of candidates a provider may append for this request. */
	UPROPERTY(Transient, meta = (ToolTip = "Maximum number of candidates a provider may append for this request."))
	int32 MaximumCandidates = 0;
};

/** Describes one bounded source region before the component chooses a point and validates placement. */
USTRUCT()
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldSpawnCandidate
{
	GENERATED_BODY()

	/** Source family that produced this candidate. */
	UPROPERTY(VisibleAnywhere, meta = (ToolTip = "Source family that produced this candidate."))
	EChunkWorldSpawnSourceFamily SourceFamily = EChunkWorldSpawnSourceFamily::ReservationField;

	/** Provider-defined source kind used for diagnostics. */
	UPROPERTY(VisibleAnywhere, meta = (ToolTip = "Provider-defined source kind used for diagnostics."))
	FName SourceKind;

	/** Provider-defined source id used for diagnostics. */
	UPROPERTY(VisibleAnywhere, meta = (ToolTip = "Provider-defined source id used for diagnostics."))
	FName SourceId;

	/** World-space source bounds used for bounded provisional-location sampling and later runtime settlement. */
	FBox SearchBounds = FBox(ForceInit);

	/** Deterministic theoretical location selected by the provider before destination chunks realize. Providers derive it from reservation/noise truth, never realized collision. */
	FVector TheoreticalLocation = FVector::ZeroVector;

	/** The theoretical terrain surface height used before destination chunks realize. Retained for compact diagnostics. */
	double TheoreticalSurfaceZ = 0.0;

	/** Relative source preference used only while selecting candidates from the same source family. */
	UPROPERTY(VisibleAnywhere, meta = (ToolTip = "Relative source preference used only while selecting candidates from the same source family."))
	float BaseScore = 0.0f;

	/** Opaque source tags matched against request and definition requirements. */
	UPROPERTY(VisibleAnywhere, meta = (ToolTip = "Opaque source tags matched against request and definition requirements."))
	FGameplayTagContainer SpawnTags;

	/** Opaque biome tags matched against request requirements. */
	UPROPERTY(VisibleAnywhere, meta = (ToolTip = "Opaque biome tags matched against request requirements."))
	FGameplayTagContainer BiomeTags;

	/** Human-readable provider detail for debug output. */
	UPROPERTY(VisibleAnywhere, meta = (ToolTip = "Human-readable provider detail for debug output."))
	FString DebugLabel;
};

namespace ChunkWorldSpawn
{
	/** Returns a concise stable label for one spawn ticket state. */
	PORISMDIMSWORLDGENERATOREXTENSION_API const TCHAR* ToString(EChunkWorldSpawnTicketState State);


	/** Returns a concise stable label for one generic spawn failure category. */
	PORISMDIMSWORLDGENERATOREXTENSION_API const TCHAR* ToString(EChunkWorldSpawnFailureCategory Category);
}
