// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "LayoutTypes.generated.h"

class UChunkStructureTemplate;
class ULayoutCompositeModuleAsset;
class ULayoutModuleAsset;
class UObject;
class ULayoutProfileAsset;
class ULayoutRegionContentSetAsset;

/** Describes the severity of one validation message emitted by layout assets or solve helpers. */
UENUM(BlueprintType)
enum class ELayoutValidationSeverity : uint8
{
	Warning,
	Error
};

/** High-level categories for proof-bearing derived-contract records. */
UENUM(BlueprintType)
enum class ELayoutProofKind : uint8
{
	SnapshotCopy,
	DerivedContract,
	DerivedOffer,
	DerivedSpan,
	DerivedVerticalAccess,
	ChildCapability,
	NormalizedCommitment
};

/** High-level categories for fail-fast assertion records. */
UENUM(BlueprintType)
enum class ELayoutValidationAssertionKind : uint8
{
	AssetValidationPassed,
	SnapshotSourcePresent,
	SnapshotContractInitialized,
	RequestContractValid,
	DerivedOfferContractValid,
	DerivedSpanContractValid,
	DerivedVerticalAccessContractValid,
	ChildCapabilityContractValid,
	ChildCommitmentContractValid,
	SnapshotSharedCellSizeContractValid
};

/** Describes one face of the fixed-size layout cell cube. */
UENUM(BlueprintType)
enum class ELayoutFaceDirection : uint8
{
	PosX UMETA(ToolTip = "The authored positive X side of the module cell. Square XY modules may rotate this side onto another horizontal world side during solving."),
	NegX UMETA(ToolTip = "The authored negative X side of the module cell. Square XY modules may rotate this side onto another horizontal world side during solving."),
	PosY UMETA(ToolTip = "The authored positive Y side of the module cell. Square XY modules may rotate this side onto another horizontal world side during solving."),
	NegY UMETA(ToolTip = "The authored negative Y side of the module cell. Square XY modules may rotate this side onto another horizontal world side during solving."),
	PosZ UMETA(ToolTip = "The authored top side of the module cell. Yaw rotation does not change this direction."),
	NegZ UMETA(ToolTip = "The authored bottom side of the module cell. Yaw rotation does not change this direction.")
};

/** Broad region intent assigned by the planner before the module solve begins. */
UENUM(BlueprintType)
	enum class ELayoutCellIntent : uint8
	{
		Boundary UMETA(ToolTip = "Planner-assigned perimeter intent. These cells are part of the solved footprint boundary and should be matched by content that is legal on the outer band."),
		Entry UMETA(ToolTip = "Planner-assigned required entry intent. These cells reserve explicit entry-facing positions that entry-restricted content and downstream continuation logic must satisfy."),
		Core UMETA(ToolTip = "Planner-assigned central intent. These cells mark the preferred base-level core area of the solved footprint for content that targets the center."),
		Interior UMETA(ToolTip = "Planner-assigned non-boundary fill intent. These cells are inside the solved footprint and are not reserved as boundary, entry, connector, or vertical-access positions."),
		Connector UMETA(ToolTip = "Planner-assigned route or corridor intent. These cells belong to a planned connective path and should be matched by content that is legal on route-constrained space."),
		VerticalAccess UMETA(ToolTip = "Planner-assigned vertical-transition intent. These cells reserve positions that must support legal traversal between levels.")
	};

/** Establishes which system owns a planned Entry cell. */
UENUM(BlueprintType)
enum class ELayoutEntryOrigin : uint8
{
	None UMETA(ToolTip = "Cell has no Entry ownership."),
	AuthoredBoundary UMETA(ToolTip = "Entry selected from the direct root's physical boundary."),
	TerrainSeam UMETA(ToolTip = "Entry selected for a stepped-terrain seam deck."),
	ChildContract UMETA(ToolTip = "Entry required by a child-region contract."),
	Continuation UMETA(ToolTip = "Entry required by a continuation contract.")
};

/** Static terrain quality tier for one VerticalAccess host pair. */
UENUM(BlueprintType)
enum class ELayoutVerticalAccessHostTier : uint8
{
	CleanInterior UMETA(ToolTip = "Both endpoints are clean Interior terrain."),
	Constrained UMETA(ToolTip = "Endpoints require explicit terrain-contact support.")
};

/** One exact normal-zone candidate that can satisfy a local VerticalAccess support face. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutVerticalAccessSupportCandidate
{
	/** Normal-zone cell constrained by this conditional support offer. */
	FIntVector Cell = FIntVector::ZeroValue;

	/** Owning bundle root; differs from Cell when another demanded host supplies a composite shadow. */
	FIntVector RootCell = FIntVector::ZeroValue;

	/** Exact supporting module snapshot. */
	FLayoutId ModuleSnapshotId;

	/** Exact supporting module index. */
	int32 ModuleSnapshotIndex = INDEX_NONE;

	/** Exact supporting module yaw. */
	int32 YawRotationSteps = 0;
};

/** One frozen lower/upper host alternative for a physical VerticalAccess requirement. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutVerticalAccessHostOption
{
	GENERATED_BODY()

	/** Physical lower cell carrying VerticalAccess intent; generated wall transitions may root on a bridge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Physical lower cell carrying VerticalAccess intent. Generated wall transitions may root on a bridge; exact module admission retains authored level and face restrictions."))
	FIntVector LowerCell = FIntVector::ZeroValue;

	/** Direct upper landing paired with LowerCell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Direct upper landing paired with LowerCell."))
	FIntVector UpperCell = FIntVector::ZeroValue;

	/** Terrain quality tier used for deterministic alternative ordering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Terrain quality tier used for deterministic alternative ordering."))
	ELayoutVerticalAccessHostTier Tier = ELayoutVerticalAccessHostTier::CleanInterior;

	/** Seeded stable ordering key. */
	uint32 SeedRank = 0;

	/** True when prewarm retained exact module/yaw/footprint/port evidence for this host. */
	bool bHasExactCandidateWitness = false;

	/** Exact root module snapshot selected by local host admission. */
	FLayoutId LowerModuleSnapshotId;

	/** Exact root module index selected by local host admission. */
	int32 LowerModuleSnapshotIndex = INDEX_NONE;

	/** Exact root yaw selected by local host admission. */
	int32 LowerYawRotationSteps = 0;

	/** Exact landing module snapshot, or the root snapshot when one composite owns both cells. */
	FLayoutId UpperModuleSnapshotId;

	/** Exact landing module index, or the root module index for a composite-owned landing. */
	int32 UpperModuleSnapshotIndex = INDEX_NONE;

	/** Exact landing yaw selected by local host admission. */
	int32 UpperYawRotationSteps = 0;

	/** Candidate-local cell that realizes UpperCell; zero identifies a separate landing root. */
	FIntVector UpperCandidateLocalCell = FIntVector::ZeroValue;

	/** Full occupied footprint of the admitted root/landing bundle. */
	TArray<FIntVector> OccupiedCells;

	/** External cells that must resolve to compatible filled support for this exact bundle. */
	TArray<FIntVector> RequiredFilledSupportCells;

	/** Correlated normal-zone module/yaw domains that can satisfy required filled support cells. */
	TArray<FLayoutVerticalAccessSupportCandidate> FilledSupportCandidates;

	/** External cells that must remain empty for this exact bundle. */
	TArray<FIntVector> RequiredEmptyClearanceCells;

	/** Horizontal root faces whose adjacent domains can consume this candidate's traversal channel. */
	uint8 LowerTraversalPortFaceMask = 0;

	/** Horizontal landing faces whose adjacent domains can consume this candidate's traversal channel.
	 * Preserving profiles retain one selected face to an adjacent normal destination; its compatible
	 * module/yaw domain lives in FilledSupportCandidates and must survive host refresh and projection.
	 */
	uint8 UpperTraversalPortFaceMask = 0;
};

/** Frozen alternatives for one authored count slot or generated-deck ascent offer. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutVerticalAccessHostGroup
{
	GENERATED_BODY()

	/** Stable ascent requirement identifier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable ascent requirement identifier."))
	FLayoutId GroupId;

	/** Planner-owned Range surplus, optional ascent, or replaceable wall-transition offer.
	 * A wall offer may be omitted only if joint CSP proves the wall through other selected structure.
	 * Mandatory authored minimum/Exact slots and required access coverage never permit omission.
	 * Omission uses Options.Num() as its selection index and never becomes a placed witness. */
	UPROPERTY()
	bool bAllowOmission = false;

	/** Generated ground/wall obligation, not an authored count slot. Count recovery must
	 * retain these alternatives even when it omits every authored Range surplus slot. */
	UPROPERTY()
	bool bIsSupplemental = false;

	/** Planner preference for surplus above the seeded Range target; not an acceptance rule. */
	UPROPERTY()
	bool bPreferOmission = false;

	/** Upper deck cell represented by this ascent requirement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Upper deck cell represented by this ascent requirement."))
	FIntVector DeckCell = FIntVector::ZeroValue;

	/** Module-admitted host alternatives in deterministic order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Module-admitted host alternatives in deterministic order."))
	TArray<FLayoutVerticalAccessHostOption> Options;

	/** First static module or face rejection observed while admitting this group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "First static module or face rejection observed while admitting this group."))
	FString FirstRejectedAdmission;
};

/** Fixed module behavior roles that determine which planned cells a module may satisfy. */
UENUM(BlueprintType)
	enum class ELayoutModuleRole : uint8
	{
		Boundary UMETA(ToolTip = "Module is eligible for cells planned with Boundary intent. This role does not bypass face-rule, route, closure, or seam validation."),
		Entry UMETA(ToolTip = "Module is eligible for cells planned with Entry intent. Entry modules may also satisfy internally planned route cells when their face rules and traversal contracts make that legal."),
		Interior UMETA(ToolTip = "Module is eligible for cells planned with Interior intent. This role marks ordinary non-boundary fill compatibility."),
		VerticalAccess UMETA(ToolTip = "Module is eligible for cells planned with VerticalAccess intent. Use this when the module can satisfy required traversal between levels.")
	};

/** Parent-relative placement/scoring zone used by recursive region contracts. */
UENUM(BlueprintType)
enum class ELayoutPlacementZone : uint8
{
	Any UMETA(ToolTip = "No placement-zone preference. This contract can match any valid zone."),
	Perimeter UMETA(ToolTip = "Placement should happen in the parent perimeter band or closure loop."),
	Edge UMETA(ToolTip = "Placement should happen along a non-corner parent edge when possible."),
	Corner UMETA(ToolTip = "Placement should happen at or near a generated parent corner anchor."),
	Interior UMETA(ToolTip = "Placement should happen away from the outer perimeter inside the parent footprint."),
	Core UMETA(ToolTip = "Placement should happen near the parent core or central anchor.")
};

/** Returns one bit for a layout face direction. */
inline constexpr uint8 LayoutFaceDirectionMask(const ELayoutFaceDirection Direction)
{
	return static_cast<uint8>(1u << static_cast<uint8>(Direction));
}

/** Returns whether a face mask contains the supplied direction. */
inline constexpr bool LayoutFaceMaskContainsDirection(const uint8 FaceMask, const ELayoutFaceDirection Direction)
{
	return (FaceMask & LayoutFaceDirectionMask(Direction)) != 0;
}

/** Resolves local placement zone from horizontal exterior or terrain-seam faces. */
inline ELayoutPlacementZone ResolveLayoutPlacementZoneFromLateralFaceMask(const uint8 FaceMask)
{
	const uint8 HorizontalMask = LayoutFaceDirectionMask(ELayoutFaceDirection::PosX)
		| LayoutFaceDirectionMask(ELayoutFaceDirection::NegX)
		| LayoutFaceDirectionMask(ELayoutFaceDirection::PosY)
		| LayoutFaceDirectionMask(ELayoutFaceDirection::NegY);
	const int32 FaceCount = FMath::CountBits(FaceMask & HorizontalMask);
	return FaceCount >= 2
		? ELayoutPlacementZone::Corner
		: FaceCount == 1
			? ELayoutPlacementZone::Edge
			: ELayoutPlacementZone::Interior;
}

/** Parent-relative level-placement policy used by recursive region scheduling and module realization. */
UENUM(BlueprintType)
enum class ELayoutLevelPlacementPolicy : uint8
{
	AnyLevel UMETA(ToolTip = "No level-placement restriction. This content may appear on any supported level."),
	GroundOnly UMETA(ToolTip = "This content may only appear on ground level Z=0."),
	SpecificLevel UMETA(ToolTip = "This content may only appear on the authored SpecificLevel index."),
	TopLevelOnly UMETA(ToolTip = "This content may only appear on the locally highest valid level for the relevant X/Y support column."),
	AboveGroundLevel UMETA(ToolTip = "This content may only appear on supported levels above ground, where Z is greater than zero."),
	BelowTopLevel UMETA(ToolTip = "This content may only appear on supported levels strictly below the locally highest valid level for the relevant X/Y support column.")
};

/** Discriminates whether one content-set entry references a module or a child region profile. */
UENUM(BlueprintType)
enum class ELayoutRegionContentKind : uint8
{
	Module UMETA(ToolTip = "This content entry references one module asset."),
	ChildRegion UMETA(ToolTip = "This content entry references one child region profile.")
};

/** Authoring mode used to define a local bounds policy. */
UENUM(BlueprintType)
enum class ELayoutBoundsPolicyMode : uint8
{
	SolvedFootprint UMETA(ToolTip = "Derive bounds from the early committed footprint selected for this region or child instance, then clamp them with InsetCells and MinLevel/MaxLevel. Use this when the closure should follow the committed footprint choice instead of explicit local coordinates."),
	ExplicitLocalBounds UMETA(ToolTip = "Use explicit local MinCells/MaxCells bounds and optional FootprintMask cells authored in region-local coordinates. In this mode the Z values on MinCells/MaxCells already define the vertical extent.")
};

/** Count constraint mode used by profile-authored entry and vertical-access reservations. */
UENUM(BlueprintType)
enum class ELayoutCountConstraintMode : uint8
{
	None UMETA(ToolTip = "Do not reserve any cells for this feature."),
	Exact UMETA(ToolTip = "Reserve exactly one authored count for this feature."),
	Range UMETA(ToolTip = "Reserve a deterministic seeded count between the authored minimum and maximum values.")
};

/** Selects whether a sparse content rule preserves supported terrain or consumes only residuals produced elsewhere. */
UENUM(BlueprintType)
enum class ELayoutSparseCandidateSource : uint8
{
	PreserveSupportedTerrain UMETA(ToolTip = "Register matching supported terrain as claimable topology before structural proof, then place sparse content into this rule's unclaimed cells."),
	ExistingResiduals UMETA(ToolTip = "Do not preserve terrain. Consume only matching residual cells already produced by optional-child drop or ordinary post-structural handoff.")
};

/** Explicit fill behavior applied to authored level scopes before normal module solving begins. */
UENUM(BlueprintType)
enum class ELayoutLevelFillMode : uint8
{
	Default UMETA(ToolTip = "Use the profile's ordinary fill behavior on matching levels."),
	BoundaryOnly UMETA(ToolTip = "Only keep boundary, entry, and required vertical-access continuation cells planned on matching levels.")
};

/** How one zone feature requirement matches provided feature tags on content entries. */
UENUM(BlueprintType)
enum class ELayoutZoneFeatureMatchMode : uint8
{
	Any UMETA(ToolTip = "A placed content entry counts when it provides any feature tag listed on the requirement."),
	All UMETA(ToolTip = "A placed content entry counts only when it provides every feature tag listed on the requirement.")
};

/** Occupancy contract for one face so the solver can tell whether a neighbor is required. */
UENUM(BlueprintType)
enum class ELayoutFaceOccupancyPolicy : uint8
{
	RequiresFilledNeighbor = 0 UMETA(DisplayName = "Requires Filled Neighbor", ToolTip = "This face must touch another filled module cell, and both faces must allow each other's face tags. Module-set validation also checks that at least one compatible partner exists. Traversal-channel links are optional for this policy."),
	AllowsEmptyOrFilledNeighbor = 1 UMETA(DisplayName = "Allows Empty Or Filled Neighbor", ToolTip = "This face may touch outside or an intentionally empty cell. If the neighbor is filled, both faces must allow each other's face tags. Traversal-channel links are optional for this policy."),
	RequiresEmptyNeighbor = 2 UMETA(DisplayName = "Requires Empty Neighbor", ToolTip = "This face is intended to point at outside or an intentionally empty cell, such as an exterior opening. If the neighbor cell is planned, that intent must be allowed to remain empty."),
	AllowsAnyNeighbor = 3 UMETA(DisplayName = "Allows Any Neighbor", ToolTip = "Wildcard policy. This face accepts empty space or any filled neighbor and skips face-tag compatibility checks. Use only when this face truly does not care what is beside it."),
	RequiresWalkableFilledNeighbor = 4 UMETA(DisplayName = "Requires Traversable Filled Neighbor", ToolTip = "This face must touch another filled module cell, both faces must allow each other's face tags, and the two faces must share at least one connected traversal channel."),
	AllowsEmptyOrWalkableFilledNeighbor = 5 UMETA(DisplayName = "Allows Empty Or Traversable Filled Neighbor", ToolTip = "This face may touch outside or an intentionally empty cell. If the neighbor is filled, both faces must allow each other's face tags and share at least one connected traversal channel.")
};

/** Authoring-time face symmetry used to infer non-authoritative face rules. */
UENUM(BlueprintType)
enum class ELayoutFaceSymmetryMode : uint8
{
	Independent UMETA(ToolTip = "Every face rule is authored independently."),
	MirrorXFromPosX UMETA(DisplayName = "Mirror X From PosX", ToolTip = "The NegX face rule is inferred from PosX. Use when both X sides should expose the same face behavior."),
	MirrorYFromPosY UMETA(DisplayName = "Mirror Y From PosY", ToolTip = "The NegY face rule is inferred from PosY. Use when both Y sides should expose the same face behavior."),
	MirrorXYFromPositive UMETA(DisplayName = "Mirror X/Y From Positive", ToolTip = "NegX is inferred from PosX and NegY is inferred from PosY. Use when each horizontal axis is symmetric but X and Y may differ."),
	RadialHorizontalFromPosX UMETA(DisplayName = "Radial Horizontal From PosX", ToolTip = "All horizontal faces are inferred from PosX. Use for modules whose horizontal faces are completely equivalent.")
};

/** Controls optional diagnostic tracing for the layout solver. */
UENUM(BlueprintType)
enum class ELayoutSolverTraceMode : uint8
{
	Disabled UMETA(ToolTip = "Do not record solver decision trace events. This is the default for runtime performance."),
	OnFailure UMETA(ToolTip = "Record a bounded in-memory solver trace and publish it only when the solve fails."),
	Always UMETA(ToolTip = "Record and publish a bounded solver trace for both successful and failed solves. Use for editor diagnostics only.")
};

/** Identifies why one residual cell survived the structural pass. */
UENUM(BlueprintType)
enum class ELayoutResidualCellSource : uint8
{
	UnoccupiedPlannedCell UMETA(ToolTip = "The structural pass left this planned cell empty after normal placement selection finished."),
	DroppedOptionalChild UMETA(ToolTip = "This planned cell belonged to a child region that was deterministically dropped during the optional-child pass."),
	TerrainBackedRule UMETA(ToolTip = "A profile terrain-residual rule intentionally kept this supported cell empty and write-free after structural claims settled.")
};

/** Severity-tagged layout validation message. */
USTRUCT(BlueprintType)
struct FLayoutValidationMessage
{
	GENERATED_BODY()

	/** Severity of this emitted validation message. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Severity of this emitted validation message."))
	ELayoutValidationSeverity Severity = ELayoutValidationSeverity::Error;

	/** Human-readable validation message. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Human-readable validation message."))
	FString Message;
};

/** Aggregated validation result used by layout assets and solve helpers. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutValidationResult
{
	GENERATED_BODY()

	/** Collected validation messages. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Collected validation messages."))
	TArray<FLayoutValidationMessage> Messages;

	/** Appends one warning message to this result. */
	void AddWarning(const FString& InMessage);

	/** Appends one error message to this result. */
	void AddError(const FString& InMessage);

	/** Returns true when the result contains no errors. */
	bool IsValid() const;

	/** Returns true when at least one warning was emitted. */
	bool HasWarnings() const;
};

/** Compact proof source for one normalized snapshot or derived-contract claim. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProofRecord
{
	GENERATED_BODY()

	/** Stable proof identifier used by fixtures and diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable proof identifier used by fixtures and diagnostics."))
	FLayoutId ProofId;

	/** High-level category of the proof-bearing claim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "High-level category of the proof-bearing claim."))
	ELayoutProofKind ProofKind = ELayoutProofKind::SnapshotCopy;

	/** Stable id for the thing this proof validates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable id for the thing this proof validates."))
	FLayoutId TargetId;

	/** Stable ids of normalized evidence used to justify the target claim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable ids of normalized evidence used to justify the target claim."))
	TArray<FLayoutId> SourceIds;

	/** Human-readable summary of why this claim is valid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Human-readable summary of why this claim is valid."))
	FString ProofSummary;
};

/** One fail-fast assertion result captured during validation or snapshot setup. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutValidationAssertionRecord
{
	GENERATED_BODY()

	/** Stable assertion identifier used by fixtures and diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable assertion identifier used by fixtures and diagnostics."))
	FLayoutId AssertionId;

	/** Invariant or shortcut category being checked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Invariant or shortcut category being checked."))
	ELayoutValidationAssertionKind AssertionKind = ELayoutValidationAssertionKind::AssetValidationPassed;

	/** Whether the assertion passed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Whether the assertion passed."))
	bool bPassed = true;

	/** Stable ids cited by this assertion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable ids cited by this assertion."))
	TArray<FLayoutId> RelatedIds;

	/** Failure detail when the assertion does not pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Failure detail when the assertion does not pass."))
	FString FailureReason;
};

/** Local area/band policy used by explicit recursive authoring contracts. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutBoundsPolicy
{
	GENERATED_BODY()

	/** Authoring mode used to interpret this bounds policy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Authoring mode used to interpret this bounds policy. SolvedFootprint derives from the early committed footprint chosen for the region or child instance. ExplicitLocalBounds uses exact local Min/Max coordinates and optional mask cells."))
	ELayoutBoundsPolicyMode Mode = ELayoutBoundsPolicyMode::SolvedFootprint;

	/** Minimum local cell included when Mode is ExplicitLocalBounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (EditCondition = "Mode == ELayoutBoundsPolicyMode::ExplicitLocalBounds", EditConditionHides, ToolTip = "Minimum local cell included when Mode is ExplicitLocalBounds. This is inclusive and authored in region-local cell coordinates. The Z value is the lowest included level in this mode."))
	FIntVector MinCells = FIntVector::ZeroValue;

	/** Maximum local cell bound when Mode is ExplicitLocalBounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (EditCondition = "Mode == ELayoutBoundsPolicyMode::ExplicitLocalBounds", EditConditionHides, ToolTip = "Maximum local cell bound when Mode is ExplicitLocalBounds. Treat this as an exclusive upper bound in region-local cell coordinates. The Z value is the exclusive upper level bound in this mode."))
	FIntVector MaxCells = FIntVector::ZeroValue;

	/** Optional explicit occupied-mask cells inside explicit local bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (EditCondition = "Mode == ELayoutBoundsPolicyMode::ExplicitLocalBounds", EditConditionHides, ToolTip = "Optional occupied local cells inside the explicit bounds. Leave empty to use the full explicit rectangle/prism."))
	TArray<FIntVector> FootprintMask;

	/** Cells to inset from the chosen solved footprint when Mode is SolvedFootprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (EditCondition = "Mode == ELayoutBoundsPolicyMode::SolvedFootprint", EditConditionHides, ClampMin = "0", UIMin = "0", ToolTip = "How many cells inward from the footprint edge to shift the derived bounds. Use zero for the outer perimeter band."))
	int32 InsetCells = 0;

	/** Lowest inclusive Z level used when Mode is SolvedFootprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (EditCondition = "Mode == ELayoutBoundsPolicyMode::SolvedFootprint", EditConditionHides, ClampMin = "0", UIMin = "0", ToolTip = "Lowest inclusive level covered by the derived bounds."))
	int32 MinLevel = 0;

	/** Highest inclusive Z level used when Mode is SolvedFootprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (EditCondition = "Mode == ELayoutBoundsPolicyMode::SolvedFootprint", EditConditionHides, ClampMin = "0", UIMin = "0", ToolTip = "Highest inclusive level covered by the derived bounds. Use LevelCount - 1 to cover every solved layer."))
	int32 MaxLevel = 0;
};

/** Explicit continuous closure contract authored on recursive-capable profiles. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutClosureRequirement
{
	GENERATED_BODY()

	/** Stable closure identifier used by diagnostics, fixtures, and future span matching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable closure identifier used by diagnostics, fixtures, and future span matching."))
	FName ClosureId;

	/** Parent-relative zone that this closure applies to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Parent-relative zone that this closure applies to. Perimeter is the normal choice for wall, gate, or tower closure authoring."))
	ELayoutPlacementZone Zone = ELayoutPlacementZone::Perimeter;

	/** Area or band that generates this closure requirement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Area or band that generates this closure requirement. SolvedFootprint mode follows the early committed footprint chosen for the region or child instance and is usually the right choice for perimeter closure authoring."))
	FLayoutBoundsPolicy BoundsPolicy;

	/** Minimum accepted thickness provided by later span/interrupter content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Minimum accepted thickness provided by compatible wall, gate, tower, or child span coverage."))
	int32 MinThicknessCells = 1;

};

/** Explicit closure-provider intent used by one content entry, or by the profile-level compatibility bridge when needed. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutClosureProviderIntent
{
	GENERATED_BODY()

	/** Stable provider-intent identifier used by diagnostics, fixtures, and content-entry matching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable provider-intent identifier used by diagnostics, fixtures, and content-entry matching."))
	FName ProviderIntentId;

	/** Parent-relative zone this content is intended to participate in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Parent-relative zone this content is intended to participate in when acting as a closure or interrupter provider."))
	ELayoutPlacementZone Zone = ELayoutPlacementZone::Perimeter;

	/** Optional closure id this content is primarily meant to satisfy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Optional closure id this content is primarily meant to satisfy. Leave empty to participate in any compatible parent closure for the selected boundary zone."))
	FName ClosureId;
};

/** Counted feature requirement evaluated against placed content ownership within one placement zone. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutZoneFeatureRequirement
{
	GENERATED_BODY()

	/** Stable requirement identifier used by diagnostics, fixtures, and deterministic ordering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable requirement identifier used by diagnostics, fixtures, and deterministic ordering."))
	FName RequirementId;

	/** Parent-relative zone where matching placed content must appear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Parent-relative placement zone where matching placed content must appear."))
	ELayoutPlacementZone Zone = ELayoutPlacementZone::Any;

	/** Feature tags that qualifying content entries must provide. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Feature", ToolTip = "Feature tags that qualifying content entries must provide. These are matched against ProvidedZoneFeatures on placed content entries, not against raw module asset identity."))
	FGameplayTagContainer RequiredFeatures;

	/** Whether a placed content entry may satisfy any listed feature or must satisfy all listed features. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Whether a placed content entry may satisfy any listed feature or must satisfy all listed features."))
	ELayoutZoneFeatureMatchMode MatchMode = ELayoutZoneFeatureMatchMode::Any;

	/** Minimum number of matching placed content instances required in the selected zone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Minimum number of matching placed content instances required in the selected zone."))
	int32 MinCount = 1;

	/** Maximum allowed number of matching placed content instances in the selected zone. Zero means no maximum limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Maximum allowed number of matching placed content instances in the selected zone. Set to zero to allow any count above the minimum."))
	int32 MaxCount = 0;
};

/** Selects whether a seam provider may realize ordinary or owner-side junction cells. */
UENUM(BlueprintType)
enum class ELayoutSeamJunctionUsage : uint8
{
	NonJunctionOnly UMETA(ToolTip = "Provider may realize ordinary owned shared-seam cells but not owner-side junction cells."),
	JunctionOnly UMETA(ToolTip = "Provider may realize owner-side junction cells but not ordinary owned shared-seam cells."),
	Both UMETA(ToolTip = "Provider may realize ordinary owned shared-seam cells and owner-side junction cells.")
};

/** Explicit parent-owned seam/provider intent used by unified content entries for shared walls and partitions. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSeamProviderIntent
{
	GENERATED_BODY()

	/** Authored name, unique within its content entry. Generated capability/witness identities live in owned runtime carriers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable authored seam-intent name, unique within this content entry. Used by diagnostics, fixtures, and content-entry matching."))
	FName SeamIntentId;

	/** Whether this provider may realize ordinary seam cells, owner-side junction cells, or both. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Controls whether this seam provider may realize ordinary owned seam cells, owner-side junction cells, or both. Face rules and module PlacementZone still govern geometry and local Edge/Corner eligibility."))
	ELayoutSeamJunctionUsage JunctionUsage = ELayoutSeamJunctionUsage::Both;

	/** Gameplay-tag family that identifies the shared seam or partition behavior this content can support. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Interface", ToolTip = "Gameplay-tag family that identifies the shared seam or partition behavior this content can support, for example Layout.Interface.Partition.Solid or Layout.Interface.Partition.Door."))
	FGameplayTag InterfaceFamily;

	/** If true, this content may be chosen as the owning side of a shared seam span. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "If true, this content may be chosen as the owning side of a shared seam span. Module-backed seam ownership is mainly intended for straight parent-owned perimeter spans, such as perimeter wall or perimeter entry sharing, instead of freeform sibling partition layout."))
	bool bCanOwnSeam = true;

	/** If true, this content may accept a compatible seam owned by a sibling or parent region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "If true, this content may accept a compatible seam owned by a sibling or parent region."))
	bool bCanAcceptSeam = false;
};

/** Module-specific side of one region content entry. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutModuleContentSettings
{
	GENERATED_BODY()

	/** Module asset this content entry can place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (EditCondition = "CompositeModule == nullptr", EditConditionHides, ToolTip = "Leaf module asset this content entry can place. Use this for ordinary single-cell leaf reuse; new multi-cell structure should move to CompositeModule instead of stretching one module entry across multiple cells."))
	TObjectPtr<ULayoutModuleAsset> Module = nullptr;

	/** Optional composite-module asset this content entry can place instead of one leaf module. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (EditCondition = "Module == nullptr", EditConditionHides, ToolTip = "Optional composite-module asset this content entry can place instead of one leaf module. Use this for new multi-cell structure built from reusable leaf modules. Author exactly one source between Module and CompositeModule for each module entry until CompositeModule receives its own durable ContentKind."))
	TObjectPtr<ULayoutCompositeModuleAsset> CompositeModule = nullptr;

	/** Parent-relative placement zone this module entry targets by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Parent-relative placement zone this module entry targets by default. Use this to restrict reused modules to corners, edges, interior cells, or other specific structural bands within the owning region."))
	ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Any;

	/** Parent-relative level placement policy this module entry targets by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Parent-relative level placement policy this module entry targets by default. Use this to restrict reused modules to ground level, top level, or another vertical band inside the owning region."))
	ELayoutLevelPlacementPolicy LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;

	/** Specific level index used when LevelPlacementPolicy is SpecificLevel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (EditCondition = "LevelPlacementPolicy == ELayoutLevelPlacementPolicy::SpecificLevel", EditConditionHides, ClampMin = "0", UIMin = "0", ToolTip = "Specific level index used when LevelPlacementPolicy is SpecificLevel. Level 0 is ground level."))
	int32 SpecificLevel = 0;

	/** If true, this module entry may be skipped when no compatible structural placement uses it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "If true, this module entry may be skipped when no compatible structural placement uses it. Module entries are normally optional candidates already, but this flag is preserved for parity with child entries and future counted-content policies."))
	bool bOptional = false;
};

/** Child-region-specific side of one region content entry. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutChildRegionContentSettings
{
	GENERATED_BODY()

	/** Child region profile this content entry can place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Child region profile this content entry can place."))
	TObjectPtr<ULayoutProfileAsset> RegionProfile = nullptr;

	/** Parent-relative placement zone this child entry targets by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Parent-relative placement zone this child entry targets by default."))
	ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Any;

	/** Parent-relative level placement policy this child entry targets by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Parent-relative level placement policy this child entry targets by default. Use this to restrict child regions to ground level, top level, or another vertical band inside the parent region."))
	ELayoutLevelPlacementPolicy LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;

	/** Specific parent-relative level index used when LevelPlacementPolicy is SpecificLevel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (EditCondition = "LevelPlacementPolicy == ELayoutLevelPlacementPolicy::SpecificLevel", EditConditionHides, ClampMin = "0", UIMin = "0", ToolTip = "Specific parent-relative level index used when LevelPlacementPolicy is SpecificLevel. Level 0 is ground level."))
	int32 SpecificLevel = 0;

	/** If true, this child entry may be dropped later when parent-owned structural solving cannot place it compatibly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "If true, this child entry may be dropped later when parent-owned structural solving cannot place it compatibly."))
	bool bOptional = false;

	/** If true, this child region's realized vertical access may satisfy the host region's ascent requirement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "If true, this child region's realized vertical access may satisfy the host region's ascent requirement when recursive scheduling composes parent and child traversal. Leave disabled when the child stair/ramp/ladder is only local to the child layout."))
	bool bContributesHostVerticalAccess = false;
};

/** One weighted module-or-child content entry in the long-term unified content-set model. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionContentEntry
{
	GENERATED_BODY()

	/** Stable content-entry identifier used by diagnostics, fixtures, and deterministic planning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable content-entry identifier used by diagnostics, fixtures, and deterministic planning."))
	FName EntryId;

	/** Whether this entry references a module or a child region profile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Whether this entry references a module or a child region profile."))
	ELayoutRegionContentKind ContentKind = ELayoutRegionContentKind::Module;

	/** Relative seeded selection weight for this content entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Relative seeded selection weight for this content entry. Higher values are considered earlier before deterministic seed-based ordering and backtracking choose the final placement."))
	int32 Weight = 1;

	/** Feature tags this content entry provides for counted zone feature requirements. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Feature", ToolTip = "Feature tags this content entry provides for counted zone feature requirements. Multiple module or child-region entries may advertise the same feature so one profile requirement can be satisfied by any compatible placed entry."))
	FGameplayTagContainer ProvidedZoneFeatures;

	/** Optional explicit closure-provider intent for this content entry when it participates in closures or interrupters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Optional explicit closure-provider intent for this content entry when it participates in parent closure or interrupter behavior."))
	TArray<FLayoutClosureProviderIntent> ClosureProviderIntents;

	/** Optional explicit seam-provider intent for this content entry when it participates in shared walls or partitions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Optional explicit seam-provider intent for this content entry when it participates in shared walls or partitions."))
	TArray<FLayoutSeamProviderIntent> SeamProviderIntents;

	/** Module-specific settings. Active when ContentKind is Module. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (EditCondition = "ContentKind == ELayoutRegionContentKind::Module", EditConditionHides, ToolTip = "Module-specific settings. Active when ContentKind is Module."))
	FLayoutModuleContentSettings ModuleSettings;

	/** Child-region-specific settings. Active when ContentKind is ChildRegion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (EditCondition = "ContentKind == ELayoutRegionContentKind::ChildRegion", EditConditionHides, ToolTip = "Child-region-specific settings. Active when ContentKind is ChildRegion."))
	FLayoutChildRegionContentSettings ChildRegionSettings;
};

/** One derived endpoint/interface offer proven from canonical module or region contract data. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutDerivedEndpointOffer
{
	GENERATED_BODY()

	/** Stable offer identifier used by diagnostics and replay fixtures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable offer identifier used by diagnostics and replay fixtures."))
	FLayoutId OfferId;

	/** Local cell that owns this endpoint offer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Local cell that owns this endpoint offer."))
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Face direction where this endpoint is exposed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Face direction where this endpoint is exposed."))
	ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;

	/** Primary connection tag offered at the endpoint face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Connection,Layout.Face", ToolTip = "Primary connection tag offered at the endpoint face."))
	FGameplayTag ConnectionTag;

	/** Allowed neighbor connection tags accepted by this endpoint face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Connection,Layout.Face", ToolTip = "Allowed neighbor connection tags accepted by this endpoint face."))
	FGameplayTagContainer AllowedConnectionTags;

	/** Traversal channels exposed by this endpoint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Traversal", ToolTip = "Traversal channels exposed by this endpoint."))
	FGameplayTagContainer TraversalChannels;

	/** Broad roles cited by the content that exports this endpoint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Broad roles cited by the content that exports this endpoint."))
	TArray<ELayoutModuleRole> Roles;

	/** Occupancy policy that governs the exposed face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Occupancy policy that governs the exposed face."))
	ELayoutFaceOccupancyPolicy OccupancyPolicy = ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor;
};

/** One derived boundary span offer proven from canonical module or region contract data. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutDerivedSpanOffer
{
	GENERATED_BODY()

	/** Stable span-offer identifier used by diagnostics and replay fixtures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable span-offer identifier used by diagnostics and replay fixtures."))
	FLayoutId SpanOfferId;

	/** Optional closure id this span is intended to satisfy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Optional closure id this span is intended to satisfy. Leave empty to match any compatible closure by coverage, thickness, and sealing rules."))
	FName ClosureId;

	/** Local cell that owns this span offer in the derived module contract. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Local cell that owns this span offer in the derived module contract."))
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Boundary-facing side covered by this span offer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Boundary-facing side covered by this span offer."))
	ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;

	/** Primary connection tag offered on the covered face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Connection,Layout.Face", ToolTip = "Primary connection tag offered on the covered face."))
	FGameplayTag ConnectionTag;

	/** Allowed neighbor connection tags accepted by the span face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Connection,Layout.Face", ToolTip = "Allowed neighbor connection tags accepted by the span face."))
	FGameplayTagContainer AllowedConnectionTags;

	/** Roles cited by the content that exports this span. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Roles cited by the content that exports this span."))
	TArray<ELayoutModuleRole> Roles;

	/** Boundary thickness contributed by this span offer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Boundary thickness contributed by this span offer when satisfying a closure requirement."))
	int32 ThicknessCells = 1;

	/** Whether this span seals the boundary side against uncontrolled leakage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Whether this span seals the boundary side against uncontrolled leakage."))
	bool bSealsBoundary = true;
};

/** One derived vertical-access contract proven from canonical module data. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutDerivedVerticalAccessContract
{
	GENERATED_BODY()

	/** Stable vertical-contract identifier used by diagnostics and replay fixtures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable vertical-contract identifier used by diagnostics and replay fixtures."))
	FLayoutId ContractId;

	/** Local cell that owns this vertical-access contract in the derived module contract. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Local cell that owns this vertical-access contract in the derived module contract."))
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Traversal channels that can enter this vertical-access contract from the current level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Traversal", ToolTip = "Traversal channels that can enter this vertical-access contract from the current level."))
	FGameplayTagContainer SourceTraversalChannels;

	/** Traversal channels exported upward to the continuation or landing level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Traversal", ToolTip = "Traversal channels exported upward to the continuation or landing level."))
	FGameplayTagContainer ExitTraversalChannels;

	/** Face direction used to hand off to the next level in this vertical-access contract. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Face direction used to hand off to the next level in this vertical-access contract."))
	ELayoutFaceDirection ExitFaceDirection = ELayoutFaceDirection::PosZ;

	/** True when the upward handoff requires matching yaw between lower and upper content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when the upward handoff requires matching yaw between lower and upper content."))
	bool bRequireMatchingYawAtExit = false;
};

/** One child-facing endpoint capability proven from a child region's top-level authored contract. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutChildCapabilityEndpoint
{
	GENERATED_BODY()

	/** Stable capability identifier used by diagnostics and replay fixtures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable capability identifier used by diagnostics and replay fixtures."))
	FLayoutId CapabilityId;

	/** Child-local cell that proves this parent-facing endpoint capability. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Child-local cell that proves this parent-facing endpoint capability. This is primarily used for level-aware capability summaries and diagnostics."))
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Face direction the child can expose to a parent-selected anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Face direction the child can expose to a parent-selected anchor."))
	ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;

	/** Primary connection tag offered at the child-facing anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Connection,Layout.Face", ToolTip = "Primary connection tag offered at the child-facing anchor."))
	FGameplayTag ConnectionTag;

	/** Allowed opposite-side connection tags accepted by this capability. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Connection,Layout.Face", ToolTip = "Allowed opposite-side connection tags accepted by this capability."))
	FGameplayTagContainer AllowedConnectionTags;

	/** Traversal channels exposed by this capability. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Traversal", ToolTip = "Traversal channels exposed by this capability."))
	FGameplayTagContainer TraversalChannels;

	/** Broad roles cited by the child content that proves this capability. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Broad roles cited by the child content that proves this capability."))
	TArray<ELayoutModuleRole> Roles;
};

/** One child-facing boundary span capability proven from a child region's top-level authored contract. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutChildCapabilitySpan
{
	GENERATED_BODY()

	/** Stable capability identifier used by diagnostics and replay fixtures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable capability identifier used by diagnostics and replay fixtures."))
	FLayoutId CapabilityId;

	/** Child-local cell that proves this boundary span capability from the compiled child contract. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Child-local cell that proves this boundary span capability from the compiled child contract. This is used for level-aware span summaries and diagnostics."))
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Optional closure id this child span is intended to satisfy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Optional closure id this child span is intended to satisfy. Leave empty to match any compatible closure by coverage, thickness, and sealing rules."))
	FName ClosureId;

	/** Boundary-facing side the child can cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Boundary-facing side the child can cover when acting as a closure provider."))
	ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;

	/** Broad roles cited by the child content that proves this capability. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Broad roles cited by the child content that proves this capability."))
	TArray<ELayoutModuleRole> Roles;

	/** Boundary thickness contributed by this child span. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Boundary thickness contributed by this child span capability."))
	int32 ThicknessCells = 1;

	/** Whether the child span seals the boundary against uncontrolled leakage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Whether the child span seals the boundary against uncontrolled leakage."))
	bool bSealsBoundary = true;
};

/** One child-facing seam capability proven from a child region's top-level authored contract. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutChildCapabilitySeam
{
	GENERATED_BODY()

	/** Stable seam-capability identifier used by diagnostics and replay fixtures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable seam-capability identifier used by diagnostics and replay fixtures."))
	FLayoutId CapabilityId;

	/** Child-local cell that proves this seam capability from the compiled child contract. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Child-local cell that proves this seam capability from the compiled child contract. This is used for level-aware seam summaries and diagnostics."))
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Shared seam or partition family this child can support. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Interface", ToolTip = "Shared seam or partition family this child can support."))
	FGameplayTag InterfaceFamily;

	/** Whether this capability may realize ordinary seam cells, owner-side junction cells, or both. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Controls whether this child seam capability may realize ordinary owned seam cells, owner-side junction cells, or both."))
	ELayoutSeamJunctionUsage JunctionUsage = ELayoutSeamJunctionUsage::Both;

	/** Boundary-facing side the child can use when participating in a seam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Boundary-facing side the child can use when participating in a seam."))
	ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;

	/** If true, the child can own a compatible seam on this face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "If true, the child can own a compatible seam on this face."))
	bool bCanOwnSeam = true;

	/** If true, the child can accept a compatible seam owned by another region on this face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "If true, the child can accept a compatible seam owned by another region on this face."))
	bool bCanAcceptSeam = false;
};

/** One committed endpoint anchor selected by a parent before the child solve runs. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutCommittedEndpointAnchor
{
	GENERATED_BODY()

	/** Stable commitment identifier used by diagnostics and replay fixtures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable commitment identifier used by diagnostics and replay fixtures."))
	FLayoutId CommitmentId;

	/** Child/local cell where the committed anchor must be honored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Child/local cell where the committed anchor must be honored."))
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Face direction the child must expose at the committed anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Face direction the child must expose at the committed anchor."))
	ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;

	/** Required block-world center Z when this anchor joins a terrain-aware continuation; INDEX_NONE leaves non-continuation anchors unconstrained. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Required block-world center Z when this anchor joins a terrain-aware continuation; INDEX_NONE leaves non-continuation anchors unconstrained."))
	int32 RequiredWorldCenterBlockZ = INDEX_NONE;

	/** Primary connection tag required at the committed anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Connection,Layout.Face", ToolTip = "Primary connection tag required at the committed anchor."))
	FGameplayTag ConnectionTag;

	/** Allowed opposite-side connection tags accepted by the committed anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Connection,Layout.Face", ToolTip = "Allowed opposite-side connection tags accepted by the committed anchor."))
	FGameplayTagContainer AllowedConnectionTags;

	/** Traversal channels required at the committed anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Traversal", ToolTip = "Traversal channels required at the committed anchor."))
	FGameplayTagContainer TraversalChannels;

	/** If true, the child must preserve matching yaw across the committed anchor handoff. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "If true, the child must preserve matching yaw across the committed anchor handoff."))
	bool bRequireMatchingYawWithFilledNeighbor = false;
};

/** High-level reservation purposes compiled from planning into explicit solve-time records. */
UENUM(BlueprintType)
enum class ELayoutCellReservationKind : uint8
{
	RequiredRoute UMETA(ToolTip = "This cell is on the main required traversal corridor and should prefer strong route-supporting candidates."),
	RouteJunction UMETA(ToolTip = "This cell is where multiple required traversal channels meet and should preserve internal bridge quality."),
	ReachabilityBranch UMETA(ToolTip = "This cell belongs to a secondary reachability branch added so interior traversal remains connected."),
	InteriorFill UMETA(ToolTip = "This cell is part of the interior-fill zone and may prefer structured fill behavior over maximum route exposure."),
	ReservedEmpty UMETA(ToolTip = "This cell is intentionally reserved to remain empty if the current planner mode permits empty placement."),
	VerticalContinuation UMETA(ToolTip = "This cell is reserved as a vertical continuation anchor and should preserve upper/lower traversal continuity."),
	BoundaryShell UMETA(ToolTip = "This cell belongs to the perimeter structural band and should preserve boundary-facing behavior."),
	FutureCompoundConnection UMETA(ToolTip = "This cell is reserved as a future parent or neighboring compound continuation anchor.")
};

/** Shared zone and authored-level scope for one typed sparse-placement rule. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSparsePlacementRuleBase
{
	GENERATED_BODY()

	/** Stable rule identifier used by snapshots, diagnostics, and deterministic ordering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable rule identifier used by snapshots, diagnostics, and deterministic ordering."))
	FName RuleId;

	/** Parent-relative finalized placement zone matched by this rule. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Parent-relative finalized placement zone matched by this rule."))
	ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Interior;

	/** Authored module-level scope matched after Flat or Stepped terrain finalization. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Authored module-level scope matched after Flat or Stepped terrain finalization. Physical terrain shifts preserve this authored level."))
	ELayoutLevelPlacementPolicy LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;

	/** Authored level used when LevelPlacementPolicy is SpecificLevel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (EditCondition = "LevelPlacementPolicy == ELayoutLevelPlacementPolicy::SpecificLevel", EditConditionHides, ClampMin = "0", UIMin = "0", ToolTip = "Authored level index matched when Level Placement Policy is Specific Level."))
	int32 SpecificLevel = 0;
};

/** Preserves matching supported terrain for structural claims without placing optional content afterward. */
USTRUCT(BlueprintType, meta = (DisplayName = "Preserve Supported Terrain"))
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSparsePreserveTerrainRule : public FLayoutSparsePlacementRuleBase
{
	GENERATED_BODY()
};

/** Shared optional-content settings for typed sparse placement rules. */
USTRUCT(BlueprintType, meta = (Hidden))
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSparseContentRuleBase : public FLayoutSparsePlacementRuleBase
{
	GENERATED_BODY()

	/** Selects whether this rule creates preserved terrain or consumes residuals produced elsewhere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Preserve Supported Terrain registers this rule's matching terrain before structural proof. Existing Residuals consumes only residual cells produced elsewhere."))
	ELayoutSparseCandidateSource CandidateSource = ELayoutSparseCandidateSource::PreserveSupportedTerrain;

	/** Content set used by this optional post-structural placement pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Optional-content set used after structural proof. It cannot satisfy Entry, route, VerticalAccess, seam, closure, or child obligations."))
	TObjectPtr<ULayoutRegionContentSetAsset> ContentSet = nullptr;

	/** Minimum Manhattan spacing in cells between accepted roots from this rule. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Minimum Manhattan spacing in cells between accepted placement roots from this rule."))
	int32 MinSpacingCells = 0;
};

/** Attempts one exact best-effort sparse placement count. */
USTRUCT(BlueprintType, meta = (DisplayName = "Place Exact Sparse Count"))
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSparseExactPlacementRule : public FLayoutSparseContentRuleBase
{
	GENERATED_BODY()

	/** Exact number of sparse placements this rule attempts without invalidating structural proof when capacity is lower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Exact number of sparse placements this rule attempts. Underfill remains diagnostic-only."))
	int32 Count = 1;
};

/** Attempts one deterministic best-effort sparse placement count within an authored range. */
USTRUCT(BlueprintType, meta = (DisplayName = "Place Sparse Count Range"))
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSparseRangePlacementRule : public FLayoutSparseContentRuleBase
{
	GENERATED_BODY()

	/** Minimum deterministic sparse placement target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Minimum deterministic sparse placement target."))
	int32 MinCount = 0;

	/** Maximum deterministic sparse placement target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Maximum deterministic sparse placement target."))
	int32 MaxCount = 0;
};

/** Scans all eligible residual cells and places the deterministic maximal legal sparse set. */
USTRUCT(BlueprintType, meta = (DisplayName = "Fill Available Residual Space"))
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSparseFillAvailablePlacementRule : public FLayoutSparseContentRuleBase
{
	GENERATED_BODY()
};

/** One explicit level-scoped fill override authored on a region profile. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutLevelFillRule
{
	GENERATED_BODY()

	/** Stable fill-rule identifier used by diagnostics and fixtures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable fill-rule identifier used by diagnostics and fixtures."))
	FName RuleId;

	/** Level scope matched by this fill rule. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Level scope matched by this fill rule. Use AboveGroundLevel to replace the old upper-level boundary-only behavior."))
	ELayoutLevelPlacementPolicy LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AboveGroundLevel;

	/** Specific level index used when LevelPlacementPolicy is SpecificLevel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (EditCondition = "LevelPlacementPolicy == ELayoutLevelPlacementPolicy::SpecificLevel", EditConditionHides, ClampMin = "0", UIMin = "0", ToolTip = "Specific level index used when LevelPlacementPolicy is SpecificLevel. Level 0 is ground level."))
	int32 SpecificLevel = 0;

	/** Fill behavior applied to matching levels before route and module solving. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Fill behavior applied to matching levels before route and module solving."))
	ELayoutLevelFillMode FillMode = ELayoutLevelFillMode::BoundaryOnly;
};

/** Terrain handling for a selected reserved-open cell during world realization. */
UENUM(BlueprintType)
enum class ELayoutReservedOpenTerrainBehavior : uint8
{
	LeaveExistingTerrain UMETA(ToolTip = "Leave pre-existing terrain in the reserved-open cell unchanged."),
	ClearReservedCell UMETA(ToolTip = "Clear the reserved-open cell's realized shared-cell volume to air before templates are placed.")
};

/** One deterministic hard-open-space rule that removes matching cells from the structural plan before fill begins. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutReservedOpenSpaceRule
{
	GENERATED_BODY()

	/** Stable reserved-open rule identifier used by diagnostics and fixtures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable reserved-open rule identifier used by diagnostics and fixtures."))
	FName RuleId;

	/** Parent-relative zone this hard open-space rule targets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Parent-relative zone this hard open-space rule targets."))
	ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Interior;

	/** Level scope matched by this hard open-space rule. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Level scope matched by this hard open-space rule."))
	ELayoutLevelPlacementPolicy LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;

	/** Specific level index used when LevelPlacementPolicy is SpecificLevel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (EditCondition = "LevelPlacementPolicy == ELayoutLevelPlacementPolicy::SpecificLevel", EditConditionHides, ClampMin = "0", UIMin = "0", ToolTip = "Specific level index used when LevelPlacementPolicy is SpecificLevel. Level 0 is ground level."))
	int32 SpecificLevel = 0;

	/** Percentage of matching candidate cells to remove from the structural plan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "100.0", ToolTip = "Percentage of matching candidate cells to remove from the structural plan. Author this as a 0 to 100 percentage."))
	float ReservedPercent = 0.0f;

	/** Minimum hard-open cells that must be removed when this rule matches candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Minimum hard-open cells that must be removed when this rule matches candidates."))
	int32 MinReservedCells = 0;

	/** Maximum hard-open cells this rule may remove. Use zero to leave the percentage result unclamped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Maximum hard-open cells this rule may remove. Use zero to leave the percentage result unclamped."))
	int32 MaxReservedCells = 0;

	/** Whether this reserved void preserves terrain or clears its realized shared-cell volume to air. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "LeaveExistingTerrain preserves terrain already in the selected void. ClearReservedCell clears only this reserved cell's realized shared-cell volume; adjacent terrain, supports, bridges, and VerticalAccess cells remain untouched."))
	ELayoutReservedOpenTerrainBehavior TerrainBehavior = ELayoutReservedOpenTerrainBehavior::LeaveExistingTerrain;
};

/** One compiled per-cell reservation record used by route-aware leaf solving and diagnostics. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutCellReservationRecord
{
	GENERATED_BODY()

	/** Stable reservation identifier used by diagnostics and replay fixtures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable reservation identifier used by diagnostics and replay fixtures."))
	FLayoutId ReservationId;

	/** Reserved local cell in layout-cell coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Reserved local cell in layout-cell coordinates."))
	FIntVector Cell = FIntVector::ZeroValue;

	/** Planner intent active on the reserved cell when the reservation was compiled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Planner intent active on the reserved cell when the reservation was compiled."))
	ELayoutCellIntent Intent = ELayoutCellIntent::Interior;

	/** Reservation family that drives candidate filtering, ordering, or diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Reservation family that drives candidate filtering, ordering, or diagnostics."))
	ELayoutCellReservationKind ReservationKind = ELayoutCellReservationKind::InteriorFill;

	/** Terrain behavior for ReservedEmpty records. Other reservation kinds leave this at LeaveExistingTerrain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Terrain behavior selected by the reserved-open rule. Only ReservedEmpty records use this value."))
	ELayoutReservedOpenTerrainBehavior TerrainBehavior = ELayoutReservedOpenTerrainBehavior::LeaveExistingTerrain;
};

/** One directional traversal requirement on a compiled route-constraint cell. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRouteFaceRequirement
{
	GENERATED_BODY()

	/** Local face that must expose the required traversal channel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Local face that must expose the required traversal channel."))
	ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;

	/** Traversal channel that the constrained face must expose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Traversal", ToolTip = "Traversal channel that the constrained face must expose."))
	FGameplayTag TraversalChannel;
};

/** One compiled per-cell route constraint used to filter and rank leaf candidates. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRouteConstraintRecord
{
	GENERATED_BODY()

	/** Stable route-constraint identifier used by diagnostics and replay fixtures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable route-constraint identifier used by diagnostics and replay fixtures."))
	FLayoutId ConstraintId;

	/** Constrained local cell in layout-cell coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Constrained local cell in layout-cell coordinates."))
	FIntVector Cell = FIntVector::ZeroValue;

	/** Planner intent active on the constrained cell when the route contract was compiled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Planner intent active on the constrained cell when the route contract was compiled."))
	ELayoutCellIntent Intent = ELayoutCellIntent::Interior;

	/** Face-specific traversal requirements that the chosen candidate must satisfy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Face-specific traversal requirements that the chosen candidate must satisfy."))
	TArray<FLayoutRouteFaceRequirement> FaceRequirements;

	/** If true, candidate ordering should actively prefer strong route support on this cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "If true, candidate ordering should actively prefer strong route support on this cell."))
	bool bScoreAsMainRoute = false;
};

/** Deterministic caller-owned forced bundle insertion committed before a region solve begins. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutForcedPlacementBundleInsertion
{
	GENERATED_BODY()

	/** Frozen root placement bundle selected for insertion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Frozen root placement bundle selected for insertion."))
	FLayoutId BundleId;

	/** Planned local cell where the selected bundle must anchor before ordinary search continues. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Planned local cell where the selected bundle must anchor before ordinary search continues."))
	FIntVector AnchorCell = FIntVector::ZeroValue;

	/** Planned proving cell that justified the anchored insertion, retained for diagnostics and later stepped-terrain consumption. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Planned proving cell that justified the anchored insertion, retained for diagnostics and later stepped-terrain consumption."))
	FIntVector ProvingCell = FIntVector::ZeroValue;
};

/** Whether a module face must face a specific side of the owning region when placed. */
UENUM(BlueprintType)
enum class ELayoutFaceBoundaryRequirement : uint8
{
	Any UMETA(ToolTip = "No boundary-facing constraint. Face may connect to exterior, interior, or stepped-terrain seam neighbors when its occupancy and connection tags allow that neighbor."),
	MustFaceExterior UMETA(ToolTip = "Face must point toward the owning region's outward boundary. Placement rejected if this face connects to an interior neighbor."),
	MustFaceInterior UMETA(ToolTip = "Face must point toward the owning region's interior. Placement rejected if this face connects to the region boundary or empty exterior space."),
	MustFaceTerrainSeam UMETA(ToolTip = "Face must point toward a planned, filled terrain seam neighbor and may not point at the outer region boundary. Terrain seams exist only when stepped terrain support creates them. This face may be traversable for authored seam doors or passages."),
	MustFaceExteriorOrTerrainSeam UMETA(ToolTip = "Face must point toward either empty exterior space or a planned, filled terrain seam neighbor. Terrain seams exist only when stepped terrain support creates them. Use an occupancy policy that permits both states so one wall or door module can serve outer and terrain-seam edges.")
};

/** One authored face rule on a fixed-size module cell. */
USTRUCT(BlueprintType)
struct FLayoutFaceRule
{
	GENERATED_BODY()

	/** Cell face this rule applies to. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Cell face this rule applies to. Module assets set this automatically from the named face slot."))
	ELayoutFaceDirection Direction = ELayoutFaceDirection::PosX;

	/** Single connection tag this face offers to a neighboring filled face during compatibility checks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (Categories = "Layout.Connection,Layout.Face", ToolTip = "Single connection tag this face offers to a neighboring filled face during compatibility checks. Use it to describe what this side is, such as exterior, open, doorway, walkway, or solid."))
	FGameplayTag ConnectionTag;

	/** Neighbor connection tags this face accepts when the adjacent cell is filled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (Categories = "Layout.Connection,Layout.Face", ToolTip = "Neighbor connection tags this face accepts when the adjacent cell is filled. Filled-neighbor policies require at least one tag here; empty-only policies ignore this list."))
	FGameplayTagContainer AllowedConnectionTags;

	/** Occupancy policy for the adjacent cell on this face. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Determines whether this face expects a filled neighbor, empty space, or wildcard occupancy before face-tag compatibility is checked."))
	ELayoutFaceOccupancyPolicy OccupancyPolicy = ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor;

	/** Traversal channels exposed on this face so the reachability graph can bridge matching neighbors. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (Categories = "Layout.Traversal", ToolTip = "Traversal channels exposed through this face. Matching traversal tags on adjacent faces create reachability graph links, and the module-level traversal set is derived from these face tags."))
	FGameplayTagContainer ConnectedTraversalChannels;

	/** Boundary-facing requirement for this face. Determines whether the face must point toward the region exterior, interior, or has no constraint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Determines whether this face must point toward the owning region's exterior boundary, interior, or has no constraint. MustFaceExterior rejects placements where this face connects to an interior neighbor. MustFaceInterior rejects placements where this face connects to the region boundary or empty exterior space."))
	ELayoutFaceBoundaryRequirement BoundaryRequirement = ELayoutFaceBoundaryRequirement::Any;

	/** Requires the adjacent filled module to use the same yaw rotation step. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Requires the adjacent filled module to use the same yaw rotation step when this face connects to a filled neighbor. Use on continuation faces where authored geometry must stay phase-aligned across cells, such as stacked spiral stair modules."))
	bool bRequireMatchingYawWithFilledNeighbor = false;

	/** Returns true when this face exposes at least one connected traversal channel. */
	bool IsTraversable() const;

	/** Returns the offered connection tag as a container for compatibility checks. */
	FGameplayTagContainer GetEffectiveConnectionTags() const;

	/** Returns the singular authored connection tag. */
	FGameplayTag GetEffectiveConnectionTag() const;

	/** Returns the accepted connection tags used by solver compatibility. */
	const FGameplayTagContainer& GetEffectiveAllowedConnectionTags() const;

};

/** Six fixed face-rule slots for one layout module. Direction is derived from the slot name. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutModuleFaceRules
{
	GENERATED_BODY()

	FLayoutModuleFaceRules();

	/** Authored positive X face rule. Direction is normalized automatically by module assets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Authored positive X face rule. For square XY modules, this face can rotate onto any horizontal world side during solving."))
	FLayoutFaceRule PosX;

	/** Authored negative X face rule. Direction is normalized automatically by module assets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Authored negative X face rule. For square XY modules, this face can rotate onto any horizontal world side during solving."))
	FLayoutFaceRule NegX;

	/** Authored positive Y face rule. Direction is normalized automatically by module assets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Authored positive Y face rule. For square XY modules, this face can rotate onto any horizontal world side during solving."))
	FLayoutFaceRule PosY;

	/** Authored negative Y face rule. Direction is normalized automatically by module assets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Authored negative Y face rule. For square XY modules, this face can rotate onto any horizontal world side during solving."))
	FLayoutFaceRule NegY;

	/** Authored top face rule. Direction is normalized automatically by module assets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Authored top face rule. Yaw rotation does not change this face; use it for ceilings, upper walkways, roofs, or vertical stack rules."))
	FLayoutFaceRule PosZ;

	/** Authored bottom face rule. Direction is normalized automatically by module assets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Authored bottom face rule. Yaw rotation does not change this face; use it for floors, supports, underside openings, or vertical stack rules."))
	FLayoutFaceRule NegZ;

	/** Forces each named slot to report the matching face direction. */
	void NormalizeDirections();

	/** Returns a mutable face rule for one direction. */
	FLayoutFaceRule* FindRule(ELayoutFaceDirection Direction);

	/** Returns a face rule for one direction. */
	const FLayoutFaceRule* FindRule(ELayoutFaceDirection Direction) const;

	/** Applies an externally built rule to the matching named slot, then normalizes direction. */
	void SetRule(const FLayoutFaceRule& Rule);

	/** Returns the six face rules in stable direction order. */
	TArray<FLayoutFaceRule> ToArray() const;
};

/** One authored internal access relationship inside a module. */
USTRUCT(BlueprintType)
struct FLayoutInternalAccessLink
{
	GENERATED_BODY()

	/** Traversal channel where this internal traversal link starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (Categories = "Layout.Traversal", ToolTip = "Traversal channel where this internal traversal link starts. The tag must be exposed by at least one face on the owning module."))
	FGameplayTag FromTraversalChannel;

	/** Traversal channel reached by this internal traversal link. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (Categories = "Layout.Traversal", ToolTip = "Traversal channel reached by this internal traversal link. The tag must be exposed by at least one face on the owning module. Use this for stairs, ramps, ladders, doors, or other inside-the-module connections between traversal channels."))
	FGameplayTag ToTraversalChannel;

	/** If true, the internal link works in both directions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "If true, traversal works both ways between the two traversal channels. Disable this for one-way drops, climbs, or other directional traversal."))
	bool bBidirectional = true;
};

/** One project-owned host-biome binding row that points Porism regions to layout assets. */
USTRUCT(BlueprintType)
struct FLayoutTerrainSurfaceSearchSettings
{
	GENERATED_BODY()

	/** Block-space Z where world-facing surface searches start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Block-space Z where world-facing surface searches start."))
	int32 TerrainSearchStartZ = 256;

	/** Number of blocks searched downward from TerrainSearchStartZ for a usable world-facing surface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Number of blocks searched downward from TerrainSearchStartZ for a usable world-facing surface."))
	int32 TerrainSearchDepthBlocks = 256;

	/** Lateral block radius frozen around an Underground cavity to find its compatible Domain boundary. Increase only for authored cavities wider than this shell. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Lateral block radius frozen around an Underground cavity to find its compatible Domain boundary. Increase only for authored cavities wider than this shell."))
	int32 CavityDomainBoundarySearchBlocks = 64;
};

/** World-facing top-level root solve budget carried before one caller maps it into execution settings. */
USTRUCT(BlueprintType)
struct FLayoutRootSolveBudgetSettings
{
	GENERATED_BODY()

	/** Overall wall-clock timeout in seconds for one root solve. Zero or below disables the time limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Overall wall-clock timeout in seconds for one root solve. Zero or below disables the time limit."))
	float MaxSolveDurationSeconds = 5.0f;
};

/** World-binding-side placement family used by the world-binding continuation pipeline. */
UENUM(BlueprintType)
enum class ELayoutWorldBindingPlacementKind : uint8
{
	None,
	OrdinaryRoot,
	SurfacePath,
	BridgeContinuation,
	TunnelContinuation
};

/** Named continuation families selected explicitly by the world-binding path pipeline. */
UENUM(BlueprintType)
enum class ELayoutWorldBindingContinuationFamilyType : uint8
{
	SurfacePath,
	BridgeContinuation,
	TunnelContinuation
};

/** Structured world-binding terrain-fit outcome preserved for runtime diagnostics and cached records. */
UENUM(BlueprintType)
enum class ELayoutWorldBindingTerrainFitDiagnosticKind : uint8
{
	None,
	AcceptedFlatFit,
	AcceptedFoundationFill,
	AcceptedPerimeterRamp,
	RejectedBridgeGradeGap,
	RejectedTerrainVariation,
	RejectedFoundationDepthExceeded,
	RejectedPerimeterTransitionDepthExceeded
};

/** One stepped-terrain support sample preserved on the frozen request carrier before recursive solving begins. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSteppedTerrainSupportSample
{
	GENERATED_BODY()

	/** Planned local cell whose terrain support was sampled for stepped solving. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Planned local cell whose terrain support was sampled for stepped solving."))
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Raw block-world top-surface Z sampled for this planned cell before lattice snapping is derived. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Raw block-world top-surface Z sampled for this planned cell before lattice snapping is derived."))
	int32 SupportSurfaceZ = 0;

	/** Global lattice-aligned support-floor Z resolved by rounding the sampled terrain surface down to the shared cell height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Global lattice-aligned support-floor Z resolved by rounding the sampled terrain surface down to the shared cell height."))
	int32 SnappedSupportFloorZ = 0;

	/** Global lattice-aligned support-ceiling Z resolved by rounding the sampled terrain surface up to the shared cell height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Global lattice-aligned support-ceiling Z resolved by rounding the sampled terrain surface up to the shared cell height."))
	int32 SnappedSupportCeilingZ = 0;
};

/** One deterministic neighboring-cell step preserved on the frozen request carrier for stepped solving. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSteppedTerrainAdjacencyStep
{
	GENERATED_BODY()

	/** Planned local source cell of the stepped-terrain adjacency. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Planned local source cell of the stepped-terrain adjacency."))
	FIntVector FromCell = FIntVector::ZeroValue;

	/** Planned local destination cell of the stepped-terrain adjacency. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Planned local destination cell of the stepped-terrain adjacency."))
	FIntVector ToCell = FIntVector::ZeroValue;

	/** Absolute raw terrain step height in blocks between the two planned cells before lattice snapping is applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Absolute raw terrain step height in blocks between the two planned cells before lattice snapping is applied."))
	int32 StepHeightBlocks = 0;

	/** Absolute designated-cell level delta between the two planned cells after lattice snapping is applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Absolute designated-cell level delta between the two planned cells after lattice snapping is applied."))
	int32 SnappedLevelDelta = 0;
};

/** Snapshot-owned stepped-terrain support map preserved on a region solve request before stepped adjacency is consumed by the solver. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSteppedTerrainSupportMap
{
	GENERATED_BODY()

	/** Shared cell height in blocks used while normalizing stepped support samples onto the request-owned lattice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Shared cell height in blocks used while normalizing stepped support samples onto the request-owned lattice."))
	int32 SharedCellHeightInBlocks = 0;

	/** Per-cell terrain support samples preserved before stepped adjacency is compiled into solver constraints. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Per-cell terrain support samples preserved before stepped adjacency is compiled into solver constraints."))
	TArray<FLayoutSteppedTerrainSupportSample> SupportSamples;

	/** Neighboring-cell step requirements preserved before stepped traversal legality is solved. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Neighboring-cell step requirements preserved before stepped traversal legality is solved."))
	TArray<FLayoutSteppedTerrainAdjacencyStep> AdjacencySteps;

	/** Largest observed raw terrain step between neighboring sampled cells on this request-owned support map. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Largest observed raw terrain step between neighboring sampled cells on this request-owned support map."))
	int32 MaximumObservedNeighborHeightDelta = 0;

	/** Largest observed designated-cell level delta between neighboring sampled cells after lattice snapping is applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Largest observed designated-cell level delta between neighboring sampled cells after lattice snapping is applied."))
	int32 MaximumObservedSnappedLevelDelta = 0;
};

/** Public root-side support classification for one stepped-terrain transition. */
UENUM(BlueprintType)
enum class ELayoutSteppedTerrainTransitionSupportStatus : uint8
{
	MissingAdjacentVerticalAccessIntent UMETA(ToolTip = "No adjacent planned VerticalAccess cell participates in the required terrain-driven transition."),
	MissingRootVerticalAccessCandidate UMETA(ToolTip = "An adjacent planned VerticalAccess cell exists, but no frozen root candidate can support it."),
	SupportedByRootVerticalAccessCandidate UMETA(ToolTip = "The required terrain-driven transition is backed by at least one frozen root vertical-access candidate.")
};

/** Public diagnostic for one stepped-terrain transition compiled during root solving. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSteppedTerrainTransitionDiagnostic
{
	GENERATED_BODY()

	/** Root-local support cell on one side of the terrain step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Root-local support cell on one side of the terrain step."))
	FIntVector FromCell = FIntVector::ZeroValue;

	/** Root-local support cell on the opposite side of the terrain step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Root-local support cell on the opposite side of the terrain step."))
	FIntVector ToCell = FIntVector::ZeroValue;

	/** Exact terrain step height in blocks between the two support cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Exact terrain step height in blocks between the two support cells."))
	int32 StepHeightBlocks = 0;

	/** Public root-side support classification for this terrain-driven transition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Public root-side support classification for this terrain-driven transition."))
	ELayoutSteppedTerrainTransitionSupportStatus SupportStatus =
		ELayoutSteppedTerrainTransitionSupportStatus::MissingAdjacentVerticalAccessIntent;
};

/** Public deterministic terrain-stage diagnostic carried across preview, runtime, planning, and continuation entrypoints. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSteppedTerrainStageDiagnostic
{
	GENERATED_BODY()

	/** Deterministic stage index in stable staged-terrain evaluation order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic stage index in stable staged-terrain evaluation order."))
	int32 StageIndex = 0;

	/** Frontier id that unlocks this stage, or INDEX_NONE for the initial reachable plateau. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Frontier id that unlocks this stage, or INDEX_NONE for the initial reachable plateau."))
	int32 TerrainAscentFrontierId = INDEX_NONE;

	/** Region debug path that emitted this stage diagnostic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Region debug path that emitted this stage diagnostic."))
	FString RegionDebugPath;

	/** Lowest snapped terrain level represented by this stage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Lowest snapped terrain level represented by this stage."))
	int32 SnappedTerrainLevel = INDEX_NONE;

	/** True when this stage represents currently reachable plateau ownership. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when this stage represents currently reachable plateau ownership."))
	bool bReachablePlateau = false;

	/** True when this stage still represents a deferred terrace until its frontier unlocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when this stage still represents a deferred terrace until its frontier unlocks."))
	bool bDeferredTerrace = false;
};

/** Public root-side ownership/support diagnostic for one staged ascent frontier. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSteppedTerrainFrontierOwnershipDiagnostic
{
	GENERATED_BODY()

	/** Deterministic ascent-frontier id. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic ascent-frontier id."))
	int32 TerrainAscentFrontierId = INDEX_NONE;

	/** Region debug path that emitted this frontier ownership diagnostic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Region debug path that emitted this frontier ownership diagnostic."))
	FString RegionDebugPath;

	/** Lower snapped terrain level participating in this ascent frontier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Lower snapped terrain level participating in this ascent frontier."))
	int32 LowerSnappedTerrainLevel = 0;

	/** Higher snapped terrain level participating in this ascent frontier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Higher snapped terrain level participating in this ascent frontier."))
	int32 HigherSnappedTerrainLevel = 0;

	/** Protected root-local ascent-pocket cells for this frontier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Protected root-local ascent-pocket cells for this frontier."))
	TArray<FIntVector> ProtectedAscentPocketCells;

	/** Parent-owned vertical-access cells counted for this frontier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Parent-owned vertical-access cells counted for this frontier."))
	TArray<FIntVector> ParentOwnedVerticalAccessCells;

	/** Child-owned region paths counted for this frontier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Child-owned region paths counted for this frontier."))
	TArray<FString> ChildOwnedRegionDebugPaths;

	/** True when this frontier currently has some legal supporting ownership path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when this frontier currently has some legal supporting ownership path."))
	bool bHasSupportingVerticalAccess = false;

	/** True when this frontier remains blocked for the current staged contract. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when this frontier remains blocked for the current staged contract."))
	bool bBlocked = true;
};

/** Ordinary root or continuation terrain-transition policy owned by the world-binding side. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutWorldBindingTerrainTransitionPolicy
{
	GENERATED_BODY()

	/** If true, shallow vertical support fill is allowed under accepted world-facing footprints. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "If true, shallow vertical support fill is allowed under accepted world-facing footprints. This is not bridge authorization."))
	bool bAllowFoundationFill = false;

	/** Maximum support depth allowed when shallow foundation fill is used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Maximum support depth allowed when shallow foundation fill is used under accepted world-facing footprints."))
	int32 MaxFoundationDepth = 3;

	/** If true, the outer perimeter of an accepted world-facing root or continuation may receive a stepped ramp transition. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "If true, the outer perimeter of an accepted world-facing root or continuation may receive a stepped ramp transition so adjacent terrain meets the intended floor plane exactly. This does not apply to child regions or internal footprint edges."))
	bool bAllowPerimeterRampTransition = false;

	/** Minimum connected elevated terrain cells required before stepped terrain shifts instead of flattening the small cluster. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Connected elevated terrain clusters smaller than this count flatten into the base stage. Increase to avoid small stair/bridge features; decrease to preserve short raised terrain steps. Applied consistently to roots and continuations."))
	int32 MinimumSteppedTerrainShiftClusterCells = 3;

	/** Retries once with a fresh flat terrain contract after classified stepped terrain-shape prewarm insufficiency. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "If enabled, a stepped root or continuation retries once as flat terrain when reserved-open, terrain-qualified Entry, or stage-topology prewarm cannot satisfy a terrain-shape-only requirement. The retry rebuilds every terrain contract from frozen evidence. It never masks module, authored Entry, VerticalAccess, cancellation, timeout, or CSP failures."))
	bool bFallbackToFlatTerrainWhenSteppedReservedOpenIsInfeasible = true;

};

/** Final-shape world-facing placement policy shared by ordinary roots and continuation families. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutWorldBindingPlacementPolicy
{
	GENERATED_BODY()

	/** Surface-search settings used while discovering or validating terrain-facing placements. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Surface-search settings used while discovering or validating terrain-facing placements."))
	FLayoutTerrainSurfaceSearchSettings SurfaceSearch;

	// Terrain-sample spacing. Always 0 — derived from SharedCellSizeInBlocks.X.
	// Not user-configurable; kept for struct-layout compatibility.
	int32 TerrainSampleGridSpacing = 0;

	// Height-ignore threshold. Always 0 — sub-cell bumps filtered by SnappedLevelDelta.
	// Not user-configurable; kept for struct-layout compatibility.
	int32 HeightIgnoreThreshold = 0;


	/** Terrain-transition policy applied after one world-facing footprint is accepted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Terrain-transition policy applied after one world-facing footprint is accepted."))
	FLayoutWorldBindingTerrainTransitionPolicy TerrainTransition;

};

/** Shared continuation-family path/terrain policy owned per family on the world-binding side. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutWorldBindingContinuationPolicy
{
	GENERATED_BODY()

	/** Extra connector/path cells sampled around the endpoint box so paths can bend around terrain obstacles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Extra path-grid cells sampled around the endpoint box so continuation routes can bend around terrain obstacles."))
	int32 PathPaddingCells = 4;

	/** Maximum allowed surface-height change between adjacent traversable continuation cells. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Maximum allowed surface-height change between adjacent traversable continuation cells."))
	int32 MaxSlopeBlocks = 3;

	/** Maximum number of consecutive gap cells the continuation may bridge. Zero means gaps are blocked. Bridge-only setting on the active path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Maximum number of consecutive gap cells the continuation may bridge. Zero means gaps are blocked. On the active path this only applies to BridgeContinuation families; surface paths and tunnels keep gap spans blocked."))
	int32 MaxBridgeGapCells = 0;
};

/** Minimal stable continuation-selection contract resolved before world-facing continuation solves begin. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutResolvedWorldBindingContinuationSelection
{
	GENERATED_BODY()

	/** Stable authored continuation-family identity selected for this root or path continuation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable authored continuation-family identity selected for this root or path continuation. Ordinary roots leave this empty."))
	FName FamilyId;

	/** Deterministic world-facing placement kind selected for the continuation family. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic world-facing placement kind selected for the continuation family. Ordinary roots leave this as None."))
	ELayoutWorldBindingPlacementKind PlacementKind = ELayoutWorldBindingPlacementKind::None;

	/** Deterministic local continuation-entry level that later runtime and solver stages must preserve without guessing again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic local continuation-entry level that later runtime and solver stages must preserve without guessing again. Ordinary roots leave this as -1."))
	int32 ResolvedEntryLevel = INDEX_NONE;
};

/** Minimal stable frontend carrier preserved on planned and resolved site records. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutWorldBindingSiteFrontendSelection
{
	GENERATED_BODY()

	/** Stable world-binding identity that owns this site-facing frontend selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable world-binding identity that owns this site-facing frontend selection. Explicit direct-root solves leave this empty."))
	FName WorldBindingId;

	/** Stable authored candidate identity selected for this site-facing frontend selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable authored candidate identity selected for this site-facing frontend selection. Explicit direct-root solves leave this empty."))
	FName WorldBindingCandidateId;

	/** Minimal stable continuation-selection contract preserved on this site-facing frontend selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Minimal stable continuation-selection contract preserved on this site-facing frontend selection. Ordinary roots and explicit direct-root solves leave this unset."))
	FLayoutResolvedWorldBindingContinuationSelection ResolvedContinuationSelection;

	/** Project-authored biome row selected for this site-facing frontend selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Project-authored biome row selected for this site-facing frontend selection when one specific live row was resolved. Explicit direct-root solves may leave this empty when the binding supports multiple compatible rows."))
	FName BiomeRowName;

	/** Compatible biome/domain rows preserved for site-facing terrain-fit allow-list replay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Compatible biome/domain row names preserved for site-facing terrain-fit allow-list replay. Explicit direct-root solves keep the owning binding's compatible row list here when terrain fit should accept any matching live row instead of one specific authored row."))
	TArray<FName> CompatibleBiomeRowNames;

	/** True when site-facing terrain fit should use any active biome surface instead of a specific owned row filter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when site-facing terrain fit should use any active biome surface instead of replaying one specific owned biome row or allow-list. Explicit direct-root tool previews use this when the selected world binding contributes configuration and lattice policy but should not hard-filter the manually chosen location by one authored biome row list."))
	bool bUseAnyActiveBiomeSurface = false;
};

/** Minimal stable root publication carrier preserved on site-facing records. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRootPublicationMetadata
{
	GENERATED_BODY()

	/** Stable root solve id preserved from the request/publication surface for cache, replay, and trace correlation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable root solve id preserved from the request/publication surface for cache, replay, and trace correlation."))
	FLayoutId RootSolveId;

	/** Stable root candidate id preserved from the request/publication surface for cache, replay, and trace correlation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable root candidate id preserved from the request/publication surface for cache, replay, and trace correlation."))
	FLayoutId RootCandidateId;

	/** Stable root placement-policy id preserved from the request/publication surface for later world-binding trace correlation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable root placement-policy id preserved from the request/publication surface for later world-binding trace correlation."))
	FLayoutId RootPlacementPolicyId;
};

/** Minimal stable authored solve-source carrier preserved on planned and resolved site records. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSiteSolveSourceSelection
{
	GENERATED_BODY()

	/** Layout profile selected for this site solve source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Layout profile selected for this site solve source."))
	TSoftObjectPtr<ULayoutProfileAsset> LayoutProfile;

	/** Unified content set selected for this site solve source when the profile uses the final authored content-entry model. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Unified content set selected for this site solve source when the profile uses the final authored content-entry model."))
	TSoftObjectPtr<ULayoutRegionContentSetAsset> ContentSet;

	/** Binding-owned exported endpoint connector tags compiled for this selected root source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Connector", ToolTip = "Binding-owned exported endpoint connector tags compiled for this selected root source. Preserve the selected world-facing export contract here so solved-site payloads do not need to reread structural profile authoring."))
	FGameplayTagContainer ExportedConnectorTypeTags;

	/** Deterministic solve seed assigned to this site solve source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic solve seed assigned to this site solve source."))
	int32 SolveSeed = 0;
};

/** Whether a cached site may enter realization without changing its solve verdict. */
UENUM(BlueprintType)
enum class ELayoutCachedApplyability : uint8
{
	Rejected UMETA(DisplayName = "Rejected"),
	Partial UMETA(DisplayName = "Partial"),
	Solved UMETA(DisplayName = "Solved")
};

/** Minimal runtime-only lifecycle carrier preserved on resolved site records after solve. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FResolvedLayoutSiteRuntimeState
{
	GENERATED_BODY()

	/** True once a deterministic full solve result has been cached for this reserved site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True once a deterministic full solve result has been cached for this reserved site. Partial results remain false."))
	bool bLayoutSolved = false;

	/** Immutable publication outcome that authorizes cached realization without rewriting the solve verdict. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Cached applyability outcome. Partial may realize retained placements while preserving the rejected full solve."))
	ELayoutCachedApplyability CachedApplyability = ELayoutCachedApplyability::Rejected;

	/** True once the solved site has been stamped into the chunk world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True once the solved site has been stamped into the chunk world."))
	bool bLayoutRealized = false;

	/** Once realized into the chunk world, do not stamp again unless an explicit regeneration path opts in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Once realized into the chunk world, do not stamp again unless an explicit regeneration path opts in."))
	bool bHasBeenCommittedToChunkWorld = false;

	/** Latest structured terrain-fit outcome preserved during runtime realization for this site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Latest structured terrain-fit outcome preserved during runtime realization for this site. World-binding ordinary-root realization can distinguish accepted flat/fill/ramp behavior from rejected bridge-grade or depth-overrun terrain outcomes without relying only on failure strings."))
	ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind = ELayoutWorldBindingTerrainFitDiagnosticKind::None;
};

/** Minimal stable location payload preserved on resolved site records after solve. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FResolvedLayoutSiteLocationMetadata
{
	GENERATED_BODY()

	/** Stable world-space center used to rediscover or realize this layout site later. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable world-space center used to rediscover or realize this layout site later."))
	FIntVector SiteCenterBlockWorldPos = FIntVector::ZeroValue;

	/** Realized footprint anchor preserved after terrain fit so later anchor math can replay the stamped location without mutating the stable site center identity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Realized footprint anchor preserved after terrain fit so later anchor math can replay the stamped location without mutating the stable site center identity. Zero means no terrain-conformed footprint anchor has been preserved yet."))
	FIntVector RealizedFootprintMinBlockWorldPos = FIntVector::ZeroValue;

	/** Optional site tags used for later connector or future compound filtering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Optional site tags used for later connector or future compound filtering."))
	FGameplayTagContainer SiteTags;
};

/** One planned cell intent before a specific module is chosen. */
USTRUCT(BlueprintType)
struct FLayoutPlannedCell
{
	GENERATED_BODY()

	/** Grid coordinate in cell units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Grid coordinate in cell units."))
	FIntVector Cell = FIntVector::ZeroValue;

	/** Broad intent assigned by the planner before the module solve begins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Broad intent assigned by the planner before the module solve begins."))
	ELayoutCellIntent Intent = ELayoutCellIntent::Interior;

	/** System that owns this cell when Intent is Entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "System that owns this cell when Intent is Entry."))
	ELayoutEntryOrigin EntryOrigin = ELayoutEntryOrigin::None;

	/** Authored level used by module placement policies. INDEX_NONE resolves to Cell.Z for legacy and unshifted plans. Terrain adaptation may shift Cell.Z without changing this level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Authored level used by Ground, Specific, Above Ground, and Top module placement policies. Leave unset to use Cell Z. Terrain stepping shifts spatial Cell Z while preserving this authored level."))
	int32 ModuleLevelIndex = INDEX_NONE;

	/** Whether terrain adaptation injected this cell for a stage transition.
	 *  Final same-Z topology determines its Boundary or Interior shell intent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Whether terrain adaptation injected this stage-transition cell. Final same-Z topology determines its Boundary or Interior shell intent."))
	bool bIsBridgeCell = false;

	/** Adapter-generated upper-deck offer, not mandatory bottom support. Joint occupancy may leave it empty unless a selected module, contact or route requires it. Authored cells must never set this flag. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Generated upper-deck offer. The module solve may keep open sky here or occupy it with compatible stairs/deck support. Bottom support and authored cells remain mandatory."))
	bool bIsTopBridgeOffer = false;

	/** Preferred placement zone for this cell. Bridge cells use local same-level topology rather than global footprint coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Preferred placement zone for this cell. Bridge cells use local same-level topology rather than global footprint coordinates."))
	ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Interior;

	/** Horizontal face mask marking adapter-authored terrain retaining seams. Each set face requires a retaining wall module face toward its planned neighbor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Horizontal face mask marking adapter-authored terrain retaining seams. Each set face requires a retaining wall module face toward its planned neighbor."))
	uint8 TerrainSeamFaceMask = 0;

	/** Directed upper-bridge contact faces a VerticalAccess landing must support. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Directed upper-bridge contact faces a VerticalAccess landing must support."))
	uint8 VerticalAccessLandingContactMask = 0;
};

/** One solved module placement in the layout grid. */
USTRUCT(BlueprintType)
struct FLayoutPlacedLocalCellFaceRuleSnapshot
{
	GENERATED_BODY()

	/** Occupied local cell inside the rigid placed bundle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Occupied local cell inside the rigid placed bundle."))
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Pointer-free leaf template identity frozen for this occupied local cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Pointer-free leaf template identity frozen for this occupied local cell."))
	FSoftObjectPath TemplatePath;

	/** Leaf yaw relative to the composite root yaw in clockwise quarter-turn steps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", ClampMax = "3", ToolTip = "Leaf yaw relative to the composite root yaw in clockwise quarter-turn steps."))
	int32 RelativeYawRotationSteps = 0;

	/** Roles frozen for this exact occupied local cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Roles frozen for this exact occupied local cell."))
	TArray<ELayoutModuleRole> Roles;

	/** Planned intents frozen for this exact occupied local cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Planned intents frozen for this exact occupied local cell."))
	TArray<ELayoutCellIntent> SupportedCellIntents;

	/** Exposed face rules realized for this local cell after snapshot compilation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Exposed face rules realized for this local cell after snapshot compilation."))
	TArray<FLayoutFaceRule> ExposedFaceRules;
};

USTRUCT(BlueprintType)
struct FLayoutPlacedDerivedInternalTraversalLink
{
	GENERATED_BODY()

	/** Stable bridge identifier used by diagnostics and fixture replay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable bridge identifier used by diagnostics and fixture replay."))
	FLayoutId LinkId;

	/** Source occupied local cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Source occupied local cell."))
	FIntVector FromLocalCell = FIntVector::ZeroValue;

	/** Source traversal channel carried by this bridge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Source traversal channel carried by this bridge."))
	FGameplayTag FromTraversalChannel;

	/** Destination occupied local cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Destination occupied local cell."))
	FIntVector ToLocalCell = FIntVector::ZeroValue;

	/** Destination traversal channel carried by this bridge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Destination traversal channel carried by this bridge."))
	FGameplayTag ToTraversalChannel;

	/** If true, traversal may flow in both directions across this local-cell bridge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "If true, traversal may flow in both directions across this local-cell bridge."))
	bool bBidirectional = true;
};

/** One solved module placement in the layout grid. */
USTRUCT(BlueprintType)
struct FLayoutPlacedModule
{
	GENERATED_BODY()

	/** Grid coordinate in cell units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Grid coordinate in cell units."))
	FIntVector Cell = FIntVector::ZeroValue;

	/** Broad planner intent assigned to this cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Broad planner intent assigned to this cell."))
	ELayoutCellIntent Intent = ELayoutCellIntent::Interior;

	/** Module selected for this occupied cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Module selected for this occupied cell."))
	TObjectPtr<ULayoutModuleAsset> Module = nullptr;

	/** Yaw rotation applied to the authored module in clockwise 90-degree steps around Z. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", ClampMax = "3", ToolTip = "Yaw rotation applied to the authored module in clockwise 90-degree steps around Z."))
	int32 YawRotationSteps = 0;

	/** Optional content-entry id that owned this module placement when the solve came from a unified content set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Optional content-entry id that owned this module placement when the solve came from a unified content set."))
	FName SourceContentEntryId;

	/** Snapshot id that selected this placement without requiring a live module asset on worker results. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Snapshot id that selected this placement without requiring a live module asset on worker results."))
	FLayoutId ModuleSnapshotId;

	/** Snapshot index that selected this placement without requiring a live module asset on worker results. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Snapshot index that selected this placement without requiring a live module asset on worker results."))
	int32 ModuleSnapshotIndex = INDEX_NONE;

	/** Soft template path preserved for game-thread realization/rehydration when worker results leave live carriers null. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Soft template path preserved for game-thread realization/rehydration when worker results leave live carriers null."))
	FSoftObjectPath TemplatePath;

	/** Optional composite source retained only while runtime/editor realization migrates onto generic bundle-backed placement identity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Optional composite source retained only while runtime/editor realization migrates onto generic bundle-backed placement identity."))
	TObjectPtr<ULayoutCompositeModuleAsset> CompositeModule = nullptr;

	/** Snapshot-backed bundle bounds in local cells, preserved so later result consumers can rotate occupied local cells without reopening module authoring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Snapshot-backed bundle bounds in local cells, preserved so later result consumers can rotate occupied local cells without reopening module authoring."))
	FIntVector BundleBoundsCells = FIntVector(1, 1, 1);

	/** Snapshot-backed occupied local cells owned by this placement's rigid bundle, preserved so later consumers can keep placement/backtracking atomic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Snapshot-backed occupied local cells owned by this placement's rigid bundle, preserved so later consumers can keep placement/backtracking atomic."))
	TArray<FIntVector> OccupiedLocalCells;

	/** Snapshot-backed exposed face rules for each occupied local cell in the rigid bundle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Snapshot-backed exposed face rules for each occupied local cell in the rigid bundle."))
	TArray<FLayoutPlacedLocalCellFaceRuleSnapshot> LocalCellFaceRules;

	/** Snapshot-backed derived traversal bridges between occupied local cells in the rigid bundle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Snapshot-backed derived traversal bridges between occupied local cells in the rigid bundle."))
	TArray<FLayoutPlacedDerivedInternalTraversalLink> DerivedInternalTraversalLinks;
};

/** Exact counted-feature provider commitment selected by structural search. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutZoneFeatureProviderCommitment
{
	GENERATED_BODY()

	/** Hard requirement satisfied by this provider instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable hard zone-feature requirement id satisfied by this provider instance."))
	FName RequirementId;

	/** Stable provider identity derived from frozen request, region, entry, and bundle-root placement identity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable provider commitment id preserved through merge, artifacts, preview, publication, and realization."))
	FLayoutId ProviderCommitmentId;

	/** Region that owns this direct provider commitment. Descendants do not implicitly credit ancestors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Region debug path that directly owns this provider commitment."))
	FString SourceRegionDebugPath;

	/** Unified content-entry id that supplied this provider. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Unified content-entry id that supplied this counted provider."))
	FName SourceContentEntryId;

	/** Physical bundle-root or direct-child anchor cell used by this one-count commitment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Physical bundle-root or direct-child anchor cell used by this one-count provider commitment."))
	FIntVector Cell = FIntVector::ZeroValue;

	/** Frozen module snapshot identity for module or composite providers. Child providers may leave this empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Frozen module snapshot identity for module or composite providers. Direct-child providers may leave this empty."))
	FLayoutId ModuleSnapshotId;

	/** Authored module level used when this provider earned zone-feature credit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Authored ModuleLevelIndex used when this provider earned zone-feature credit. Physical terrain shifting does not replace this value."))
	int32 ModuleLevelIndex = INDEX_NONE;

	/** Frozen terrain-stage identity for this provider when staged terrain supplied one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Frozen terrain-stage identity for this provider when staged terrain supplied one. INDEX_NONE means no terrain stage applied."))
	int32 TerrainStageIndex = INDEX_NONE;
};

/** Low-overhead counters emitted by solver propagation and backtracking. */
USTRUCT(BlueprintType)
struct FLayoutSolverPropagationStats
{
	GENERATED_BODY()

	/** Number of propagation runs started while solving this layout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of propagation runs started while solving this layout."))
	int32 PropagationRunCount = 0;

	/** Number of propagation fixed-point passes evaluated across all propagation runs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of propagation fixed-point passes evaluated across all propagation runs."))
	int32 PropagationPassCount = 0;

	/** Number of queued cell-neighbor arcs processed during propagation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of queued cell-neighbor arcs processed during propagation."))
	int32 ArcQueuePopCount = 0;

	/** Number of candidate-to-neighbor support checks performed during propagation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of candidate-to-neighbor support checks performed during propagation."))
	int32 SupportCheckCount = 0;

	/** Number of candidates removed from unsolved domains by propagation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of candidates removed from unsolved domains by propagation."))
	int32 CandidateRemovalCount = 0;

	/** Number of cells that failed because propagation emptied their domain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of cells that failed because propagation emptied their domain."))
	int32 FailedCellCount = 0;

	/** Number of candidate placements that were rolled back while searching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of candidate placements that were rolled back while searching."))
	int32 BacktrackCount = 0;

	/** Number of candidate attempts consumed by this solve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of candidate attempts consumed by this solve."))
	int32 CandidateAttemptCount = 0;

	/** Deterministic staged-terrain stage count compiled from the request-owned terrain contract. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic staged-terrain stage count compiled from the request-owned terrain contract."))
	int32 TerrainStageCount = 0;

	/** Deterministic staged-terrain blocked-frontier count compiled from the request-owned terrain contract. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic staged-terrain blocked-frontier count compiled from the request-owned terrain contract."))
	int32 TerrainBlockedFrontierCount = 0;

	/** Number of backtracks attributed to staged-terrain ordering or ownership branches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of backtracks attributed to staged-terrain ordering or ownership branches."))
	int32 TerrainStageBacktrackCount = 0;

	/** Number of hard zone-feature demands compiled once for indexed search. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of hard zone-feature demands compiled once for indexed search."))
	int32 HardZoneFeatureDemandCount = 0;

	/** Number of demand/root provider-capacity entries compiled for hard zone-feature search. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of demand/root provider-capacity entries compiled for hard zone-feature search."))
	int32 HardZoneFeatureProviderRootCount = 0;

	/** Number of branches rejected by hard zone-feature minimum-capacity or maximum-count bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of branches rejected by hard zone-feature minimum-capacity or maximum-count bounds."))
	int32 HardZoneFeatureCountBoundPruneCount = 0;

	/** Number of completed structural proofs rejected because final hard-feature commitments disagreed with authored bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of completed structural proofs rejected because final hard-feature commitments disagreed with authored bounds."))
	int32 HardZoneFeatureLateAuditFailureCount = 0;

	/** Wall-clock seconds spent inside propagation runs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Wall-clock seconds spent inside propagation runs."))
	double PropagationSeconds = 0.0;
};

/** One compiled boundary face segment required by an authored closure contract. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutClosureCoverageSegmentRecord
{
	GENERATED_BODY()

	/** Stable closure requirement name that owns this segment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable closure requirement name that owns this segment."))
	FName ClosureId;

	/** Boundary zone cited by the authored closure requirement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Boundary zone cited by the authored closure requirement."))
	ELayoutPlacementZone BoundaryZone = ELayoutPlacementZone::Perimeter;

	/** Local cell that owns this required boundary face segment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Local cell that owns this required boundary face segment."))
	FIntVector Cell = FIntVector::ZeroValue;

	/** Boundary-facing direction that must be covered on this cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Boundary-facing direction that must be covered on this cell."))
	ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;

	/** Minimum authored closure thickness required for this segment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Minimum authored closure thickness required for this segment."))
	int32 MinThicknessCells = 1;

	/** Whether a compatible provider covered this segment during the solve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Whether a compatible provider covered this segment during the solve."))
	bool bCovered = false;

	/** Stable provider identifier when this segment is covered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable provider identifier when this segment is covered."))
	FLayoutId ProviderId;
};

/** One compiled closure coverage summary produced from authored requirements and solved providers. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutClosureCoverageRecord
{
	GENERATED_BODY()

	/** Stable closure requirement name summarized by this record. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable closure requirement name summarized by this record."))
	FName ClosureId;

	/** Boundary zone cited by the authored closure requirement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Boundary zone cited by the authored closure requirement."))
	ELayoutPlacementZone BoundaryZone = ELayoutPlacementZone::Perimeter;


	/** Number of required boundary face segments compiled for this closure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of required boundary face segments compiled for this closure."))
	int32 RequiredSegmentCount = 0;

	/** Number of required boundary face segments actually covered by compatible providers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of required boundary face segments actually covered by compatible providers."))
	int32 CoveredSegmentCount = 0;

	/** Whether the closure is fully satisfied for the current solve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Whether the closure is fully satisfied for the current solve."))
	bool bSatisfied = false;
};

/** One deterministic continuous run derived from adjacent compiled closure face segments. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutClosureRunRecord
{
	GENERATED_BODY()

	/** Stable closure requirement name summarized by this run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable closure requirement name summarized by this run."))
	FName ClosureId;

	/** Boundary zone cited by the authored closure requirement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Boundary zone cited by the authored closure requirement."))
	ELayoutPlacementZone BoundaryZone = ELayoutPlacementZone::Perimeter;

	/** Boundary-facing direction shared by every segment in this run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Boundary-facing direction shared by every segment in this run."))
	ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;

	/** First cell in deterministic run order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "First cell in deterministic run order."))
	FIntVector StartCell = FIntVector::ZeroValue;

	/** Last cell in deterministic run order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Last cell in deterministic run order."))
	FIntVector EndCell = FIntVector::ZeroValue;

	/** Minimum authored closure thickness required by the segments in this run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Minimum authored closure thickness required by the segments in this run."))
	int32 MinThicknessCells = 1;

	/** Whether a compatible provider covered every segment in this run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Whether a compatible provider covered every segment in this run."))
	bool bCovered = false;

	/** Stable provider identifier when the run is covered by one provider. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable provider identifier when the run is covered by one provider."))
	FLayoutId ProviderId;

	/** Number of adjacent boundary segments represented by this run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of adjacent boundary segments represented by this run."))
	int32 SegmentCount = 0;
};

/** One parent-owned shared seam planned between adjacent regions. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutPartitionSeamRecord
{
	GENERATED_BODY()

	/** Stable seam identifier used by diagnostics and replay fixtures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable seam identifier used by diagnostics and replay fixtures."))
	FLayoutId SeamId;

	/** Parent region that planned and owns this seam contract. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Parent region that planned and owns this seam contract."))
	FString ParentRegionDebugPath;

	/** Region that owns the wall or partition on this shared seam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Region that owns the wall or partition on this shared seam."))
	FString OwnerRegionDebugPath;

	/** Region that accepts the neighboring owned seam instead of placing its own perimeter provider. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Region that accepts the neighboring owned seam instead of placing its own perimeter provider."))
	FString PassiveRegionDebugPath;

	/** Gameplay-tag family used to negotiate this seam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Interface", ToolTip = "Gameplay-tag family used to negotiate this seam."))
	FGameplayTag InterfaceFamily;

	/** Face direction owned by the seam owner across the shared span. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Face direction owned by the seam owner across the shared span."))
	ELayoutFaceDirection OwnerFaceDirection = ELayoutFaceDirection::PosX;

	/** Opposite face direction seen by the passive region across the shared span. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Opposite face direction seen by the passive region across the shared span."))
	ELayoutFaceDirection PassiveFaceDirection = ELayoutFaceDirection::NegX;

	/** First owner-region cell in deterministic seam order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "First owner-region cell in deterministic seam order."))
	FIntVector OwnerStartCell = FIntVector::ZeroValue;

	/** Last owner-region cell in deterministic seam order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Last owner-region cell in deterministic seam order."))
	FIntVector OwnerEndCell = FIntVector::ZeroValue;

	/** First passive-region cell in deterministic seam order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "First passive-region cell in deterministic seam order."))
	FIntVector PassiveStartCell = FIntVector::ZeroValue;

	/** Last passive-region cell in deterministic seam order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Last passive-region cell in deterministic seam order."))
	FIntVector PassiveEndCell = FIntVector::ZeroValue;

	/** Number of aligned adjacent segments represented by this seam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of aligned adjacent segments represented by this seam."))
	int32 SegmentCount = 0;

	/** If true, the passive side may count this seam toward closure satisfaction on the consumed span. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "If true, the passive side may count this seam toward closure satisfaction on the consumed span."))
	bool bCountsTowardClosure = true;

	/** Stable provider identifier when the owning region solved this seam with compatible content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable provider identifier when the owning region solved this seam with compatible content."))
	FLayoutId ProviderId;
};

/** One planned cell left for the residual-placement pass after structural commitments settle. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutResidualCellRecord
{
	GENERATED_BODY()

	/** Residual local cell in layout-cell coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Residual local cell in layout-cell coordinates."))
	FIntVector Cell = FIntVector::ZeroValue;

	/** Planner intent still active on the residual cell after structural solving settled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Planner intent still active on the residual cell after structural solving settled."))
	ELayoutCellIntent Intent = ELayoutCellIntent::Interior;

	/** Why this cell reached the residual-placement pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Why this cell reached the residual-placement pass."))
	ELayoutResidualCellSource Source = ELayoutResidualCellSource::UnoccupiedPlannedCell;

	/** Region debug path that produced this residual cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Region debug path that produced this residual cell."))
	FString SourceRegionDebugPath;

	/** Optional content-entry id that owned the dropped child or structural content request behind this cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Optional content-entry id that owned the dropped child or structural content request behind this cell."))
	FName SourceContentEntryId;

	/** Optional drop-decision id when this residual cell came from an optional child drop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Optional drop-decision id when this residual cell came from an optional child drop."))
	FLayoutId RelatedDropDecisionId;

	/** Typed sparse-placement rule that authored this intentional no-write cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Typed sparse-placement rule that preserved this intentional no-write cell. Empty for ordinary and dropped-child residuals."))
	FName SourceSparsePlacementRuleId;

	/** Authored module level preserved across physical terrain shifts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Authored module level preserved across physical terrain shifts."))
	int32 ModuleLevelIndex = INDEX_NONE;

	/** Final owning-region placement zone compiled from canonical topology support. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Final owning-region placement zone compiled from canonical topology support."))
	ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Interior;

	/** Frozen terrain-stage identity for this physical cell, or INDEX_NONE for ordinary Flat topology. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Frozen terrain-stage identity for this physical cell, or INDEX_NONE for ordinary Flat topology."))
	int32 TerrainStageIndex = INDEX_NONE;
};

/** One deterministic optional-child drop decision recorded after structural commitments settle. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutDroppedOptionalChildRecord
{
	GENERATED_BODY()

	/** Stable drop-decision identifier used by diagnostics and residual records. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable drop-decision identifier used by diagnostics and residual records."))
	FLayoutId DropDecisionId;

	/** Parent region that owned the optional child entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Parent region that owned the optional child entry."))
	FString ParentRegionDebugPath;

	/** Child region that was deterministically dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Child region that was deterministically dropped."))
	FString ChildRegionDebugPath;

	/** Unified content-entry id that authored the optional child. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Unified content-entry id that authored the optional child."))
	FName SourceContentEntryId;

	/** Child profile snapshot id associated with the dropped child request. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Child profile snapshot id associated with the dropped child request."))
	FLayoutId ChildProfileSnapshotId;

	/** Planned child cells that become attributed residual cells when the optional drop is preserved downstream. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Planned child cells that become attributed residual cells when later post-structural rebuild or sparse placement needs to preserve this optional drop without the original child request."))	
	TArray<FLayoutPlannedCell> DroppedPlannedCells;

	/** Related proof/assertion/source ids that explain why the drop was attributed to this child. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Related proof, assertion, or snapshot ids that explain why this optional child drop was attributed to the recorded child."))
	TArray<FLayoutId> RelatedIds;

	/** Human-readable blocking reason captured from the failed child solve or follow-up structural audit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Human-readable blocking reason captured from the failed child solve or follow-up structural audit."))
	FString FailureReason;
};

/** One accepted sparse placement added after structural solving established residual space. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSparsePlacementCommitment
{
	GENERATED_BODY()

	/** Sparse rule that accepted this placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Sparse rule that accepted this placement."))
	FName RuleId;

	/** Content-entry id selected by the sparse pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Content-entry id selected by the sparse pass."))
	FName SourceContentEntryId;

	/** Region debug path that owned this sparse placement pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Region debug path that owned this sparse placement pass."))
	FString SourceRegionDebugPath;

	/** Cell filled by the sparse placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Cell filled by the sparse placement."))
	FIntVector Cell = FIntVector::ZeroValue;

	/** Planner intent that the sparse placement consumed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Planner intent that the sparse placement consumed."))
	ELayoutCellIntent Intent = ELayoutCellIntent::Interior;

	/** Module chosen by the sparse pass for this placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Module chosen by the sparse pass for this placement."))
	TObjectPtr<ULayoutModuleAsset> Module = nullptr;

	/** Yaw rotation applied by the sparse pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", ClampMax = "3", ToolTip = "Yaw rotation applied by the sparse pass."))
	int32 YawRotationSteps = 0;

	/** Every physical cell atomically occupied by this sparse bundle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Every physical cell atomically occupied by this sparse bundle."))
	TArray<FIntVector> OccupiedCells;
};

/** Bounded work counters for one post-structural sparse placement pass. */
USTRUCT(BlueprintType)
struct FLayoutSparsePlacementStats
{
	GENERATED_BODY()

	/** Number of authored sparse rules evaluated in array order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of authored sparse rules evaluated in array order."))
	int32 RuleCount = 0;

	/** Total eligible residual root cells considered across rules. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Total eligible residual root cells considered across rules."))
	int32 EligibleCellCount = 0;

	/** Number of root-cell legal-domain queries performed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of root-cell legal-domain queries performed."))
	int32 LegalCandidateCheckCount = 0;

	/** Number of accepted sparse bundle-root placements. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of accepted sparse bundle-root placements."))
	int32 AcceptedPlacementCount = 0;

	/** Number of physical sparse bundle cells checked for residual authority. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Number of physical sparse bundle cells checked for residual authority."))
	int32 OccupiedCellCheckCount = 0;

	/** Total sparse placement duration in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Total sparse placement duration in seconds."))
	double DurationSeconds = 0.0;
};

/** Structured reason why a route-constrained cell eliminated one candidate during live route-domain filtering. */
UENUM(BlueprintType)
enum class ELayoutRouteDomainFailureKind : uint8
{
	None UMETA(ToolTip = "No route-domain failure kind was recorded."),
	EmptyCandidate UMETA(ToolTip = "The candidate was the explicit empty cell and could not satisfy a route-constrained planned cell."),
	MissingRequiredTraversalChannel UMETA(ToolTip = "The compiled route requirement did not carry a real traversal channel on one required face."),
	MissingFaceRule UMETA(ToolTip = "The candidate did not expose the required face rule on one routed face."),
	MissingFaceTraversalChannel UMETA(ToolTip = "The candidate exposed the face, but that face did not carry the required traversal channel."),
	IncompatibleFaceOccupancy UMETA(ToolTip = "The candidate face could not legally connect to the filled routed neighbor the route requires."),
	MissingInternalTraversalConnectivity UMETA(ToolTip = "The candidate exposed the required routed faces, but it did not connect those route channels internally.")
};

/**
 * Diagnostic summary for the live route-domain filtering pass that constrains
 * planned cells against required traversal faces before recursive solve.
 */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRouteDomainFilterDiagnostics
{
	/** True when the route-domain filter actually processed one or more constraints. */
	bool bApplied = false;

	/** Number of constrained cells visited by the live route-domain filter. */
	int32 ConstrainedCellCount = 0;

	/** Number of boundary-intent constrained cells visited by the live route-domain filter. */
	int32 BoundaryConstrainedCellCount = 0;

	/** Number of boundary-intent constrained cells requiring more than one route face. */
	int32 MultiFaceBoundaryConstrainedCellCount = 0;

	/** Total eligible candidates seen before route filtering across all constrained cells. */
	int32 TotalEligibleCandidateCount = 0;

	/** Total candidates removed by the live route-domain filter across all constrained cells. */
	int32 EliminatedCandidateCount = 0;

	/** Smallest surviving domain size left on any constrained cell after route filtering. */
	int32 TightestRemainingDomainSize = 0;

	/** Number of constrained cells left with exactly one surviving candidate after route filtering. */
	int32 SingleRemainingCandidateCellCount = 0;

	/** Number of constrained cells left with four or fewer surviving candidates after route filtering. */
	int32 AtMostFourRemainingCandidateCellCount = 0;

	/** True when one constrained cell lost every candidate during route filtering. */
	bool bFailedConstraint = false;

	/** Cell whose domain collapsed during live route filtering when bFailedConstraint is true. */
	FIntVector FailedConstraintCell = FIntVector::ZeroValue;

	/** Eligible candidate count on the failed constrained cell before route filtering. */
	int32 FailedConstraintEligibleCandidateCount = 0;

	/** Required face count on the failed constrained cell when bFailedConstraint is true. */
	int32 FailedConstraintRequiredFaceCount = 0;

	/** Dominant structured failure kind on the collapsed constrained cell when bFailedConstraint is true. */
	ELayoutRouteDomainFailureKind FailedConstraintDominantFailureKind = ELayoutRouteDomainFailureKind::None;

	/** Number of eliminated candidates on the collapsed constrained cell matching the dominant structured failure kind. */
	int32 FailedConstraintDominantFailureCount = 0;
};

/** Structured failure stage recorded before recursive CSP candidate search begins. */
UENUM(BlueprintType)
enum class ELayoutSolvePreparationFailureKind : uint8
{
	None UMETA(ToolTip = "No solve-preparation failure was recorded."),
	TraversalTopologyInfeasible UMETA(ToolTip = "Required traversal anchors could not be connected through prepared route topology."),
	SteppedTerrainModuleDomainInfeasible UMETA(ToolTip = "A generated stepped-terrain bridge or retaining cell has no admissible module in the frozen terrain topology."),
	ZoneFeatureProviderCapacityInfeasible UMETA(ToolTip = "A mandatory zone-feature minimum has no precommitted credit or statically compatible provider source in the frozen request.")
};

/** Diagnostic authority available when a rejected solve preview was captured. */
UENUM(BlueprintType)
enum class ELayoutRejectedPreviewStage : uint8
{
	None UMETA(ToolTip = "No rejected preview stage was recorded."),
	PreparedTopology UMETA(ToolTip = "Only finalized planned topology is authoritative for diagnostics; no child transform or module proof was certified."),
	CertifiedChildBranch UMETA(ToolTip = "A child transform, replacement volume, and boundary-domain certificate were frozen before proof failed."),
	ParentProof UMETA(ToolTip = "A certified residual-parent proof returned diagnostic data before the full schedule failed."),
	ChildProof UMETA(ToolTip = "A certified child proof returned diagnostic data before the full schedule failed."),
	MergedProof UMETA(ToolTip = "Parent and child proofs reached merge before final schedule audit failed.")
};

/** Region role that owns a structured recursive solve failure. */
UENUM(BlueprintType)
enum class ELayoutRegionalFailureScope : uint8
{
	None UMETA(ToolTip = "No structured regional failure was recorded."),
	Parent UMETA(ToolTip = "Failure belongs to parent coordination, residual proof, or host authority."),
	Child UMETA(ToolTip = "Failure belongs to one identified child source or regional proof.")
};

/** Pointer-free first-cause failure transported through regional execution, artifacts, and preview diagnostics. */
USTRUCT(BlueprintType)
struct FLayoutRegionalFailureRecord
{
	GENERATED_BODY()

	/** Parent or child scope that owns this failure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Parent or child scope that owns this failure."))
	ELayoutRegionalFailureScope Scope = ELayoutRegionalFailureScope::None;

	/** Stable phase name emitted by the failing authority boundary. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable phase name emitted by the failing authority boundary."))
	FName Phase;

	/** Parent region coordinating the failed branch, when known. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Parent region coordinating the failed branch, when known."))
	FString ParentRegionDebugPath;

	/** Exact failed region path; child failures must identify their child region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Exact failed region path; child failures identify their child region."))
	FString RegionDebugPath;

	/** Content-entry source selected for this regional value, when known. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Content-entry source selected for this regional value, when known."))
	FName SourceContentEntryId;

	/** Stable branch identity used by deferred regional execution, when available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable branch identity used by deferred regional execution, when available."))
	FLayoutId BranchId;

	/** Exact child stage-mapping identity rejected by this branch, when available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Exact child stage-mapping identity rejected by this branch, when available."))
	FLayoutId StageMappingId;

	/** Exact certified child handoff identity rejected by this branch, when available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Exact certified child handoff identity rejected by this branch, when available."))
	FLayoutId CertificateId;

	/** Structured pre-CSP preparation failure retained without parsing text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Structured pre-CSP preparation failure retained without parsing text."))
	ELayoutSolvePreparationFailureKind PreparationFailureKind = ELayoutSolvePreparationFailureKind::None;

	/** Candidate attempts performed by the failing regional phase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Candidate attempts performed by the failing regional phase."))
	int32 CandidateAttemptCount = 0;

	/** Planning variant rejected by the failing regional phase, or INDEX_NONE when unknown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Planning variant rejected by the failing regional phase, or INDEX_NONE when unknown."))
	int32 VariantIndex = INDEX_NONE;

	/** Exact first causal failure emitted by the owning phase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Exact first causal failure emitted by the owning phase."))
	FString FirstCause;

	/** Best later failure retained without replacing FirstCause. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Best later failure retained without replacing FirstCause."))
	FString DownstreamCause;

	/** Returns true when one authority boundary recorded a first cause. */
	bool IsSet() const { return Scope != ELayoutRegionalFailureScope::None && !FirstCause.IsEmpty(); }
};

/** Result of one deterministic offline layout solve. */
USTRUCT(BlueprintType)
struct FLayoutSolveResult
{
	GENERATED_BODY()

	/** True when the solve produced a valid deterministic placement set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when the solve produced a valid deterministic placement set."))
	bool bSucceeded = false;

	/** Seed used for the solve so preview and runtime paths can agree on the same result. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Seed used for the solve so preview and runtime paths can agree on the same result."))
	int32 Seed = 0;

	/** Chosen footprint size in cells for the solved layout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Chosen footprint size in cells for the solved layout."))
	FIntPoint FootprintSize = FIntPoint::ZeroValue;

	/** Normalized shared cell size in blocks that preview, planning, and realization should preserve without rereading live assets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Normalized shared cell size in blocks that preview, planning, and realization should preserve without rereading live assets."))
	FIntVector SharedCellSizeInBlocks = FIntVector::ZeroValue;

	/** Optional authored-block Z offset that preview and realization should apply when stamping solved templates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Optional authored-block Z offset that preview and realization should apply when stamping solved templates."))
	int32 TemplatePlacementZOffsetBlocks = 0;

	/** World-facing placement kind chosen before solve execution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "World-facing placement kind chosen before solve execution."))
	ELayoutWorldBindingPlacementKind RootPlacementKind = ELayoutWorldBindingPlacementKind::None;

	/** World-binding-owned placement policy that realization should honor without rereading live authoring state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "World-binding-owned placement policy that realization should honor without rereading live authoring state."))
	FLayoutWorldBindingPlacementPolicy WorldBindingPlacementPolicy;

	/** Deterministic local solve level whose top surface world-facing terrain alignment should honor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic local solve level whose top surface world-facing terrain alignment should honor. Ordinary roots usually preserve the lowest realized occupied level; continuation roots may preserve the resolved continuation entry level."))
	int32 ResolvedTerrainAlignmentLevel = INDEX_NONE;

	/** Planned broad-intent cells emitted before concrete module selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Planned broad-intent cells emitted before concrete module selection."))
	TArray<FLayoutPlannedCell> PlannedCells;

	/** Final occupied module placements. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Final occupied module placements."))
	TArray<FLayoutPlacedModule> Placements;

	/** Exact hard zone-feature provider commitments selected during structural search. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Exact hard zone-feature provider commitments selected during structural search and preserved through merge, artifacts, preview, publication, and realization."))
	TArray<FLayoutZoneFeatureProviderCommitment> ZoneFeatureProviderCommitments;

	/** Stable child stage-mapping identities selected by recursive coordination; full pointer-free mappings live on the solved artifact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable child stage-mapping identities selected by recursive coordination and preserved through accepted planning payloads."))
	TArray<FLayoutId> ChildStageMappingIds;

	/** Exported local entry-capable cells that later continuation or realization logic may target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Exported local entry-capable cells that later continuation or realization logic may target."))
	TArray<FIntVector> ExportedEntryCells;

	/** Explicit per-cell reservation records compiled before concrete module selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Explicit per-cell reservation records compiled before concrete module selection."))
	TArray<FLayoutCellReservationRecord> CompiledReservations;

	/** Explicit per-cell route constraints compiled before concrete module selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Explicit per-cell route constraints compiled before concrete module selection."))
	TArray<FLayoutRouteConstraintRecord> RouteConstraints;

	/** Request-owned stepped terrain support map preserved on the merged solve surface for preview/runtime stepped diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Request-owned stepped terrain support map preserved on the merged solve surface for preview, runtime, and stepped-diagnostic consumers."))
	FLayoutSteppedTerrainSupportMap SteppedTerrainSupportMap;

	/** Request-owned forced bundle insertions preserved on the merged solve surface for preview/runtime stepped diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Request-owned forced bundle insertions preserved on the merged solve surface for preview, runtime, and stepped-diagnostic consumers."))
	TArray<FLayoutForcedPlacementBundleInsertion> ForcedPlacementBundleInsertions;

	/** Request-owned required route constraints preserved on the merged solve surface for preview/runtime stepped diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Request-owned required route constraints preserved on the merged solve surface for preview, runtime, and stepped-diagnostic consumers."))
	TArray<FLayoutRouteConstraintRecord> RequestOwnedRequiredRouteConstraints;

	/** Request-owned validation assertions preserved on the merged solve surface for preview/runtime contract diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Request-owned validation assertions preserved on the merged solve surface for preview, runtime, and request-contract diagnostic consumers."))
	TArray<FLayoutValidationAssertionRecord> ValidationAssertions;

	/** Public diagnostics for every stepped-terrain transition compiled on the root path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Public diagnostics for every stepped-terrain transition compiled on the root path."))
	TArray<FLayoutSteppedTerrainTransitionDiagnostic> SteppedTerrainTransitionDiagnostics;

	/** Public diagnostics for the stepped-terrain transitions the root path could not currently satisfy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Public diagnostics for the stepped-terrain transitions the root path could not currently satisfy."))
	TArray<FLayoutSteppedTerrainTransitionDiagnostic> UnsupportedSteppedTerrainTransitionDiagnostics;

	/** Deterministic staged-terrain diagnostics grouped by plateau stage and later frontier unlock order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic staged-terrain diagnostics grouped by plateau stage and later frontier unlock order."))
	TArray<FLayoutSteppedTerrainStageDiagnostic> TerrainStageDiagnostics;

	/** Deterministic staged-terrain ownership/support diagnostics keyed by ascent-frontier id. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic staged-terrain ownership/support diagnostics keyed by ascent-frontier id."))
	TArray<FLayoutSteppedTerrainFrontierOwnershipDiagnostic> TerrainFrontierOwnershipDiagnostics;

	/** Future-terrace closure segments preserved only for later proof translation, not current-stage closure ownership. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Future-terrace closure segments preserved only for later proof translation, not current-stage closure ownership."))
	TArray<FLayoutClosureCoverageSegmentRecord> FutureTerraceProofClosureSegments;

	/** True when every public stepped-terrain transition diagnostic was structurally supported on the root path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when every public stepped-terrain transition diagnostic was structurally supported on the root path."))
	bool bCanSatisfySteppedTerrainTransitions = true;

	/** Explicit closure coverage summaries compiled from authored closure requirements and solved providers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Explicit closure coverage summaries compiled from authored closure requirements and solved providers."))
	TArray<FLayoutClosureCoverageRecord> ClosureCoverage;

	/** Explicit boundary face segments used while validating closure coverage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Explicit boundary face segments used while validating closure coverage."))
	TArray<FLayoutClosureCoverageSegmentRecord> ClosureSegments;

	/** Deterministic continuous closure runs derived from adjacent compiled boundary segments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic continuous closure runs derived from adjacent compiled boundary segments."))
	TArray<FLayoutClosureRunRecord> ClosureRuns;

	/** Parent-owned shared seam records evaluated during recursive scheduling or later structural audits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Parent-owned shared seam records evaluated during recursive scheduling or later structural audits."))
	TArray<FLayoutPartitionSeamRecord> PartitionSeams;

	/** Planned cells left for the later residual-placement pass after structural commitments settle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Planned cells left for the later residual-placement pass after structural commitments settle."))
	TArray<FLayoutResidualCellRecord> ResidualUnoccupiedCells;

	/** Explicit optional-child drop decisions recorded before residual placement begins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Explicit optional-child drop decisions recorded before residual placement begins."))
	TArray<FLayoutDroppedOptionalChildRecord> DroppedOptionalChildren;

	/** Sparse placements accepted after structural solving established residual space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Sparse placements accepted after structural solving established residual space."))
	TArray<FLayoutSparsePlacementCommitment> SparsePlacementCommitments;

	/** Bounded work counters from post-structural sparse placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Bounded work counters from post-structural sparse placement."))
	FLayoutSparsePlacementStats SparsePlacementStats;

	/** Diagnostic authority stage retained for a rejected solve preview. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Diagnostic authority stage retained for a rejected solve preview."))
	ELayoutRejectedPreviewStage RejectedPreviewStage = ELayoutRejectedPreviewStage::None;

	/** Stable candidate-domain certificate consumed by this regional proof. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable candidate-domain certificate consumed by this regional proof."))
	FLayoutId CandidateDomainCertificateId;

	/** Exact candidate-domain restriction ids consumed by this regional proof. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Exact candidate-domain restriction ids consumed by this regional proof."))
	TArray<FLayoutId> CandidateDomainRestrictionIds;

	/** Structured pre-CSP failure kind used by bounded policy fallbacks without parsing diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Structured pre-CSP failure kind used by bounded policy fallbacks without parsing diagnostics."))
	ELayoutSolvePreparationFailureKind PreparationFailureKind = ELayoutSolvePreparationFailureKind::None;

	/** Propagation and backtracking counters for profiling solver behavior. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Propagation and backtracking counters for profiling solver behavior."))
	FLayoutSolverPropagationStats PropagationStats;

	/** Off-by-default live route-domain filtering summary used for deeper solver diagnosis. */
	FLayoutRouteDomainFilterDiagnostics RouteDomainFilterDiagnostics;

	/** Validation and solve messages emitted during planning or placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Validation and solve messages emitted during planning or placement."))
	TArray<FLayoutValidationMessage> Messages;

	/** Structured first-cause regional failure shared by synchronous and asynchronous execution paths. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Structured first-cause regional failure shared by synchronous and asynchronous execution paths."))
	FLayoutRegionalFailureRecord RegionalFailure;

	/** Optional failure reason when the solve fails. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Optional failure reason when the solve fails."))
	FString FailureReason;
};

/** Minimal solved-layout payload preserved on resolved site records after deterministic solve. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FResolvedLayoutSiteSolvedPayload
{
	GENERATED_BODY()

	/** Cached solve result used for deterministic realization and connector targeting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Cached solve result used for deterministic realization and connector targeting."))
	FLayoutSolveResult SolveResult;

	/** Exported entry-capable cell positions in local layout-cell coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Exported entry-capable cell positions in local layout-cell coordinates."))
	TArray<FIntVector> ExportedEntryCells;

	/** Connector-family tags exported by this solved site for future inter-layout path generation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Connector-family tags exported by this solved site for future inter-layout path generation."))
	FGameplayTagContainer ExportedConnectorTypeTags;
};

/** Cached deterministic record for one reserved or solved layout site in streamed worlds. */
USTRUCT(BlueprintType)
struct FResolvedLayoutSiteRecord
{
	GENERATED_BODY()

	/** Stable world-space center used to rediscover or realize this layout site later. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable world-space center used to rediscover or realize this layout site later."))
	FIntVector SiteCenterBlockWorldPos = FIntVector::ZeroValue;

	/** Optional site tags used for later connector or future compound filtering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Optional site tags used for later connector or future compound filtering."))
	FGameplayTagContainer SiteTags;

	/** Preserved realized footprint anchor used after terrain fit so downstream anchor reconstruction matches the stamped world location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Preserved realized footprint anchor used after terrain fit so downstream anchor reconstruction matches the stamped world location. Zero means this site has not preserved a terrain-conformed footprint anchor yet."))
	FIntVector RealizedFootprintMinBlockWorldPos = FIntVector::ZeroValue;

	/** Stable world-binding identity that produced this resolved site when it came from world-facing planning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable world-binding identity that produced this resolved site when it came from world-facing planning. Explicit direct-root solves leave this empty."))
	FName WorldBindingId;

	/** Stable weighted-candidate identity selected for this resolved site when it came from world-facing planning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable weighted-candidate identity selected for this resolved site when it came from world-facing planning. Explicit direct-root solves leave this empty."))
	FName WorldBindingCandidateId;

	/** Minimal stable continuation-selection contract preserved from world-facing planning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Minimal stable continuation-selection contract preserved from world-facing planning. Ordinary roots and explicit direct-root solves leave this unset."))
	FLayoutResolvedWorldBindingContinuationSelection ResolvedContinuationSelection;

	/** Stable root solve id preserved from the request/publication surface for later cache, replay, and trace correlation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable root solve id preserved from the request/publication surface for later cache, replay, and trace correlation. Planning-window imports keep the accepted stable record identity here, and explicit direct-root solves keep their deterministic direct-root request id."))
	FLayoutId RootSolveId;

	/** Stable root candidate id preserved from the request/publication surface for later cache, replay, and trace correlation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable root candidate id preserved from the request/publication surface for later cache, replay, and trace correlation. Planning-window imports keep the accepted selected candidate id here, and explicit direct-root solves keep their deterministic direct-root request id."))
	FLayoutId RootCandidateId;

	/** Stable root placement-policy id preserved from the request/publication surface for later cache, replay, and world-binding trace correlation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable root placement-policy id preserved from the request/publication surface for later cache, replay, and world-binding trace correlation. Planning-window imports keep the accepted world-binding policy owner here, and explicit direct-root solves keep the deterministic direct-root placement-policy id."))
	FLayoutId RootPlacementPolicyId;

	/** Project-authored biome row selected for this resolved site when it came from planning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Project-authored biome row selected for this resolved site when one specific live row was resolved. Explicit direct-root solves may leave this empty when the binding supports multiple compatible rows."))
	FName BiomeRowName;

	/** Compatible biome/domain rows preserved for resolved-site terrain-fit allow-list replay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Compatible biome/domain row names preserved for resolved-site terrain-fit allow-list replay. Explicit direct-root solves keep the owning binding's compatible row list here when terrain fit should accept any matching live row instead of one specific authored row."))
	TArray<FName> CompatibleBiomeRowNames;

	/** True when resolved-site terrain fit should use any active biome surface instead of a specific owned row filter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when resolved-site terrain fit should use any active biome surface instead of replaying one specific owned biome row or allow-list. Explicit direct-root tool previews use this when the selected world binding contributes configuration and lattice policy but should not hard-filter the manually chosen location by one authored biome row list."))
	bool bUseAnyActiveBiomeSurface = false;

	/** Unified content set chosen for this site when the authored profile uses the final content-entry model. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Unified content set chosen for this site when the authored profile uses the final content-entry model."))
	TSoftObjectPtr<ULayoutRegionContentSetAsset> ContentSet;

	/** Layout profile chosen for this site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Layout profile chosen for this site."))
	TSoftObjectPtr<ULayoutProfileAsset> LayoutProfile;

	/** Returns the minimal world-binding frontend carrier preserved on this resolved site record. */
	FLayoutWorldBindingSiteFrontendSelection GetWorldBindingFrontendSelection() const
	{
		FLayoutWorldBindingSiteFrontendSelection Selection;
		Selection.WorldBindingId = WorldBindingId;
		Selection.WorldBindingCandidateId = WorldBindingCandidateId;
		Selection.ResolvedContinuationSelection = ResolvedContinuationSelection;
		Selection.BiomeRowName = BiomeRowName;
		Selection.CompatibleBiomeRowNames = CompatibleBiomeRowNames;
		Selection.bUseAnyActiveBiomeSurface = bUseAnyActiveBiomeSurface;
		return Selection;
	}

	/** Replaces the minimal world-binding frontend carrier preserved on this resolved site record. */
	void SetWorldBindingFrontendSelection(const FLayoutWorldBindingSiteFrontendSelection& InSelection)
	{
		WorldBindingId = InSelection.WorldBindingId;
		WorldBindingCandidateId = InSelection.WorldBindingCandidateId;
		ResolvedContinuationSelection = InSelection.ResolvedContinuationSelection;
		BiomeRowName = InSelection.BiomeRowName;
		CompatibleBiomeRowNames = InSelection.CompatibleBiomeRowNames;
		bUseAnyActiveBiomeSurface = InSelection.bUseAnyActiveBiomeSurface;
	}

	/** Returns the minimal root publication carrier preserved on this resolved site record. */
	FLayoutRootPublicationMetadata GetRootPublicationMetadata() const
	{
		FLayoutRootPublicationMetadata Metadata;
		Metadata.RootSolveId = RootSolveId;
		Metadata.RootCandidateId = RootCandidateId;
		Metadata.RootPlacementPolicyId = RootPlacementPolicyId;
		return Metadata;
	}

	/** Replaces the minimal root publication carrier preserved on this resolved site record. */
	void SetRootPublicationMetadata(const FLayoutRootPublicationMetadata& InMetadata)
	{
		RootSolveId = InMetadata.RootSolveId;
		RootCandidateId = InMetadata.RootCandidateId;
		RootPlacementPolicyId = InMetadata.RootPlacementPolicyId;
	}

	/** Returns the minimal authored solve-source carrier preserved on this resolved site record. */
	FLayoutSiteSolveSourceSelection GetSiteSolveSourceSelection() const
	{
		FLayoutSiteSolveSourceSelection SourceSelection;
		SourceSelection.LayoutProfile = LayoutProfile;
		SourceSelection.ContentSet = ContentSet;
		SourceSelection.ExportedConnectorTypeTags = ExportedConnectorTypeTags;
		SourceSelection.SolveSeed = SolveSeed;
		return SourceSelection;
	}

	/** Replaces the minimal authored solve-source carrier preserved on this resolved site record. */
	void SetSiteSolveSourceSelection(const FLayoutSiteSolveSourceSelection& InSelection)
	{
		LayoutProfile = InSelection.LayoutProfile;
		ContentSet = InSelection.ContentSet;
		ExportedConnectorTypeTags = InSelection.ExportedConnectorTypeTags;
		SolveSeed = InSelection.SolveSeed;
	}

	/** Returns the minimal runtime-only lifecycle carrier preserved on this resolved site record. */
	FResolvedLayoutSiteRuntimeState GetResolvedSiteRuntimeState() const
	{
		FResolvedLayoutSiteRuntimeState RuntimeState;
		RuntimeState.bLayoutSolved = bLayoutSolved;
		RuntimeState.CachedApplyability = CachedApplyability;
		RuntimeState.bLayoutRealized = bLayoutRealized;
		RuntimeState.bHasBeenCommittedToChunkWorld = bHasBeenCommittedToChunkWorld;
		RuntimeState.TerrainFitDiagnosticKind = TerrainFitDiagnosticKind;
		return RuntimeState;
	}

	/** Replaces the minimal runtime-only lifecycle carrier preserved on this resolved site record. */
	void SetResolvedSiteRuntimeState(const FResolvedLayoutSiteRuntimeState& InState)
	{
		bLayoutSolved = InState.bLayoutSolved;
		CachedApplyability = InState.CachedApplyability;
		bLayoutRealized = InState.bLayoutRealized;
		bHasBeenCommittedToChunkWorld = InState.bHasBeenCommittedToChunkWorld;
		TerrainFitDiagnosticKind = InState.TerrainFitDiagnosticKind;
	}

	/** Returns the minimal stable location payload preserved on this resolved site record. */
	FResolvedLayoutSiteLocationMetadata GetResolvedSiteLocationMetadata() const
	{
		FResolvedLayoutSiteLocationMetadata Metadata;
		Metadata.SiteCenterBlockWorldPos = SiteCenterBlockWorldPos;
		Metadata.RealizedFootprintMinBlockWorldPos = RealizedFootprintMinBlockWorldPos;
		Metadata.SiteTags = SiteTags;
		return Metadata;
	}

	/** Replaces the minimal stable location payload preserved on this resolved site record. */
	void SetResolvedSiteLocationMetadata(const FResolvedLayoutSiteLocationMetadata& InMetadata)
	{
		SiteCenterBlockWorldPos = InMetadata.SiteCenterBlockWorldPos;
		RealizedFootprintMinBlockWorldPos = InMetadata.RealizedFootprintMinBlockWorldPos;
		SiteTags = InMetadata.SiteTags;
	}

	/** Returns the minimal solved-layout payload preserved on this resolved site record. */
	FResolvedLayoutSiteSolvedPayload GetResolvedSiteSolvedPayload() const
	{
		FResolvedLayoutSiteSolvedPayload Payload;
		Payload.SolveResult = SolveResult;
		Payload.ExportedEntryCells = ExportedEntryCells;
		Payload.ExportedConnectorTypeTags = ExportedConnectorTypeTags;
		return Payload;
	}

	/** Replaces the minimal solved-layout payload preserved on this resolved site record. */
	void SetResolvedSiteSolvedPayload(const FResolvedLayoutSiteSolvedPayload& InPayload)
	{
		SolveResult = InPayload.SolveResult;
		ExportedEntryCells = InPayload.ExportedEntryCells;
		ExportedConnectorTypeTags = InPayload.ExportedConnectorTypeTags;
	}

	/** Deterministic solve seed used for this site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic solve seed used for this site."))
	int32 SolveSeed = 0;

	/** True once a deterministic full solve result has been cached for this reserved site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True once a deterministic full solve result has been cached for this reserved site. Certified partial results remain false."))
	bool bLayoutSolved = false;

	/** Immutable publication outcome that authorizes cached realization without rewriting the solve verdict. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Cached applyability outcome. Certified Partial may realize independently certified placements while preserving the rejected full solve."))
	ELayoutCachedApplyability CachedApplyability = ELayoutCachedApplyability::Rejected;

	/** True once the solved site has been stamped into the chunk world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True once the solved site has been stamped into the chunk world."))
	bool bLayoutRealized = false;

	/** Once realized into the chunk world, do not stamp again unless an explicit regeneration path opts in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Once realized into the chunk world, do not stamp again unless an explicit regeneration path opts in."))
	bool bHasBeenCommittedToChunkWorld = false;

	/** Cached solve result used for deterministic realization and connector targeting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Cached solve result used for deterministic realization and connector targeting."))
	FLayoutSolveResult SolveResult;

	/** Stable solved artifact id accepted by publication before realization-prep consumes the legacy solve payload. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable solved artifact id accepted by publication before realization-prep consumes the legacy solve payload."))
	FLayoutId SolvedArtifactId;

	/** Number of active cells validated on the accepted solved artifact before downstream writes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Number of active cells validated on the accepted solved artifact before downstream writes."))
	int32 SolvedArtifactActiveCellCount = 0;

	/** Exported entry-capable cell positions in local layout-cell coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Exported entry-capable cell positions in local layout-cell coordinates."))
	TArray<FIntVector> ExportedEntryCells;

	/** Connector-family tags exported by this solved site for future inter-layout path generation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Connector-family tags exported by this solved site for future inter-layout path generation."))
	FGameplayTagContainer ExportedConnectorTypeTags;

	/** Latest structured terrain-fit outcome preserved during runtime realization for this site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Latest structured terrain-fit outcome preserved during runtime realization for this site. World-binding ordinary-root realization can distinguish accepted flat/fill/ramp behavior from rejected bridge-grade or depth-overrun terrain outcomes without relying only on failure strings."))
	ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind =
		ELayoutWorldBindingTerrainFitDiagnosticKind::None;

	/** True when realization-prep output has been durably computed and cached for this site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when realization-prep output has been durably computed and cached for this site. ApplyCachedSolve consumes this instead of rebuilding."))
	bool bWritePlanReady = false;

	/** Stable realization write-plan id produced at accepted-solve publication. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable realization write-plan id produced at accepted-solve publication from the solved artifact and frozen terrain contract."))
	FLayoutId CachedRealizationWritePlanId;

	/** Deterministic metadata hash of the accepted realization write plan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic metadata hash of the accepted realization write plan. Apply rejects stale cached realization-prep records before terrain/template writes."))
	int32 CachedRealizationWritePlanHash = 0;

	/** Provenance marker tying the cached realization-prep output back to the accepted root solve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Provenance marker tying the cached realization-prep output back to the accepted root solve so ApplyCachedSolve can reject stale or synthesized authority."))
	FLayoutId CachedRealizationProvenanceId;

	/** Idempotency marker for replaying the same accepted write plan without duplicating or drifting writes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Idempotency marker for replaying the same accepted write plan without duplicating or drifting terrain/template writes."))
	FLayoutId CachedRealizationIdempotencyMarker;

	/** Frozen terrain contract id produced by the adapter during pre-solve, cached for realization-prep replay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Frozen terrain contract id produced by the adapter during pre-solve, cached for realization-prep replay. ApplyCachedSolve uses this to construct the write plan without rebuilding a compatibility contract."))
	FLayoutId CachedFrozenTerrainContractId;

	/** Per-column absolute base block-world Z (ResolvedStageBaseBlockWorldZ) cached from the precomputed frozen contract for debug overlay cell positioning. Key is footprint-local (X,Y). Not serialized. */
	TMap<FIntPoint, int32> CachedFrozenTerrainBaseZByColumn;

	/** Number of chunk write-batch passes in the cached write plan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Number of chunk write-batch passes in the cached realization-prep output."))
	int32 CachedChunkWritePassCount = 0;

	/** Terrain subplan count from the cached write plan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Terrain subplan write count from the cached realization-prep output."))
	int32 CachedTerrainWriteCount = 0;

	/** Template subplan count from the cached write plan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Template subplan placement count from the cached realization-prep output."))
	int32 CachedTemplatePlacementCount = 0;

};

/** One exported endpoint candidate used while pairing site layouts into connector layouts. */
USTRUCT(BlueprintType)
struct FResolvedLayoutConnectorEndpoint
{
	GENERATED_BODY()

	/** Coarse reservation key of the site that exported this endpoint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Coarse reservation key of the site that exported this endpoint."))
	FIntPoint SiteReservationKey = FIntPoint::ZeroValue;

	/** Stable root record identity of the site that exported this endpoint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable root record identity of the site that exported this endpoint. This distinguishes roots that share one coarse reservation cell."))
	FString RootRecordKey;

	/** World-space center block position of the site that exported this endpoint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "World-space center block position of the site that exported this endpoint."))
	FIntVector SiteCenterBlockWorldPos = FIntVector::ZeroValue;

	/** Local solved cell exported as an entry-capable endpoint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Local solved cell exported as an entry-capable endpoint."))
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Approximate world-space center of this endpoint used for connector pairing and path planning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Approximate world-space center of this endpoint used for connector pairing and path planning."))
	FIntVector EndpointBlockWorldPos = FIntVector::ZeroValue;

	/** Connector family exported by the owning site for this endpoint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Connector", ToolTip = "Connector family exported by the owning site for this endpoint."))
	FGameplayTag ConnectorTypeTag;

	/** Outward face direction of the exported entry cell, as solved by the owning site. Used by the prewarm for inter-layout face-rule validation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Outward face direction of the exported entry cell, as solved by the owning site. Used by the prewarm for inter-layout face-rule validation."))
	ELayoutFaceDirection ExposedEntryFaceDirection = ELayoutFaceDirection::PosX;
};

/** Lifecycle state for one bounded continuation layout inside an inter-root route. */
UENUM(BlueprintType)
enum class ELayoutContinuationSegmentState : uint8
{
	Planned,
	PendingTerrain,
	Queued,
	Solved,
	Partial,
	WaitingForChunks,
	Committed,
	Failed
};

/** Compact route-owned descriptor for one bounded continuation layout. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContinuationSegmentDescriptor
{
	GENERATED_BODY()

	/** Stable ordinal used to order segment solve, preview, and realization work. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	int32 SegmentIndex = INDEX_NONE;

	/** Inclusive indices into the route centerline owned by this segment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	int32 FirstRouteCellIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	int32 LastRouteCellIndex = INDEX_NONE;

	/** Family candidate selected for this bounded segment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	FName CandidateId;

	/** Candidate profile retained for deterministic request reconstruction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	TSoftObjectPtr<ULayoutProfileAsset> LayoutProfile;

	/** Normalized segment footprint and route-cell minimum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	FIntPoint FootprintSize = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	FIntPoint MinRouteCell = FIntPoint::ZeroValue;

	/** Explicit segment ingress and egress anchors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	FIntVector IngressCell = FIntVector::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	FIntVector EgressCell = FIntVector::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	ELayoutFaceDirection IngressFaceDirection = ELayoutFaceDirection::PosX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	ELayoutFaceDirection EgressFaceDirection = ELayoutFaceDirection::PosX;

	/** Current independent solve and realization lifecycle state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	ELayoutContinuationSegmentState State = ELayoutContinuationSegmentState::Planned;
};

/** Compact route record retained between two exported root endpoints. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContinuationRouteRecord
{
	GENERATED_BODY()

	/** Stable identity shared by every segment generated from one reserved root edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	FString RouteKey;

	/** World-binding and family selected for this route. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	FName WorldBindingId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	FName ContinuationFamilyId;

	/** Root-owned endpoints remain immutable route terminals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	FResolvedLayoutConnectorEndpoint StartRootEndpoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	FResolvedLayoutConnectorEndpoint EndRootEndpoint;

	/** Ordered cardinal centerline retained without solved placement artifacts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	TArray<FIntPoint> CenterlineCells;

	/** Ordered bounded descriptors generated from CenterlineCells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Continuation")
	TArray<FLayoutContinuationSegmentDescriptor> Segments;
};

/** Minimal stable world-binding frontend carrier preserved on resolved connector records. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutResolvedConnectorTerrainPathSelection
{
	GENERATED_BODY()

	/** If true, the connector should prefer terrain-aware path sampling instead of the fallback L-corridor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "If true, the connector should prefer terrain-aware path sampling instead of the fallback L-corridor."))
	bool bEnableTerrainAwarePathing = true;

	/** Shared biome row preserved for same-biome connector routing when one row alone owns the active path surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Shared biome row preserved for same-biome connector routing when one row alone owns the active path surface."))
	FName PathBiomeRowName;

	/** Biome-row allow-list preserved for cross-biome connector routing when multiple authored rows may own the active path surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Biome-row allow-list preserved for cross-biome connector routing when multiple authored rows may own the active path surface."))
	TArray<FName> PathBiomeRowNames;
};

/** Minimal stable world-binding frontend carrier preserved on resolved connector records. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutResolvedConnectorFrontendSelection
{
	GENERATED_BODY()

	/** Stable world-binding identity that owns this connector-facing frontend selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable world-binding identity that owns this connector-facing frontend selection. Legacy direct-root connector records may leave this empty."))
	FName WorldBindingId;

	/** Project-authored biome row selected for this connector-facing frontend selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Project-authored biome row selected for this connector-facing frontend selection. Cross-biome fallback records may leave this empty when no shared row survives."))
	FName BiomeRowName;

	/** Stable authored continuation-family identity selected for this connector-facing frontend selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable authored continuation-family identity selected for this connector-facing frontend selection. Legacy direct-root connector records may leave this empty."))
	FName ContinuationFamilyId;

	/** Stable authored continuation-family candidate identity selected for this connector-facing frontend selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable authored continuation-family candidate identity selected for this connector-facing frontend selection. Legacy direct-root connector records may leave this empty."))
	FName ContinuationFamilyCandidateId;

	/** Deterministic world-facing placement kind selected for this connector-facing frontend selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic world-facing placement kind selected for this connector-facing frontend selection."))
	ELayoutWorldBindingPlacementKind PlacementKind = ELayoutWorldBindingPlacementKind::None;

	/** Minimal stable continuation-selection contract preserved on this connector-facing frontend selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Minimal stable continuation-selection contract preserved on this connector-facing frontend selection."))
	FLayoutResolvedWorldBindingContinuationSelection ResolvedContinuationSelection;

	/** Resolved terrain-path row policy preserved on this connector-facing frontend selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Resolved terrain-path row policy preserved on this connector-facing frontend selection so continuation-family routing can rebuild terrain-aware connector settings without reopening transient site-pair context."))
	FLayoutResolvedConnectorTerrainPathSelection TerrainPathSelection;

	/** World-binding placement policy preserved on this connector-facing frontend selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "World-binding placement policy preserved on this connector-facing frontend selection."))
	FLayoutWorldBindingPlacementPolicy WorldBindingPlacementPolicy;

	/** Continuation-family path policy preserved on this connector-facing frontend selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Continuation-family path policy preserved on this connector-facing frontend selection."))
	FLayoutWorldBindingContinuationPolicy ContinuationPolicy;

	/** Solve budget preserved on this connector-facing frontend selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Solve budget preserved on this connector-facing frontend selection."))
	FLayoutRootSolveBudgetSettings SolveBudget;

	/** Binding-owned shared cell size preserved on this connector-facing frontend selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Binding-owned shared cell size preserved on this connector-facing frontend selection so runtime or planning fallback seams can rebuild one connector request without reopening live content-set or module-set compatibility metrics."))
	FIntVector SharedCellSizeInBlocks = FIntVector::ZeroValue;

	/** Binding-owned template placement offset preserved on this connector-facing frontend selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Binding-owned template placement offset preserved on this connector-facing frontend selection so runtime or planning fallback seams can rebuild one connector request without reopening live authoring state."))
	int32 TemplatePlacementZOffsetBlocks = 0;
};

/** Minimal authored solve-source carrier preserved on resolved connector records. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutResolvedConnectorSolveSourceSelection
{
	GENERATED_BODY()

	/** Layout profile selected for this connector solve source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Layout profile selected for this connector solve source."))
	TSoftObjectPtr<ULayoutProfileAsset> LayoutProfile;

	/** Unified content set selected for this connector solve source when the profile uses the final authored content-entry model. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Unified content set selected for this connector solve source when the profile uses the final authored content-entry model."))
	TSoftObjectPtr<ULayoutRegionContentSetAsset> ContentSet;

	/** Deterministic solve seed assigned to this connector solve source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic solve seed assigned to this connector solve source."))
	int32 SolveSeed = 0;
};

/** Minimal runtime-only lifecycle carrier preserved on resolved connector records after solve. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FResolvedLayoutConnectorRuntimeState
{
	GENERATED_BODY()

	/** True once a deterministic connector solve result has been cached. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True once a deterministic connector solve result has been cached."))
	bool bLayoutSolved = false;

	/** True once the solved connector has been stamped into the chunk world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True once the solved connector has been stamped into the chunk world."))
	bool bLayoutRealized = false;

	/** Once realized into the chunk world, do not stamp again unless an explicit regeneration path opts in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Once realized into the chunk world, do not stamp again unless an explicit regeneration path opts in."))
	bool bHasBeenCommittedToChunkWorld = false;

	/** Latest structured terrain-fit outcome preserved during runtime realization for this connector root. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Latest structured terrain-fit outcome preserved during runtime realization for this connector root."))
	ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind =
		ELayoutWorldBindingTerrainFitDiagnosticKind::None;
};

/** Cached deterministic record for one reserved or solved inter-layout connector. */
USTRUCT(BlueprintType)
struct FResolvedLayoutConnectorRecord
{
	GENERATED_BODY()

	/** Stable coarse reservation key of the starting site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable coarse reservation key of the starting site."))
	FIntPoint StartSiteReservationKey = FIntPoint::ZeroValue;

	/** Stable coarse reservation key of the destination site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable coarse reservation key of the destination site."))
	FIntPoint EndSiteReservationKey = FIntPoint::ZeroValue;

	/** Stable authored world-binding identity selected for this inter-layout path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable authored world-binding identity selected for this inter-layout path. This lets later runtime/frontend rebuilds locate the same binding contract without inferring it only from the paired sites."))
	FName WorldBindingId;

	/** Shared biome row used when this connector path was selected under one authored world-binding family. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Shared biome row used when this connector path was selected under one authored world-binding family. This stays separate from later cross-biome continuation policy so same-biome authored continuation requests can rebuild their runtime view without guessing the row context."))
	FName BiomeRowName;

	/** Connector family used to solve this inter-layout path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Categories = "Layout.Connector", ToolTip = "Connector family used to solve this inter-layout path."))
	FGameplayTag ConnectorTypeTag;

	/** Authored continuation family selected for this inter-layout path on the world-binding side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Authored continuation family selected for this inter-layout path on the world-binding side. Legacy row-driven connector paths may leave this empty."))
	FName ContinuationFamilyId;

	/** Authored continuation-family candidate selected for this inter-layout path on the world-binding side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Authored continuation-family candidate selected for this inter-layout path on the world-binding side. This stays separate from the deterministic connector request/publication ids so later continuation-family request cutover does not have to overload the connector solve id as candidate identity."))
	FName ContinuationFamilyCandidateId;

	/** Stable route identity shared by every bounded segment of one reserved root edge. Empty for an unsplit record. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable route identity shared by every bounded segment of one reserved root edge. Empty for an unsplit record."))
	FLayoutId ContinuationRouteId;

	/** Ordered bounded segment identity inside ContinuationRouteId. INDEX_NONE identifies an unsplit record. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Ordered bounded segment identity inside the continuation route. INDEX_NONE identifies an unsplit record."))
	int32 ContinuationSegmentIndex = INDEX_NONE;

	/** Deterministic world-facing placement kind selected for this inter-layout path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic world-facing placement kind selected for this inter-layout path."))
	ELayoutWorldBindingPlacementKind PlacementKind = ELayoutWorldBindingPlacementKind::None;

	/** Deterministic continuation-selection contract selected for this inter-layout path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic continuation-selection contract selected for this inter-layout path. Legacy row-driven connector paths may leave this empty."))
	FLayoutResolvedWorldBindingContinuationSelection ResolvedContinuationSelection;

	/** Resolved terrain-path row policy selected for this inter-layout path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Resolved terrain-path row policy selected for this inter-layout path. This lets later connector request rebuilding keep the same authored biome-row allow-list instead of depending on transient site-pair context."))
	FLayoutResolvedConnectorTerrainPathSelection TerrainPathSelection;

	/** World-binding placement policy selected for this inter-layout path when the connector already carries explicit world-binding frontend context. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "World-binding placement policy selected for this inter-layout path when the connector already carries explicit world-binding frontend context. Legacy connector fallback request assembly does not treat the nested default policy state by itself as authoritative carrier ownership."))
	FLayoutWorldBindingPlacementPolicy WorldBindingPlacementPolicy;

	/** Authored continuation-family policy selected for this inter-layout path when the connector already carries explicit world-binding frontend context. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Authored continuation-family policy selected for this inter-layout path when the connector already carries explicit world-binding frontend context. This lets later runtime/frontend rebuilds keep continuation path limits without requiring the live binding UObject."))
	FLayoutWorldBindingContinuationPolicy ContinuationPolicy;

	/** Root solve budget selected for this inter-layout path when the connector already carries explicit world-binding frontend context. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Root solve budget selected for this inter-layout path when the connector already carries explicit world-binding frontend context. Fallback request assembly uses the caller budget when no explicit connector frontend carrier is present."))
	FLayoutRootSolveBudgetSettings SolveBudget;

	/** Binding-owned shared cell size preserved on this inter-layout frontend carrier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Binding-owned shared cell size preserved on this inter-layout frontend carrier so thin cached connector paths can rebuild requests without reopening live content-set or module-set compatibility metrics."))
	FIntVector FrontendSharedCellSizeInBlocks = FIntVector::ZeroValue;

	/** Binding-owned template placement offset preserved on this inter-layout frontend carrier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Binding-owned template placement offset preserved on this inter-layout frontend carrier so thin cached connector paths can rebuild requests without reopening live authoring state."))
	int32 FrontendTemplatePlacementZOffsetBlocks = 0;

	/** Stable root solve id preserved from the connector request/publication surface for cache, replay, and trace correlation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable root solve id preserved from the connector request/publication surface for cache, replay, and trace correlation."))
	FLayoutId RootSolveId;

	/** Stable root candidate id preserved from the connector request/publication surface for cache, replay, and trace correlation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable root candidate id preserved from the connector request/publication surface for cache, replay, and trace correlation."))
	FLayoutId RootCandidateId;

	/** Stable root placement-policy id preserved from the connector request/publication surface for later world-binding trace correlation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable root placement-policy id preserved from the connector request/publication surface for later world-binding trace correlation."))
	FLayoutId RootPlacementPolicyId;

	/** Approximate world-space starting endpoint used to plan this connector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Approximate world-space starting endpoint used to plan this connector."))
	FIntVector StartEndpointBlockWorldPos = FIntVector::ZeroValue;

	/** Approximate world-space destination endpoint used to plan this connector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Approximate world-space destination endpoint used to plan this connector."))
	FIntVector EndEndpointBlockWorldPos = FIntVector::ZeroValue;

	/** True when the resolved connector record preserves explicit outward endpoint-facing directions instead of relying on reconstructed path deltas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when the resolved connector record preserves explicit outward endpoint-facing directions instead of relying on reconstructed path deltas."))
	bool bHasResolvedEndpointFacingDirections = false;

	/** Outward connector-facing direction at the starting endpoint cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Outward connector-facing direction at the starting endpoint cell."))
	ELayoutFaceDirection StartEndpointFacingDirection = ELayoutFaceDirection::PosX;

	/** Outward connector-facing direction at the destination endpoint cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Outward connector-facing direction at the destination endpoint cell."))
	ELayoutFaceDirection EndEndpointFacingDirection = ELayoutFaceDirection::PosX;

	/** Outward entry face direction of the root layout's exported start entry cell, as solved by the owning site. Used for inter-layout face-rule validation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Outward entry face direction of the root layout's exported start entry cell, as solved by the owning site. Used for inter-layout face-rule validation."))
	ELayoutFaceDirection StartRootEntryFaceDirection = ELayoutFaceDirection::PosX;

	/** Outward entry face direction of the root layout's exported end entry cell, as solved by the owning site. Used for inter-layout face-rule validation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Outward entry face direction of the root layout's exported end entry cell, as solved by the owning site. Used for inter-layout face-rule validation."))
	ELayoutFaceDirection EndRootEntryFaceDirection = ELayoutFaceDirection::PosX;

	/** Solved start-endpoint boundary strip preserved as block-world XY columns for later ingress shaping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Solved start-endpoint boundary strip preserved as block-world XY columns for later ingress shaping."))
	TArray<FIntPoint> StartEndpointBoundaryStripBlockXY;

	/** Solved end-endpoint boundary strip preserved as block-world XY columns for later ingress shaping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Solved end-endpoint boundary strip preserved as block-world XY columns for later ingress shaping."))
	TArray<FIntPoint> EndEndpointBoundaryStripBlockXY;

	/** Block-world origin of the solved connector footprint; placement cells are stamped relative to this path origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Block-world origin of the solved connector footprint; placement cells are stamped relative to this path origin."))
	FIntVector PathOriginBlockWorldPos = FIntVector::ZeroValue;

	/** Unified content set chosen for this connector path when the authored profile uses the final content-entry model. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Unified content set chosen for this connector path when the authored profile uses the final content-entry model."))
	TSoftObjectPtr<ULayoutRegionContentSetAsset> ContentSet;

	/** Connector layout profile chosen for this path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Connector layout profile chosen for this path."))
	TSoftObjectPtr<ULayoutProfileAsset> LayoutProfile;

	/** Deterministic solve seed used for this connector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Deterministic solve seed used for this connector."))
	int32 SolveSeed = 0;

	/** True once the connector solve result has been cached. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True once the connector solve result has been cached."))
	bool bLayoutSolved = false;

	/** True once the connector has been stamped into the chunk world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True once the connector has been stamped into the chunk world."))
	bool bLayoutRealized = false;

	/** Once realized into the chunk world, do not stamp again unless an explicit regeneration path opts in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Once realized into the chunk world, do not stamp again unless an explicit regeneration path opts in."))
	bool bHasBeenCommittedToChunkWorld = false;

	/** Cached connector solve result used for incremental chunk-load realization. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Cached connector solve result used for incremental chunk-load realization."))
	FLayoutSolveResult SolveResult;

	/** Stable solved artifact id accepted by connector publication before realization-prep consumes the legacy solve payload. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Stable solved artifact id accepted by connector publication before realization-prep consumes the legacy solve payload."))
	FLayoutId SolvedArtifactId;

	/** Number of active cells validated on the accepted connector solved artifact before downstream writes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Number of active cells validated on the accepted connector solved artifact before downstream writes."))
	int32 SolvedArtifactActiveCellCount = 0;

	/** Latest structured terrain-fit outcome preserved during runtime realization for this connector root. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Latest structured terrain-fit outcome preserved during runtime realization for this connector root. SurfacePath realization can distinguish accepted flat/fill/ramp behavior from rejected bridge-grade or depth-overrun terrain outcomes without relying only on failure strings."))
	ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind =
		ELayoutWorldBindingTerrainFitDiagnosticKind::None;

	/** Returns the minimal world-binding frontend carrier preserved on this resolved connector record. */
	FLayoutResolvedConnectorFrontendSelection GetResolvedConnectorFrontendSelection() const
	{
		FLayoutResolvedConnectorFrontendSelection Selection;
		Selection.WorldBindingId = WorldBindingId;
		Selection.BiomeRowName = BiomeRowName;
		Selection.ContinuationFamilyId = ContinuationFamilyId;
		Selection.ContinuationFamilyCandidateId = ContinuationFamilyCandidateId;
		Selection.PlacementKind = PlacementKind;
		Selection.ResolvedContinuationSelection = ResolvedContinuationSelection;
		Selection.TerrainPathSelection = TerrainPathSelection;
		Selection.WorldBindingPlacementPolicy = WorldBindingPlacementPolicy;
		Selection.ContinuationPolicy = ContinuationPolicy;
		Selection.SolveBudget = SolveBudget;
		Selection.SharedCellSizeInBlocks = FrontendSharedCellSizeInBlocks;
		Selection.TemplatePlacementZOffsetBlocks = FrontendTemplatePlacementZOffsetBlocks;
		return Selection;
	}

	/** Replaces the minimal world-binding frontend carrier preserved on this resolved connector record. */
	void SetResolvedConnectorFrontendSelection(const FLayoutResolvedConnectorFrontendSelection& InSelection)
	{
		WorldBindingId = InSelection.WorldBindingId;
		BiomeRowName = InSelection.BiomeRowName;
		ContinuationFamilyId = InSelection.ContinuationFamilyId;
		ContinuationFamilyCandidateId = InSelection.ContinuationFamilyCandidateId;
		PlacementKind = InSelection.PlacementKind;
		ResolvedContinuationSelection = InSelection.ResolvedContinuationSelection;
		TerrainPathSelection = InSelection.TerrainPathSelection;
		WorldBindingPlacementPolicy = InSelection.WorldBindingPlacementPolicy;
		ContinuationPolicy = InSelection.ContinuationPolicy;
		SolveBudget = InSelection.SolveBudget;
		FrontendSharedCellSizeInBlocks = InSelection.SharedCellSizeInBlocks;
		FrontendTemplatePlacementZOffsetBlocks = InSelection.TemplatePlacementZOffsetBlocks;
	}

	/** Returns the minimal root publication carrier preserved on this resolved connector record. */
	FLayoutRootPublicationMetadata GetRootPublicationMetadata() const
	{
		FLayoutRootPublicationMetadata Metadata;
		Metadata.RootSolveId = RootSolveId;
		Metadata.RootCandidateId = RootCandidateId;
		Metadata.RootPlacementPolicyId = RootPlacementPolicyId;
		return Metadata;
	}

	/** Replaces the minimal root publication carrier preserved on this resolved connector record. */
	void SetRootPublicationMetadata(const FLayoutRootPublicationMetadata& InMetadata)
	{
		RootSolveId = InMetadata.RootSolveId;
		RootCandidateId = InMetadata.RootCandidateId;
		RootPlacementPolicyId = InMetadata.RootPlacementPolicyId;
	}

	/** Returns the minimal authored solve-source carrier preserved on this resolved connector record. */
	FLayoutResolvedConnectorSolveSourceSelection GetResolvedConnectorSolveSourceSelection() const
	{
		FLayoutResolvedConnectorSolveSourceSelection Selection;
		Selection.LayoutProfile = LayoutProfile;
		Selection.ContentSet = ContentSet;
		Selection.SolveSeed = SolveSeed;
		return Selection;
	}

	/** Replaces the minimal authored solve-source carrier preserved on this resolved connector record. */
	void SetResolvedConnectorSolveSourceSelection(const FLayoutResolvedConnectorSolveSourceSelection& InSelection)
	{
		LayoutProfile = InSelection.LayoutProfile;
		ContentSet = InSelection.ContentSet;
		SolveSeed = InSelection.SolveSeed;
	}

	/** Returns the minimal runtime-only lifecycle carrier preserved on this resolved connector record. */
	FResolvedLayoutConnectorRuntimeState GetResolvedConnectorRuntimeState() const
	{
		FResolvedLayoutConnectorRuntimeState State;
		State.bLayoutSolved = bLayoutSolved;
		State.bLayoutRealized = bLayoutRealized;
		State.bHasBeenCommittedToChunkWorld = bHasBeenCommittedToChunkWorld;
		State.TerrainFitDiagnosticKind = TerrainFitDiagnosticKind;
		return State;
	}

	/** Replaces the minimal runtime-only lifecycle carrier preserved on this resolved connector record. */
	void SetResolvedConnectorRuntimeState(const FResolvedLayoutConnectorRuntimeState& InState)
	{
		bLayoutSolved = InState.bLayoutSolved;
		bLayoutRealized = InState.bLayoutRealized;
		bHasBeenCommittedToChunkWorld = InState.bHasBeenCommittedToChunkWorld;
		TerrainFitDiagnosticKind = InState.TerrainFitDiagnosticKind;
	}
};

/** Shared direction helpers used by the planner, validator, and solver. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutDirectionUtils
{
	/** Returns the opposite face direction for a fixed-size cell face. */
	static ELayoutFaceDirection GetOpposite(ELayoutFaceDirection Direction);

	/** Returns the cell delta represented by the supplied face direction. */
	static FIntVector ToCellDelta(ELayoutFaceDirection Direction);

	/** Returns a horizontal face direction after rotating it around Z in 90-degree steps. */
	static ELayoutFaceDirection RotateYaw(ELayoutFaceDirection Direction, int32 YawRotationSteps);
};
