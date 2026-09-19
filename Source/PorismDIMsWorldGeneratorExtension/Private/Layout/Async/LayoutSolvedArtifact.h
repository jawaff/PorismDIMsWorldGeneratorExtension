// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Contracts/LayoutContractTypes.h"
#include "Layout/Solver/LayoutProfileSolver.h"

/** Pointer-free frozen metadata for one occupied local cell in a solved placement bundle. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSolvedArtifactOccupiedCell
{
	/** Occupied local coordinate inside the root-owned bundle. */
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Leaf template resolved by the immutable module snapshot. */
	FSoftObjectPath TemplatePath;

	/** Leaf yaw relative to the root placement yaw. */
	int32 RelativeYawRotationSteps = 0;

	/** Roles contributed by this exact local cell. */
	TArray<ELayoutModuleRole> Roles;

	/** Planned intents this exact local cell may satisfy. */
	TArray<ELayoutCellIntent> SupportedCellIntents;

	/** Exposed faces retained after internal composite glue suppression. */
	TArray<FLayoutFaceRule> ExposedFaceRules;
};

/** Pointer-free solved placement copied from worker proof output before realization-prep writes are derived. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSolvedArtifactPlacement
{
	/** Region path that owns this solved placement. */
	FString RegionDebugPath;

	/** Parent- or root-relative region offset that scopes this solved placement. */
	FIntVector RegionCellOffset = FIntVector::ZeroValue;

	/** Solved local cell selected by the proof worker. */
	FIntVector Cell = FIntVector::ZeroValue;

	/** Broad planner intent solved for this cell. */
	ELayoutCellIntent Intent = ELayoutCellIntent::Interior;

	/** Content-entry id that selected this placement, when known. */
	FName SourceContentEntryId;

	/** Snapshot id that selected this placement without a live module asset. */
	FLayoutId ModuleSnapshotId;

	/** Soft template path preserved for later game-thread realization rehydration. */
	FSoftObjectPath TemplatePath;

	/** Clockwise yaw rotation chosen by the solve. */
	int32 YawRotationSteps = 0;

	/** Snapshot-backed occupied cells for this rigid placement bundle. */
	TArray<FIntVector> OccupiedLocalCells;

	/** Complete pointer-free local-cell descriptors required by artifact audit and realization. */
	TArray<FLayoutSolvedArtifactOccupiedCell> OccupiedCellDescriptors;
};

/** Lifecycle state for a pointer-free solved artifact before realization/write-plan consumption. */
enum class ELayoutSolvedArtifactStatus : uint8
{
	PendingChildren,
	SolvedCellsComplete,
	WritePlanReady,
	AcceptedComplete,
	Rejected,
	Canceled
};

/** Worker-produced solved artifact containing solved cells/templates/diagnostics only, never write operations. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSolvedArtifact
{
	/** True only after a worker result was copied into this pointer-free artifact. */
	bool bHasProducedArtifact = false;

	/** Lifecycle status used to fail closed before realization/write-plan consumption. */
	ELayoutSolvedArtifactStatus Status = ELayoutSolvedArtifactStatus::Rejected;

	/** Stable artifact id for diagnostics and replay. */
	FLayoutId ArtifactId;

	/** Region path copied from the solved result. */
	FString RegionDebugPath;

	/** Cell offset copied from the solved result. */
	FIntVector RegionCellOffset = FIntVector::ZeroValue;

	/** Solve seed copied from the worker result. */
	int32 Seed = 0;

	/** Footprint size copied from the worker result. */
	FIntPoint FootprintSize = FIntPoint::ZeroValue;

	/** Shared cell size copied from the worker result. */
	FIntVector SharedCellSizeInBlocks = FIntVector::ZeroValue;

	/** Solved placements copied without live asset pointers. */
	TArray<FLayoutSolvedArtifactPlacement> Placements;

	/** Exact hard zone-feature provider commitments copied from solve output. */
	TArray<FLayoutZoneFeatureProviderCommitment> ZoneFeatureProviderCommitments;

	/** Exact pointer-free child stage mappings copied from the accepted schedule. */
	TArray<FLayoutChildStageMappingResult> ChildStageMappings;

	/** Candidate-domain certificates consumed by accepted regional proofs. */
	TArray<FLayoutId> CandidateDomainCertificateIds;

	/** Exact committed owner/passive seam records copied from solve output. */
	TArray<FLayoutPartitionSeamRecord> PartitionSeams;

	/** Exact owner-side junction requirements derived once from committed seam runs. */
	TArray<FLayoutOwnedSeamJunctionRequirement> JunctionRequirements;

	/** True only when provider, seam, and junction commitments came from an accepted successful solve. */
	bool bStructuralCommitmentsAuthoritative = false;

	/** True when active-cell provenance was attached before realization/write-plan consumption. */
	bool bHasActiveCellProvenance = false;

	/** Kind-qualified active cells copied from the frozen terrain/contract boundary. */
	TArray<FLayoutContractActiveCellRecord> ActiveCells;

	/** Structured first-cause regional failure retained by rejected partial artifacts. */
	FLayoutRegionalFailureRecord RegionalFailure;

	/** Public diagnostics/proofs copied from the solved result for later publication. */
	TArray<FLayoutValidationAssertionRecord> ValidationAssertions;

	/** Footprint min block world position used for coordinate translation by continuation scouts. */
	FIntVector FootprintMinBlockWorldPos = FIntVector::ZeroValue;

	/** Region-local entry cell exposed to the outside, intended for continuation connection. */
	FIntVector ExposedEntryCell = FIntVector::ZeroValue;

	/** Face direction of the exposed entry cell that points outside the footprint. */
	ELayoutFaceDirection ExposedEntryFaceDirection = ELayoutFaceDirection::PosX;
};

/** Explicit-only builder for solved artifacts; rejects live carriers and never derives realization/write batches. */
namespace LayoutSolvedArtifact
{
	/** Builds a pointer-free solved artifact from one successful region solve result. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildFromSolveResult(
		FLayoutId ArtifactId,
		const FLayoutRegionSolveResult& SolveResult,
		FLayoutSolvedArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Builds a pointer-free solved artifact from one successful schedule result without deriving write operations. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildFromScheduleResult(
		FLayoutId ArtifactId,
		const FString& RegionDebugPathFallback,
		const FLayoutRegionSolveScheduleResult& ScheduleResult,
		FLayoutSolvedArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Builds an artifact from retained placements after a full schedule rejection. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildFromPartialScheduleResult(
		FLayoutId ArtifactId,
		const FString& RegionDebugPathFallback,
		const FLayoutRegionSolveScheduleResult& ScheduleResult,
		FLayoutSolvedArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Attaches validated active-cell provenance before realization-prep or write-plan consumers use the artifact. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryAttachActiveCellProvenance(
		const TArray<FLayoutContractActiveCellRecord>& ActiveCells,
		FLayoutSolvedArtifact& InOutArtifact,
		FString& OutFailureReason);

	/** Validates that write-plan or realization-prep inputs preserve kind-qualified active-cell provenance. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateActiveCellProvenanceForWriteInputs(
		const FLayoutSolvedArtifact& Artifact,
		FString& OutFailureReason);
}
