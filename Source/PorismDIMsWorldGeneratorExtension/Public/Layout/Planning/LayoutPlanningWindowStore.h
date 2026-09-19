// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Contracts/LayoutContractTypes.h"
#include "Layout/Types/LayoutTypes.h"
#include "UObject/Object.h"

#include "LayoutPlanningWindowStore.generated.h"

class UObject;
class ULayoutProfileAsset;
class ULayoutRegionContentSetAsset;
class ULayoutWorldBindingAsset;

/** Unit used when expressing a planning-window dimension or sample spacing. */
UENUM(BlueprintType)
enum class ELayoutPlanningWindowUnit : uint8
{
	Blocks,
	Chunks
};

/** Server-side lifecycle state for one planned layout site. */
UENUM(BlueprintType)
enum class EPlannedLayoutSiteState : uint8
{
	Pending,
	Accepted,
	Rejected,
	Realized
};

/** Transient frozen-submission state for one planned site attempt. */
UENUM(BlueprintType)
enum class ELayoutPlannedSiteFrozenSubmissionState : uint8
{
	None,
	DescriptorReady,
	SolveQueued,
	CompletedAwaitingPublish,
	Accepted,
	Rejected,
	Tombstoned
};

/** Lifecycle state for one retained exported continuation endpoint. */
UENUM(BlueprintType)
enum class ELayoutContinuationEndpointState : uint8
{
	Planned,
	Ready,
	Reserved,
	Connected,
	Expired
};

/** Compact planning-window record for one root entry under one continuation family. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutPlanningWindowEndpointRecord
{
	GENERATED_BODY()

	/** Stable identity for this root-entry-family availability record. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning")
	FString StableEndpointKey;

	/** Stable planned-root identity that owns this exported entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning")
	FString RootRecordKey;

	/** Binding identity that owns this endpoint's continuation families. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning")
	FName WorldBindingId;

	/** Exported entry geometry and tag used by both automatic and editor selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning")
	FResolvedLayoutConnectorEndpoint Endpoint;

	/** Continuation family that owns this availability slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning")
	FName ContinuationFamilyId;

	/** Shared root/family limit, including reserved, in-flight and committed routes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ClampMin = "1", ToolTip = "Binding family limit shared by every entry on this root, including pending routes."))
	int32 MaxConnectionsPerSite = 1;

	/** Remaining connections allowed for this family at this root entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning")
	int32 RemainingConnections = 0;

	/** Current compact endpoint lifecycle state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning")
	ELayoutContinuationEndpointState State = ELayoutContinuationEndpointState::Planned;

	/** Pending pair reservation, empty while endpoint is available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning")
	FString ReservedEdgeKey;
};

/** Lifecycle state for a retained continuation edge. */
UENUM(BlueprintType)
enum class ELayoutContinuationEdgeState : uint8
{
	Reserved,
	Committed,
	Failed
};

/** Compact authoritative reservation for one unordered continuation endpoint pair. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutPlanningWindowContinuationEdgeRecord
{
	GENERATED_BODY()

	/** Stable unordered identity for this family/candidate endpoint pair. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning")
	FString EdgeKey;

	/** Stable endpoint identities and root ownership for this reservation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning")
	FString StartEndpointKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning")
	FString EndEndpointKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning")
	FString StartRootRecordKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning")
	FString EndRootRecordKey;

	/** Family scope for aggregate capacity and unordered root-pair uniqueness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Continuation family owning this reserved or committed root connection."))
	FName ContinuationFamilyId;

	/** Compact endpoint positions retained only to prune a committed edge after capacity removes endpoint records. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning")
	FIntVector StartEndpointBlockWorldPos = FIntVector::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning")
	FIntVector EndEndpointBlockWorldPos = FIntVector::ZeroValue;

	/** Reservation becomes committed only after connector writes succeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning")
	ELayoutContinuationEdgeState State = ELayoutContinuationEdgeState::Reserved;
};

/** Sampling resolution for loaded-terrain biome discovery; capacity belongs to the runtime owner. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutPlanningWindowSettings
{
	GENERATED_BODY()

	/** Upper bound on scheduling-region scale; automatic candidates still use the binding's normal-cell lattice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Upper bound on automatic planning-region scale in the selected units. Runtime clamps this to the smallest binding XY cell dimension; each region spans four scales per axis. Candidate centers use each binding's normal-cell lattice, not this value as a sampling stride. Smaller values split work into smaller regions; larger values cannot skip normal-cell candidates."))
	int32 SampleSpacing = 16;

	/** Unit used for SampleSpacing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Unit used for SampleSpacing."))
	ELayoutPlanningWindowUnit SampleSpacingUnit = ELayoutPlanningWindowUnit::Blocks;

	/** Validates this planning-window settings struct. */
	FLayoutValidationResult Validate() const;
};

/** Minimal reservation/source carrier preserved on planned site records before realization import. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutPlannedSiteReservationSourceSelection
{
	GENERATED_BODY()

	/** Authored world binding asset that produced this planned site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Authored world binding asset that produced this planned site."))
	TSoftObjectPtr<ULayoutWorldBindingAsset> WorldBinding;

	/** Exact site-center XY lookup key; full identity belongs to the root publication ID. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Exact block-world site-center XY lookup key. Binding footprint reservations control spacing; this key does not quantize sites onto a second grid."))
	FIntPoint ReservationKey = FIntPoint::ZeroValue;

	/** Deterministic center in base-block world coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Deterministic center in base-block world coordinates."))
	FIntVector SiteCenterBlockWorldPos = FIntVector::ZeroValue;

	/** Stable world seed used when this planned record was produced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable world seed used when this planned record was produced."))
	int32 WorldSeed = 0;
};

/** Stable identity plus lifecycle outcome preserved on planned site records across planning-window transitions. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutPlannedSiteLifecycleMetadata
{
	GENERATED_BODY()

	/** Stable identity for this planned layout site. Generated when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable identity for this planned layout site. Generated when empty."))
	FString StableRecordKey;

	/** Current server-side planned-site lifecycle state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Current server-side planned-site lifecycle state."))
	EPlannedLayoutSiteState State = EPlannedLayoutSiteState::Pending;

	/** Optional reason set when the planned site is rejected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Optional reason set when the planned site is rejected."))
	FString RejectionReason;

	/** Structured world-binding terrain-fit or continuation-endpoint outcome preserved across planning-window lifecycle transitions when known. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Structured world-binding terrain-fit or continuation-endpoint outcome preserved across planning-window lifecycle transitions when known."))
	ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind = ELayoutWorldBindingTerrainFitDiagnosticKind::None;

	/** Minimal solved footprint retained after realization so realized sites continue blocking planning-window overlaps without retaining solve payload. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Solved footprint dimensions retained after realization for overlap rejection. Zero means legacy realized records have no recoverable footprint authority."))
	FIntPoint RetainedSolvedFootprintSize = FIntPoint::ZeroValue;

	/** Transient frozen descriptor lifecycle state used to prevent duplicate publishable solves for this record. */
	ELayoutPlannedSiteFrozenSubmissionState FrozenSubmissionState = ELayoutPlannedSiteFrozenSubmissionState::None;

	/** Descriptor id expected by the currently publishable attempt, if any. */
	FLayoutId FrozenSubmissionDescriptorId;

	/** Generation expected by the currently publishable attempt. */
	uint64 FrozenSubmissionGeneration = 0;

	/** Attempt index expected by the currently publishable attempt. */
	int32 FrozenSubmissionAttemptIndex = 0;

	/** Audit hash expected by the currently publishable attempt. */
	int32 FrozenSubmissionAuditHash = 0;
};

/** Cached accepted-solve payload preserved on planned site records before realization import. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutPlannedSiteAcceptedSolvePayload
{
	GENERATED_BODY()

	/** Cached solve result once the site has been accepted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Cached solve result once the site has been accepted."))
	FLayoutSolveResult SolveResult;

	/** Frozen terrain contract preserved on accepted planned records so later realization can replay the same terrain artifact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Frozen terrain contract preserved on accepted planned records so later realization can replay the same terrain artifact without reopening terrain-fit decisions."))
	FLayoutFrozenTerrainContract FrozenTerrainContract;

	/** Stable solved artifact id accepted by publication before realization-prep consumes the legacy solve payload. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable solved artifact id accepted by publication before realization-prep consumes the legacy solve payload."))
	FLayoutId SolvedArtifactId;

	/** Number of active cells validated on the accepted solved artifact before downstream writes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Number of active cells validated on the accepted solved artifact before downstream writes."))
	int32 SolvedArtifactActiveCellCount = 0;
};

/** One server-side planned layout record that can exist before chunks are loaded. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FPlannedLayoutSiteRecord
{
	GENERATED_BODY()

	/** Stable identity for this planned layout site. Generated when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable identity for this planned layout site. Generated when empty."))
	FString StableRecordKey;

	/** Authored world binding asset that produced this planned site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Authored world binding asset that produced this planned site."))
	TSoftObjectPtr<ULayoutWorldBindingAsset> WorldBinding;

	/** Stable world-binding identity that produced this planned site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable world-binding identity that produced this planned site."))
	FName WorldBindingId;

	/** Stable weighted-candidate identity selected for this planned site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable weighted-candidate identity selected for this planned site."))
	FName WorldBindingCandidateId;

	/** Minimal stable continuation-selection contract chosen before any world-facing continuation solve begins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Minimal stable continuation-selection contract chosen before any world-facing continuation solve begins. Ordinary roots leave this unset."))
	FLayoutResolvedWorldBindingContinuationSelection ResolvedContinuationSelection;

	/** Project-authored biome row selected for this planned site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Project-authored biome row selected for this planned site."))
	FName BiomeRowName;

	/** Stable root solve id preserved from the request/publication surface for later cache, replay, and trace correlation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable root solve id preserved from the request/publication surface for later cache, replay, and trace correlation. Accepted planning records keep the stable planning-window record identity here."))
	FLayoutId RootSolveId;

	/** Stable root candidate id preserved from the request/publication surface for later cache, replay, and trace correlation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable root candidate id preserved from the request/publication surface for later cache, replay, and trace correlation. Accepted planning records keep the selected world-binding candidate id here."))
	FLayoutId RootCandidateId;

	/** Stable root placement-policy id preserved from the request/publication surface for later cache, replay, and world-binding trace correlation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable root placement-policy id preserved from the request/publication surface for later cache, replay, and world-binding trace correlation. Accepted planning records keep the selected world-binding policy owner here."))
	FLayoutId RootPlacementPolicyId;

	/** Exact site-center XY lookup key; full identity belongs to the root publication ID. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Exact block-world site-center XY lookup key. Binding footprint reservations control spacing; this key does not quantize sites onto a second grid."))
	FIntPoint ReservationKey = FIntPoint::ZeroValue;

	/** Deterministic center in base-block world coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Deterministic center in base-block world coordinates."))
	FIntVector SiteCenterBlockWorldPos = FIntVector::ZeroValue;

	/** True when Planning Window discovery selected this site through an environment-specific producer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "True when Planning Window discovery selected this site through a Surface or Underground candidate producer before solve publication."))
	bool bHasDiscoveredEnvironmentMode = false;

	/** Frozen environment mode selected by Planning Window discovery. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (EditCondition = "bHasDiscoveredEnvironmentMode", ToolTip = "True for an Underground candidate; false for a Surface candidate."))
	bool bDiscoveredUnderground = false;

	/** True when discovery resolved bounded procedural column occupancy, independent of loaded terrain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (EditCondition = "bHasDiscoveredEnvironmentMode", ToolTip = "When true, prewarm must fail closed if procedural column evidence no longer matches the discovered environment."))
	bool bEnvironmentDiscoveryQualifiedProceduralOccupancy = false;

	/** Layout profile selected for this planned site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Layout profile selected for this planned site."))
	TSoftObjectPtr<ULayoutProfileAsset> LayoutProfile;

	/** Unified content set selected for this planned site when the profile uses the final authored content-entry model. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Unified content set selected for this planned site when the profile uses the final authored content-entry model."))
	TSoftObjectPtr<ULayoutRegionContentSetAsset> LayoutContentSet;

	/** Binding-owned exported endpoint connector tags compiled for this planned root selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (Categories = "Layout.Connector", ToolTip = "Binding-owned exported endpoint connector tags compiled for this planned root selection. Preserve the selected world-facing export contract here so later accepted import and solved-site publication do not need to reread structural profile authoring."))
	FGameplayTagContainer ExportedConnectorTypeTags;

	/** Stable world seed used when this planned record was produced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable world seed used when this planned record was produced."))
	int32 WorldSeed = 0;

	/** Deterministic solve seed assigned to this planned site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Deterministic solve seed assigned to this planned site."))
	int32 SolveSeed = 0;

	/** Stable discovery candidate id used by bounded hot-submit scouting and retry bookkeeping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable discovery candidate id used by bounded hot-submit scouting and retry bookkeeping."))
	FLayoutId DiscoveryCandidateId;

	/** Deterministic discovery order assigned before background enqueue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Deterministic discovery order assigned before background enqueue."))
	int32 DiscoveryOrderIndex = INDEX_NONE;

	/** Stable terrain-backed placement shift id used by bounded hot-submit scouting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable terrain-backed placement shift id used by bounded hot-submit scouting so resumed discovery keeps the same coarse-placement branch."))
	FLayoutId DiscoveryPlacementShiftId;

	/** Coarse placement shift cells selected before enqueue for deterministic shifted-branch replay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Coarse placement shift cells selected before enqueue for deterministic shifted-branch replay and fail-closed request/manifest validation."))
	FIntVector DiscoveryPlacementShiftCells = FIntVector::ZeroValue;

	/** Solve seed selected for the discovered branch after any shifted site-center snap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Solve seed selected for the discovered branch after any shifted site-center snap so resumed planning can verify exact shifted request identity."))
	int32 DiscoverySolveSeed = 0;

	/** Stable terrain-backed candidate ordering id used by bounded hot-submit priority bookkeeping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable terrain-backed candidate ordering id used by bounded hot-submit priority bookkeeping without rereading live terrain."))
	FLayoutId DiscoveryOrderingId;

	/** Background solve priority assigned from deterministic discovery and terrain-backed shift order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Background solve priority assigned from deterministic discovery and terrain-backed shift order for replayable enqueue order."))
	int32 DiscoveryPriority = 0;

	/** Cached solve result once the site has been accepted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Cached solve result once the site has been accepted."))
	FLayoutSolveResult SolveResult;

	/** Frozen terrain contract preserved on accepted planned records for later realization replay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Frozen terrain contract preserved on accepted planned records for later realization replay without reopening terrain-fit decisions."))
	FLayoutFrozenTerrainContract FrozenTerrainContract;

	/** Stable solved artifact id accepted by publication before realization-prep consumes the legacy solve payload. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Stable solved artifact id accepted by publication before realization-prep consumes the legacy solve payload."))
	FLayoutId SolvedArtifactId;

	/** Number of active cells validated on the accepted solved artifact before downstream writes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Number of active cells validated on the accepted solved artifact before downstream writes."))
	int32 SolvedArtifactActiveCellCount = 0;

	/** Current server-side planned-site lifecycle state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Current server-side planned-site lifecycle state."))
	EPlannedLayoutSiteState State = EPlannedLayoutSiteState::Pending;

	/** Optional reason set when the planned site is rejected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Optional reason set when the planned site is rejected."))
	FString RejectionReason;

	/** Structured world-binding terrain-fit or continuation-endpoint outcome preserved on rejected planning records when known. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Structured world-binding terrain-fit or continuation-endpoint outcome preserved on rejected planning records when known."))
	ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind = ELayoutWorldBindingTerrainFitDiagnosticKind::None;

	/** Minimal solved footprint retained after realization so this record continues blocking planning-window overlaps without retaining solve payload. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Solved footprint dimensions retained after realization for overlap rejection. Zero means legacy realized records have no recoverable footprint authority."))
	FIntPoint RetainedSolvedFootprintSize = FIntPoint::ZeroValue;

	/** Transient frozen descriptor lifecycle state used by the runtime descriptor-store guard. */
	ELayoutPlannedSiteFrozenSubmissionState FrozenSubmissionState = ELayoutPlannedSiteFrozenSubmissionState::None;

	/** Transient descriptor id expected by the currently publishable attempt. */
	FLayoutId FrozenSubmissionDescriptorId;

	/** Transient descriptor generation expected by the currently publishable attempt. */
	uint64 FrozenSubmissionGeneration = 0;

	/** Transient descriptor attempt index expected by the currently publishable attempt. */
	int32 FrozenSubmissionAttemptIndex = 0;

	/** Transient descriptor audit hash expected by the currently publishable attempt. */
	int32 FrozenSubmissionAuditHash = 0;

	/** Returns the minimal world-binding frontend carrier preserved on this planned site record. */
	FLayoutWorldBindingSiteFrontendSelection GetWorldBindingFrontendSelection() const
	{
		FLayoutWorldBindingSiteFrontendSelection Selection;
		Selection.WorldBindingId = WorldBindingId;
		Selection.WorldBindingCandidateId = WorldBindingCandidateId;
		Selection.ResolvedContinuationSelection = ResolvedContinuationSelection;
		Selection.BiomeRowName = BiomeRowName;
		return Selection;
	}

	/** Replaces the minimal world-binding frontend carrier preserved on this planned site record. */
	void SetWorldBindingFrontendSelection(const FLayoutWorldBindingSiteFrontendSelection& InSelection)
	{
		WorldBindingId = InSelection.WorldBindingId;
		WorldBindingCandidateId = InSelection.WorldBindingCandidateId;
		ResolvedContinuationSelection = InSelection.ResolvedContinuationSelection;
		BiomeRowName = InSelection.BiomeRowName;
	}

	/** Returns the minimal root publication carrier preserved on this planned site record. */
	FLayoutRootPublicationMetadata GetRootPublicationMetadata() const
	{
		FLayoutRootPublicationMetadata Metadata;
		Metadata.RootSolveId = RootSolveId;
		Metadata.RootCandidateId = RootCandidateId;
		Metadata.RootPlacementPolicyId = RootPlacementPolicyId;
		return Metadata;
	}

	/** Replaces the minimal root publication carrier preserved on this planned site record. */
	void SetRootPublicationMetadata(const FLayoutRootPublicationMetadata& InMetadata)
	{
		RootSolveId = InMetadata.RootSolveId;
		RootCandidateId = InMetadata.RootCandidateId;
		RootPlacementPolicyId = InMetadata.RootPlacementPolicyId;
	}

	/** Returns the minimal authored solve-source carrier preserved on this planned site record. */
	FLayoutSiteSolveSourceSelection GetSiteSolveSourceSelection() const
	{
		FLayoutSiteSolveSourceSelection SourceSelection;
		SourceSelection.LayoutProfile = LayoutProfile;
		SourceSelection.ContentSet = LayoutContentSet;
		SourceSelection.ExportedConnectorTypeTags = ExportedConnectorTypeTags;
		SourceSelection.SolveSeed = SolveSeed;
		return SourceSelection;
	}

	/** Replaces the minimal authored solve-source carrier preserved on this planned site record. */
	void SetSiteSolveSourceSelection(const FLayoutSiteSolveSourceSelection& InSelection)
	{
		LayoutProfile = InSelection.LayoutProfile;
		LayoutContentSet = InSelection.ContentSet;
		ExportedConnectorTypeTags = InSelection.ExportedConnectorTypeTags;
		SolveSeed = InSelection.SolveSeed;
	}

	/** Returns the minimal reservation/source carrier preserved on this planned site record. */
	FLayoutPlannedSiteReservationSourceSelection GetPlannedSiteReservationSourceSelection() const
	{
		FLayoutPlannedSiteReservationSourceSelection Selection;
		Selection.WorldBinding = WorldBinding;
		Selection.ReservationKey = ReservationKey;
		Selection.SiteCenterBlockWorldPos = SiteCenterBlockWorldPos;
		Selection.WorldSeed = WorldSeed;
		return Selection;
	}

	/** Replaces the minimal reservation/source carrier preserved on this planned site record. */
	void SetPlannedSiteReservationSourceSelection(const FLayoutPlannedSiteReservationSourceSelection& InSelection)
	{
		WorldBinding = InSelection.WorldBinding;
		ReservationKey = InSelection.ReservationKey;
		SiteCenterBlockWorldPos = InSelection.SiteCenterBlockWorldPos;
		WorldSeed = InSelection.WorldSeed;
	}

	/** Returns the stable identity plus lifecycle outcome preserved on this planned site record. */
	FLayoutPlannedSiteLifecycleMetadata GetPlannedSiteLifecycleMetadata() const
	{
		FLayoutPlannedSiteLifecycleMetadata Metadata;
		Metadata.StableRecordKey = StableRecordKey;
		Metadata.State = State;
		Metadata.RejectionReason = RejectionReason;
		Metadata.TerrainFitDiagnosticKind = TerrainFitDiagnosticKind;
		Metadata.RetainedSolvedFootprintSize = RetainedSolvedFootprintSize;
		Metadata.FrozenSubmissionState = FrozenSubmissionState;
		Metadata.FrozenSubmissionDescriptorId = FrozenSubmissionDescriptorId;
		Metadata.FrozenSubmissionGeneration = FrozenSubmissionGeneration;
		Metadata.FrozenSubmissionAttemptIndex = FrozenSubmissionAttemptIndex;
		Metadata.FrozenSubmissionAuditHash = FrozenSubmissionAuditHash;
		return Metadata;
	}

	/** Replaces the stable identity plus lifecycle outcome preserved on this planned site record. */
	void SetPlannedSiteLifecycleMetadata(const FLayoutPlannedSiteLifecycleMetadata& InMetadata)
	{
		StableRecordKey = InMetadata.StableRecordKey;
		State = InMetadata.State;
		RejectionReason = InMetadata.RejectionReason;
		TerrainFitDiagnosticKind = InMetadata.TerrainFitDiagnosticKind;
		RetainedSolvedFootprintSize = InMetadata.RetainedSolvedFootprintSize;
		FrozenSubmissionState = InMetadata.FrozenSubmissionState;
		FrozenSubmissionDescriptorId = InMetadata.FrozenSubmissionDescriptorId;
		FrozenSubmissionGeneration = InMetadata.FrozenSubmissionGeneration;
		FrozenSubmissionAttemptIndex = InMetadata.FrozenSubmissionAttemptIndex;
		FrozenSubmissionAuditHash = InMetadata.FrozenSubmissionAuditHash;
	}

	/** Returns the cached accepted-solve payload preserved on this planned site record. */
	FLayoutPlannedSiteAcceptedSolvePayload GetPlannedSiteAcceptedSolvePayload() const
	{
		FLayoutPlannedSiteAcceptedSolvePayload Payload;
		Payload.SolveResult = SolveResult;
		Payload.FrozenTerrainContract = FrozenTerrainContract;
		Payload.SolvedArtifactId = SolvedArtifactId;
		Payload.SolvedArtifactActiveCellCount = SolvedArtifactActiveCellCount;
		return Payload;
	}

	/** Replaces the cached accepted-solve payload preserved on this planned site record. */
	void SetPlannedSiteAcceptedSolvePayload(const FLayoutPlannedSiteAcceptedSolvePayload& InPayload)
	{
		SolveResult = InPayload.SolveResult;
		FrozenTerrainContract = InPayload.FrozenTerrainContract;
		SolvedArtifactId = InPayload.SolvedArtifactId;
		SolvedArtifactActiveCellCount = InPayload.SolvedArtifactActiveCellCount;
	}
};

/** Server-owned planned-layout store used by the chunk-world runtime component. */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API ULayoutPlanningWindowStore : public UObject
{
	GENERATED_BODY()

public:
	/** Validates planning-window settings. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	static FLayoutValidationResult ValidatePlanningWindowSettings(const FLayoutPlanningWindowSettings& InSettings);

	/** Builds a deterministic planned-record key from the fields that identify one layout site. */
	UFUNCTION(BlueprintPure, Category = "Layout|Planning")
	static FString MakePlannedLayoutSiteRecordKey(
		FName WorldBindingId,
		FName WorldBindingCandidateId,
		FLayoutResolvedWorldBindingContinuationSelection ResolvedContinuationSelection,
		FName BiomeRowName,
		FIntPoint ReservationKey,
		FIntVector SiteCenterBlockWorldPos,
		int32 WorldSeed);

	/** Builds a deterministic planned-record key from one explicit planned-site frontend carrier. */
	static FString MakePlannedLayoutSiteRecordKeyFromFrontendSelection(
		const FLayoutWorldBindingSiteFrontendSelection& FrontendSelection,
		FIntPoint ReservationKey,
		FIntVector SiteCenterBlockWorldPos,
		int32 WorldSeed);

	/** Builds a deterministic planned-record key from the explicit planned-site frontend and reservation/source carriers. */
	static FString MakePlannedLayoutSiteRecordKeyFromSelections(
		const FLayoutWorldBindingSiteFrontendSelection& FrontendSelection,
		const FLayoutPlannedSiteReservationSourceSelection& ReservationSourceSelection);

	/** Replaces the settings used by this store. Invalid settings are ignored and reported. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	FLayoutValidationResult SetPlanningWindowSettings(const FLayoutPlanningWindowSettings& InSettings);

	/** Returns the currently configured planning-window settings. */
	UFUNCTION(BlueprintPure, Category = "Layout|Planning")
	FLayoutPlanningWindowSettings GetPlanningWindowSettings() const { return Settings; }

	/** Removes all planned records from this store. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	void ResetPlannedLayoutSiteRecords();

	/** Adds a pending record or returns the existing record with the same stable key. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	bool UpsertPendingPlannedLayoutSiteRecord(
		const FPlannedLayoutSiteRecord& InRecord,
		FPlannedLayoutSiteRecord& OutRecord);

	/** Returns one planned record by stable key. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	bool TryGetPlannedLayoutSiteRecord(const FString& StableRecordKey, FPlannedLayoutSiteRecord& OutRecord) const;

	/** Returns all planned records currently held by the store. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	TArray<FPlannedLayoutSiteRecord> GetPlannedLayoutSiteRecords() const;

	/** Borrows a game-thread record until the next store mutation; lets scheduling inspect metadata without copying payloads. */
	const FPlannedLayoutSiteRecord* FindPlannedLayoutSiteRecord(const FString& StableRecordKey) const
	{
		return PlannedSiteRecordsByKey.Find(StableRecordKey);
	}

	/** Evicts unplaced work after its runtime owner fences publication. Placed protection is never removed here. */
	bool RemoveUnrealizedPlannedLayoutSiteRecord(const FString& StableRecordKey)
	{
		const FPlannedLayoutSiteRecord* Record = PlannedSiteRecordsByKey.Find(StableRecordKey);
		return Record && Record->State != EPlannedLayoutSiteState::Realized && PlannedSiteRecordsByKey.Remove(StableRecordKey) > 0;
	}

	/** Summarizes current root lifecycle counts and one failure without copying cached solve payloads. */
	FString BuildDebugStatusSummary() const;

	/** Imports saved planned records into this store, optionally replacing any existing runtime records first. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	int32 ImportPlannedLayoutSiteRecords(const TArray<FPlannedLayoutSiteRecord>& InRecords, bool bReplaceExisting);

	/** Returns planned records with the requested state. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	TArray<FPlannedLayoutSiteRecord> GetPlannedLayoutSiteRecordsByState(EPlannedLayoutSiteState State) const;

	/** Returns accepted or realized planned records whose solved footprints overlap the supplied inclusive block bounds. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	TArray<FPlannedLayoutSiteRecord> GetAcceptedPlannedLayoutSiteRecordsOverlappingBlockBounds(
		FIntPoint MinBlockXY,
		FIntPoint MaxBlockXY,
		FIntVector CellSizeInBlocks) const;

	/** Marks a pending or accepted planned record as accepted and optionally updates its solve result. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	bool AcceptPlannedLayoutSiteRecord(const FString& StableRecordKey, const FLayoutSolveResult& SolveResult);

	/** Marks a pending or accepted planned record as accepted while preserving one frozen terrain contract for later realization replay. */
	bool AcceptPlannedLayoutSiteRecordWithFrozenTerrainContract(
		const FString& StableRecordKey,
		const FLayoutSolveResult& SolveResult,
		const FLayoutFrozenTerrainContract& FrozenTerrainContract);

	/** Accepts the consumed merged solve and frozen authority. Regional artifact placement totals are not a checksum for this payload. */
	bool AcceptPlannedLayoutSiteRecordWithAcceptedArtifacts(
		const FString& StableRecordKey,
		const FLayoutSolveResult& SolveResult,
		const FLayoutFrozenTerrainContract& FrozenTerrainContract,
		FLayoutId SolvedArtifactId);

	/** Marks a non-realized planned record as rejected and optionally preserves a structured terrain-fit diagnostic outcome. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	bool RejectPlannedLayoutSiteRecord(
		const FString& StableRecordKey,
		const FString& RejectionReason,
		ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind = ELayoutWorldBindingTerrainFitDiagnosticKind::None);

	/** Updates the structured terrain-fit or continuation-endpoint diagnostic preserved on an existing planned record without mutating its lifecycle state. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	bool UpdatePlannedLayoutSiteRecordTerrainFitDiagnostic(
		const FString& StableRecordKey,
		ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind);

	/** Updates transient frozen-submission state on an existing planned record. */
	bool UpdatePlannedLayoutSiteFrozenSubmissionState(
		const FString& StableRecordKey,
		ELayoutPlannedSiteFrozenSubmissionState State,
		FLayoutId DescriptorId = NAME_None,
		uint64 Generation = 0,
		int32 AttemptIndex = 0,
		int32 AuditHash = 0);

	/** Resets a rejected or tombstoned planned record so scout retry may own one new publishable attempt. */
	bool ResetPlannedLayoutSiteRecordForFrozenSubmissionRetry(const FString& StableRecordKey);

	/** Marks an accepted planned record as realized. Calling this repeatedly is safe. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	bool MarkPlannedLayoutSiteRecordRealized(const FString& StableRecordKey);

	/** Retires realized/rejected metadata outside retained root or bounded failure ownership.
	 * Endpoint/edge pruning runs first. Pending/accepted work and placed geometry are untouched. */
	int32 RemoveSettledRecordsWithoutInfluence(const TSet<FString>& RetainedRootKeys);

	/** Adds one endpoint availability record or returns an equivalent existing record unchanged. */
	bool UpsertContinuationEndpointRecord(
		const FLayoutPlanningWindowEndpointRecord& InRecord,
		FLayoutPlanningWindowEndpointRecord& OutRecord);

	/** Borrows edge ownership for immediate game-thread handoff; invalidated by subsequent ledger mutations. */
	const FLayoutPlanningWindowContinuationEdgeRecord* FindContinuationEdgeRecord(const FString& EdgeKey) const
	{
		return ContinuationEdgeRecordsByKey.Find(EdgeKey);
	}

	/** Changes only when endpoint discovery inputs change, not on failed reservation release. */
	uint64 GetContinuationEndpointRevision() const { return ContinuationEndpointRevision; }

	/** Returns a stable snapshot of all retained continuation endpoint records. */
	TArray<FLayoutPlanningWindowEndpointRecord> GetContinuationEndpointRecords() const;

	/** Resolves stable endpoint-pair identity from two exported endpoint snapshots and one selected family/candidate. */
	bool TryMakeContinuationEdgeKey(
		const FResolvedLayoutConnectorEndpoint& Start,
		const FResolvedLayoutConnectorEndpoint& End,
		FName ContinuationFamilyId,
		FName ContinuationFamilyCandidateId,
		FString& OutEdgeKey,
		FString& OutFailureReason) const;

	/** Counts routes once per root/family. Committed-only counting proves terminal capacity; reservations cannot. */
	int32 CountContinuationRootConnections(const FString& RootRecordKey, FName ContinuationFamilyId, bool bCommittedOnly = false) const;

	/** Atomically reserves entry/root capacity and root-pair uniqueness; explicit retry may reopen failed attempts only. */
	bool ReserveContinuationEndpointPair(
		const FResolvedLayoutConnectorEndpoint& Start,
		const FResolvedLayoutConnectorEndpoint& End,
		FName ContinuationFamilyId,
		FName ContinuationFamilyCandidateId,
		FString& OutEdgeKey,
		FString& OutFailureReason,
		bool bExplicitRetry = false);

	/** Releases capacity; failures retain bounded no-repeat history, cancellations do not charge attempts. */
	void ReleaseContinuationEndpointPair(const FString& EdgeKey, bool bFailed = false);

	/** Forgets failed-pair attempts after an input revision changes; preserves reserved/committed capacity and consumed-entry protection. */
	void InvalidateContinuationFailureHistory();

	/** Consumes one capacity on each endpoint after one connector commits. Safe to replay. */
	bool ConsumeContinuationEndpointPair(const FString& EdgeKey);

	/** Removes endpoints and edges outside all inclusive retention windows (all when empty). Preserves consumed-capacity protection; mismatched arrays do nothing. */
	int32 RemoveContinuationEndpointRecordsOutsideWindows(
		const TArray<FIntPoint>& MinBlockXYs,
		const TArray<FIntPoint>& MaxBlockXYs);

	/** Removes every retained endpoint owned by one pruned or rejected root. */
	void RemoveContinuationEndpointRecordsForRoot(const FString& RootRecordKey);

private:
	uint64 ContinuationEndpointRevision = 1;

	/** Configured settings for finite planning-window updates. */
	UPROPERTY()
	FLayoutPlanningWindowSettings Settings;

	/** Planned site records keyed by stable record identity. */
	UPROPERTY()
	TMap<FString, FPlannedLayoutSiteRecord> PlannedSiteRecordsByKey;

	/** Compact endpoint availability keyed by stable root-entry-family identity. */
	UPROPERTY()
	TMap<FString, FLayoutPlanningWindowEndpointRecord> ContinuationEndpointRecordsByKey;

	/** Pending and committed continuation edges keyed by stable unordered pair identity. */
	UPROPERTY()
	TMap<FString, FLayoutPlanningWindowContinuationEdgeRecord> ContinuationEdgeRecordsByKey;

	/** Final-capacity endpoint tombstones prevent replayed root publication from restoring consumed availability. */
	UPROPERTY()
	TSet<FString> ConsumedContinuationEndpointKeys;
};
