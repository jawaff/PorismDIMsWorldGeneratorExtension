// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Solver/LayoutRegionSchedulePlacementBridgeTypes.h"
#include "Layout/Solver/LayoutRegionScheduleSolverFacade.h"
#include "Layout/Solver/LayoutZoneFeatureDemand.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"

namespace LayoutRegionScheduleSolverPrivate
{
	/** Rebuild region-local artifacts with the same negotiated physical mapping used by proof assembly. */
	void RebuildMergedPostStructuralArtifacts(
		FLayoutRegionSolveScheduleResult& ScheduleResult,
		const TArray<FLayoutDirectChildRegionCommitment>& DirectChildCommitments);
	FIntVector MapChildResultCellToParent(
		const FIntVector& ChildCell,
		const FIntVector& Offset,
		const FLayoutChildStageMappingResult* StageMapping);
	bool EvaluateScheduledZoneFeatureRequirements(
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		FLayoutRegionSolveScheduleResult& ScheduleResult,
		FString& OutFailureReason);
	void AppendOwnedDroppedChildResidualCellsToRegionResult(
		const FString& RegionDebugPath,
		const FLayoutSolveResult& MergedSolveResult,
		const TArray<FLayoutDroppedOptionalChildRecord>& DroppedOptionalChildren,
		FLayoutSolveResult& RegionSolveResult);
	void AppendDroppedChildResidualCells(
		const FLayoutRegionSolveRequest& Request,
		const FLayoutDroppedOptionalChildRecord& DropRecord,
		FLayoutSolveResult& MergedSolveResult);
	void SuppressPassiveSeamPlacementsOnRegionResults(
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		const TMap<FString, int32>& RegionResultIndexByPath,
		FLayoutRegionSolveScheduleResult& ScheduleResult);
	FIntVector TranslateCell(const FIntVector& Cell, const FIntVector& Offset);
	FLayoutPlannedCell TranslatePlannedCell(const FLayoutPlannedCell& PlannedCell, const FIntVector& Offset);
	FLayoutPlacedModule TranslatePlacedModule(const FLayoutPlacedModule& Placement, const FIntVector& Offset);
	FLayoutCellReservationRecord TranslateReservationRecord(const FLayoutCellReservationRecord& Reservation, const FIntVector& Offset);
	FLayoutRouteConstraintRecord TranslateRouteConstraintRecord(const FLayoutRouteConstraintRecord& Constraint, const FIntVector& Offset);
	FLayoutClosureCoverageSegmentRecord TranslateClosureCoverageSegmentRecord(const FLayoutClosureCoverageSegmentRecord& Segment, const FIntVector& Offset);
	FLayoutResidualCellRecord TranslateResidualCellRecord(const FLayoutResidualCellRecord& ResidualCell, const FIntVector& Offset);
	struct FZoneFeatureRequirementCountDetails;
	FString BuildZoneFeatureRequirementOutcomeMessage(
		const FString& RegionDebugPath,
		const FLayoutZoneFeatureRequirement& Requirement,
		const FZoneFeatureRequirementCountDetails& CountDetails,
		const TArray<FString>& AvailableProviders,
		const FString& Problem);
}

/**
 * Private rewrite home for `FLayoutPostStructuralHandoffBuilder`.
 *
 * This file will own closure/seam handoff, sparse-placement eligibility, zone
 * feature visibility, and connector continuity outputs after structural proof.
 */

namespace LayoutRegionScheduleSolverFacade
{
	bool ApplyPostStructuralScheduleHandoffs(
		FLayoutRegionSolveScheduleResult& InOutScheduleResult,
		const FNegotiatedProofScheduleContract& ProofContract,
		FString& OutFailureReason)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_PostStructural, STAT_PorismLayout_PostStructural);
		TMap<FString, const FLayoutRegionSolveRequest*> RequestsByPath;
		RequestsByPath.Add(
			ProofContract.ParentRequest.RegionDebugPath,
			&ProofContract.ParentRequest);
		for (const FLayoutRegionSolveRequest& ChildRequest : ProofContract.ChildRequests)
		{
			RequestsByPath.Add(ChildRequest.RegionDebugPath, &ChildRequest);
		}

		// The frozen optional-drop detail is enough to rebuild dropped-child
		// residual ownership before sparse placement runs, without depending on
		// the original child request staying live on the rewrite path.
		for (const FLayoutDroppedOptionalChildRecord& DropRecord :
			ProofContract.OptionalChildPlan.DroppedOptionalChildren)
		{
			for (const FLayoutPlannedCell& PlannedCell : DropRecord.DroppedPlannedCells)
			{
				const bool bAlreadyPresent =
					InOutScheduleResult.MergedSolveResult.ResidualUnoccupiedCells.ContainsByPredicate(
						[&](const FLayoutResidualCellRecord& ExistingResidual)
						{
							return ExistingResidual.Cell == PlannedCell.Cell
								&& ExistingResidual.RelatedDropDecisionId == DropRecord.DropDecisionId;
						});
				if (bAlreadyPresent)
				{
					continue;
				}

				FLayoutResidualCellRecord& ResidualCell =
					InOutScheduleResult.MergedSolveResult.ResidualUnoccupiedCells.AddDefaulted_GetRef();
				ResidualCell.Cell = PlannedCell.Cell;
				ResidualCell.Intent = PlannedCell.Intent;
				ResidualCell.Source = ELayoutResidualCellSource::DroppedOptionalChild;
				ResidualCell.SourceRegionDebugPath = DropRecord.ChildRegionDebugPath;
				ResidualCell.SourceContentEntryId = DropRecord.SourceContentEntryId;
				ResidualCell.RelatedDropDecisionId = DropRecord.DropDecisionId;
			}
		}

		for (FLayoutRegionSolveResult& RegionResult : InOutScheduleResult.RegionResults)
		{
			LayoutRegionScheduleSolverPrivate::AppendOwnedDroppedChildResidualCellsToRegionResult(
				RegionResult.RegionDebugPath,
				InOutScheduleResult.MergedSolveResult,
				ProofContract.OptionalChildPlan.DroppedOptionalChildren,
				RegionResult.SolveResult);

			if (RegionResult.bDroppedAsOptionalChild)
			{
				continue;
			}

			const FLayoutRegionSolveRequest* RegionRequest =
				RequestsByPath.FindRef(RegionResult.RegionDebugPath);
			if (RegionRequest == nullptr)
			{
				continue;
			}

			FString SparseFailureReason;
			if (!LayoutProfileSolverInternal::ApplySparsePlacementRulesToSolveResult(
				RegionRequest->ProfileSnapshot,
				RegionResult.RegionDebugPath,
				RegionRequest->Seed,
				RegionResult.SolveResult,
				SparseFailureReason))
			{
				OutFailureReason = SparseFailureReason.IsEmpty()
					? FString::Printf(
						TEXT("Sparse placement failed for region '%s'."),
						*RegionResult.RegionDebugPath)
					: SparseFailureReason;
				return false;
			}
		}

		if (!LayoutRegionScheduleSolverPrivate::EvaluateScheduledZoneFeatureRequirements(
			RequestsByPath,
			InOutScheduleResult,
			OutFailureReason))
		{
			return false;
		}

		// Route the existing merged artifact rebuild through the explicit
		// post-structural boundary so rewrite-owned proof execution also owns the
		// scheduled zone-feature visibility audit plus merged artifact rebuild
		// instead of inheriting that work from the deferred-proof helper.
		LayoutRegionScheduleSolverPrivate::RebuildMergedPostStructuralArtifacts(
			InOutScheduleResult, ProofContract.DirectChildCommitments);
		return true;
	}
}

namespace LayoutRegionScheduleSolverPrivate
{
	void RebuildMergedPostStructuralArtifacts(
		FLayoutRegionSolveScheduleResult& ScheduleResult,
		const TArray<FLayoutDirectChildRegionCommitment>& DirectChildCommitments)
	{
		ScheduleResult.MergedSolveResult.PlannedCells.Reset();
		ScheduleResult.MergedSolveResult.Placements.Reset();
		ScheduleResult.MergedSolveResult.ZoneFeatureProviderCommitments.Reset();
		ScheduleResult.MergedSolveResult.ExportedEntryCells.Reset();
		ScheduleResult.MergedSolveResult.CompiledReservations.Reset();
		ScheduleResult.MergedSolveResult.RouteConstraints.Reset();
		ScheduleResult.MergedSolveResult.ClosureCoverage.Reset();
		ScheduleResult.MergedSolveResult.ClosureSegments.Reset();
		ScheduleResult.MergedSolveResult.ClosureRuns.Reset();
		ScheduleResult.MergedSolveResult.ResidualUnoccupiedCells.Reset();
		ScheduleResult.MergedSolveResult.SparsePlacementCommitments.Reset();
		ScheduleResult.MergedSolveResult.SparsePlacementStats = FLayoutSparsePlacementStats();
		TMap<FIntVector, int32> PlacementIndexByWorldCell;

		TMap<FIntVector, FString> ForcedOwnerRegionByWorldCell;
		for (const FLayoutPartitionSeamRecord& SeamRecord : ScheduleResult.MergedSolveResult.PartitionSeams)
		{
			const FIntVector OwnerStep(
				FMath::Clamp(SeamRecord.OwnerEndCell.X - SeamRecord.OwnerStartCell.X, -1, 1),
				FMath::Clamp(SeamRecord.OwnerEndCell.Y - SeamRecord.OwnerStartCell.Y, -1, 1),
				FMath::Clamp(SeamRecord.OwnerEndCell.Z - SeamRecord.OwnerStartCell.Z, -1, 1));
			const FIntVector PassiveStep(
				FMath::Clamp(SeamRecord.PassiveEndCell.X - SeamRecord.PassiveStartCell.X, -1, 1),
				FMath::Clamp(SeamRecord.PassiveEndCell.Y - SeamRecord.PassiveStartCell.Y, -1, 1),
				FMath::Clamp(SeamRecord.PassiveEndCell.Z - SeamRecord.PassiveStartCell.Z, -1, 1));

			for (int32 SegmentIndex = 0; SegmentIndex < SeamRecord.SegmentCount; ++SegmentIndex)
			{
				const FIntVector OwnerWorldCell = SeamRecord.OwnerStartCell + OwnerStep * SegmentIndex;
				const FIntVector PassiveWorldCell = SeamRecord.PassiveStartCell + PassiveStep * SegmentIndex;
				if (OwnerWorldCell == PassiveWorldCell)
				{
					ForcedOwnerRegionByWorldCell.FindOrAdd(OwnerWorldCell, SeamRecord.OwnerRegionDebugPath);
				}
			}
		}

		for (const FLayoutRegionSolveResult& RegionResult : ScheduleResult.RegionResults)
		{
			const FLayoutDirectChildRegionCommitment* StageCommitment = DirectChildCommitments.FindByPredicate(
				[&RegionResult](const FLayoutDirectChildRegionCommitment& Candidate)
				{
					return Candidate.ChildRegionDebugPath == RegionResult.RegionDebugPath;
				});
			const auto MapCell = [&RegionResult, StageCommitment](const FIntVector& Cell)
			{
				return MapChildResultCellToParent(
					Cell, RegionResult.RegionCellOffset, StageCommitment != nullptr ? &StageCommitment->StageMapping : nullptr);
			};
			for (const FLayoutPlannedCell& PlannedCell : RegionResult.SolveResult.PlannedCells)
			{
				ScheduleResult.MergedSolveResult.PlannedCells.Add(
					TranslatePlannedCell(PlannedCell, MapCell(PlannedCell.Cell) - PlannedCell.Cell));
			}

			for (const FLayoutPlacedModule& Placement : RegionResult.SolveResult.Placements)
			{
				const FLayoutPlacedModule TranslatedPlacement = TranslatePlacedModule(Placement, MapCell(Placement.Cell) - Placement.Cell);
				const FString* ForcedOwnerRegionPath = ForcedOwnerRegionByWorldCell.Find(TranslatedPlacement.Cell);
				if (ForcedOwnerRegionPath == nullptr)
				{
					ScheduleResult.MergedSolveResult.Placements.Add(TranslatedPlacement);
					continue;
				}

				const bool bCurrentRegionIsForcedOwner = *ForcedOwnerRegionPath == RegionResult.RegionDebugPath;
				if (!bCurrentRegionIsForcedOwner)
				{
					continue;
				}

				if (int32* ExistingPlacementIndex = PlacementIndexByWorldCell.Find(TranslatedPlacement.Cell))
				{
					if (ScheduleResult.MergedSolveResult.Placements.IsValidIndex(*ExistingPlacementIndex))
					{
						ScheduleResult.MergedSolveResult.Placements[*ExistingPlacementIndex] = TranslatedPlacement;
					}
					continue;
				}

				const int32 AddedIndex = ScheduleResult.MergedSolveResult.Placements.Add(TranslatedPlacement);
				PlacementIndexByWorldCell.Add(TranslatedPlacement.Cell, AddedIndex);
			}

			for (const FLayoutZoneFeatureProviderCommitment& Commitment :
				RegionResult.SolveResult.ZoneFeatureProviderCommitments)
			{
				FLayoutZoneFeatureProviderCommitment TranslatedCommitment = Commitment;
				TranslatedCommitment.Cell = MapCell(Commitment.Cell);
				ScheduleResult.MergedSolveResult.ZoneFeatureProviderCommitments.Add(
					MoveTemp(TranslatedCommitment));
			}

			if (RegionResult.SourceContentEntryId.IsNone())
			{
				// Site publication owns root exterior Entries; child and terrain-seam gates remain region-local.
				for (const FIntVector& EntryCell : RegionResult.SolveResult.ExportedEntryCells)
				{
					const FLayoutPlannedCell* const PlannedEntry =
						RegionResult.SolveResult.PlannedCells.FindByPredicate(
							[&EntryCell](const FLayoutPlannedCell& PlannedCell)
							{
								return PlannedCell.Cell == EntryCell
									&& PlannedCell.Intent == ELayoutCellIntent::Entry;
							});
					if (PlannedEntry != nullptr
						&& (PlannedEntry->EntryOrigin == ELayoutEntryOrigin::TerrainSeam
							|| PlannedEntry->EntryOrigin == ELayoutEntryOrigin::ChildContract))
					{
						continue;
					}
					ScheduleResult.MergedSolveResult.ExportedEntryCells.Add(
						MapCell(EntryCell));
				}
			}

			for (const FLayoutCellReservationRecord& Reservation : RegionResult.SolveResult.CompiledReservations)
			{
				ScheduleResult.MergedSolveResult.CompiledReservations.Add(
					TranslateReservationRecord(
						Reservation,
						MapCell(Reservation.Cell) - Reservation.Cell));
			}

			for (const FLayoutRouteConstraintRecord& Constraint : RegionResult.SolveResult.RouteConstraints)
			{
				ScheduleResult.MergedSolveResult.RouteConstraints.Add(
					TranslateRouteConstraintRecord(
						Constraint,
						MapCell(Constraint.Cell) - Constraint.Cell));
			}

			ScheduleResult.MergedSolveResult.ClosureCoverage.Append(RegionResult.SolveResult.ClosureCoverage);

			for (const FLayoutClosureCoverageSegmentRecord& Segment : RegionResult.SolveResult.ClosureSegments)
			{
				ScheduleResult.MergedSolveResult.ClosureSegments.Add(
					TranslateClosureCoverageSegmentRecord(
						Segment,
						MapCell(Segment.Cell) - Segment.Cell));
			}

			for (const FLayoutClosureRunRecord& Run : RegionResult.SolveResult.ClosureRuns)
			{
				FLayoutClosureRunRecord MappedRun = Run;
				MappedRun.StartCell = MapCell(Run.StartCell);
				MappedRun.EndCell = MapCell(Run.EndCell);
				ScheduleResult.MergedSolveResult.ClosureRuns.Add(MoveTemp(MappedRun));
			}

			for (const FLayoutResidualCellRecord& ResidualCell : RegionResult.SolveResult.ResidualUnoccupiedCells)
			{
				ScheduleResult.MergedSolveResult.ResidualUnoccupiedCells.Add(
					TranslateResidualCellRecord(ResidualCell, MapCell(ResidualCell.Cell) - ResidualCell.Cell));
			}

			for (const FLayoutSparsePlacementCommitment& Commitment : RegionResult.SolveResult.SparsePlacementCommitments)
			{
				FLayoutSparsePlacementCommitment MappedCommitment = Commitment;
				MappedCommitment.Cell = MapCell(Commitment.Cell);
				for (FIntVector& Cell : MappedCommitment.OccupiedCells)
				{
					Cell = MapCell(Cell);
				}
				ScheduleResult.MergedSolveResult.SparsePlacementCommitments.Add(MoveTemp(MappedCommitment));
			}
			FLayoutSparsePlacementStats& MergedStats = ScheduleResult.MergedSolveResult.SparsePlacementStats;
			const FLayoutSparsePlacementStats& RegionStats = RegionResult.SolveResult.SparsePlacementStats;
			MergedStats.RuleCount += RegionStats.RuleCount;
			MergedStats.EligibleCellCount += RegionStats.EligibleCellCount;
			MergedStats.LegalCandidateCheckCount += RegionStats.LegalCandidateCheckCount;
			MergedStats.AcceptedPlacementCount += RegionStats.AcceptedPlacementCount;
			MergedStats.OccupiedCellCheckCount += RegionStats.OccupiedCellCheckCount;
			MergedStats.DurationSeconds += RegionStats.DurationSeconds;
		}

		ScheduleResult.MergedSolveResult.ZoneFeatureProviderCommitments.Sort(
			[](const FLayoutZoneFeatureProviderCommitment& Left,
				const FLayoutZoneFeatureProviderCommitment& Right)
			{
				return Left.ProviderCommitmentId.LexicalLess(
					Right.ProviderCommitmentId);
			});
	}

	struct FZoneFeatureRequirementCountDetails
	{
		int32 MatchCount = 0;
		TArray<FString> MatchingSources;
	};

	bool EvaluateScheduledZoneFeatureRequirements(
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		FLayoutRegionSolveScheduleResult& ScheduleResult,
		FString& OutFailureReason)
	{
		for (FLayoutRegionSolveResult& RegionResult : ScheduleResult.RegionResults)
		{
			if (RegionResult.bDroppedAsOptionalChild)
			{
				continue;
			}

			const FLayoutRegionSolveRequest* RegionRequest = RequestsByPath.FindRef(RegionResult.RegionDebugPath);
			if (RegionRequest == nullptr || RegionRequest->ProfileSnapshot.ZoneFeatureRequirements.IsEmpty())
			{
				continue;
			}

			TArray<const FLayoutZoneFeatureRequirement*> SortedRequirements;
			SortedRequirements.Reserve(RegionRequest->ProfileSnapshot.ZoneFeatureRequirements.Num());
			for (const FLayoutZoneFeatureRequirement& Requirement : RegionRequest->ProfileSnapshot.ZoneFeatureRequirements)
			{
				SortedRequirements.Add(&Requirement);
			}

			SortedRequirements.Sort([](const FLayoutZoneFeatureRequirement& Left, const FLayoutZoneFeatureRequirement& Right)
			{
				return Left.RequirementId.LexicalLess(Right.RequirementId);
			});

			for (const FLayoutZoneFeatureRequirement* RequirementPtr : SortedRequirements)
			{
				check(RequirementPtr != nullptr);
				const FLayoutZoneFeatureRequirement& Requirement = *RequirementPtr;
				if (Requirement.RequiredFeatures.IsEmpty())
				{
					continue;
				}

				TArray<FString> AvailableProviders;
				for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : RegionRequest->ContentSetSnapshot.Entries)
				{
					if (LayoutZoneFeatureDemand::DoesProvidedFeatureSetMatchRequirement(EntrySnapshot.ProvidedZoneFeatures, Requirement))
					{
						AvailableProviders.Add(EntrySnapshot.EntryId.IsNone() ? TEXT("<none>") : EntrySnapshot.EntryId.ToString());
					}
				}
				AvailableProviders.Sort();

				FZoneFeatureRequirementCountDetails CountDetails;
				// Local project fix: final audit reads exact shared module/child commitments instead of reconstructing provider authority.
				for (const FLayoutZoneFeatureProviderCommitment& Commitment :
					RegionResult.SolveResult.ZoneFeatureProviderCommitments)
				{
					if (Commitment.RequirementId != Requirement.RequirementId)
					{
						continue;
					}

					++CountDetails.MatchCount;
					CountDetails.MatchingSources.Add(FString::Printf(
						TEXT("%s@(%d,%d,%d)[%s]"),
						*Commitment.SourceContentEntryId.ToString(),
						Commitment.Cell.X,
						Commitment.Cell.Y,
						Commitment.Cell.Z,
						*Commitment.ProviderCommitmentId.ToString()));
				}

				const bool bBelowMinimum = CountDetails.MatchCount < Requirement.MinCount;
				const bool bAboveMaximum = Requirement.MaxCount > 0 && CountDetails.MatchCount > Requirement.MaxCount;
				if (!bBelowMinimum && !bAboveMaximum)
				{
					continue;
				}

				const FString Problem = bBelowMinimum
					? FString::Printf(TEXT("Only %d matching placement(s) or direct child placements were found, but the requirement needs %d."), CountDetails.MatchCount, Requirement.MinCount)
					: FString::Printf(TEXT("Found %d matching placement(s) or direct child placements, which exceeds the allowed maximum of %d."), CountDetails.MatchCount, Requirement.MaxCount);
				const FString Message = BuildZoneFeatureRequirementOutcomeMessage(
					RegionResult.RegionDebugPath,
					Requirement,
					CountDetails,
					AvailableProviders,
					Problem);

				FLayoutValidationMessage ValidationMessage;
				ValidationMessage.Severity = ELayoutValidationSeverity::Error;
				ValidationMessage.Message = Message;
				RegionResult.SolveResult.Messages.Add(ValidationMessage);
				ScheduleResult.MergedSolveResult.Messages.Add(ValidationMessage);

				++RegionResult.SolveResult.PropagationStats.HardZoneFeatureLateAuditFailureCount;
				++ScheduleResult.MergedSolveResult.PropagationStats.HardZoneFeatureLateAuditFailureCount;
				OutFailureReason = Message;
				return false;
			}
		}

		return true;
	}

void AppendDroppedChildResidualCells(
		const FLayoutRegionSolveRequest& Request,
		const FLayoutDroppedOptionalChildRecord& DropRecord,
		FLayoutSolveResult& MergedSolveResult)
	{
		for (const FLayoutPlannedCell& PlannedCell : Request.PlannedCells)
		{
			FLayoutResidualCellRecord& ResidualCell = MergedSolveResult.ResidualUnoccupiedCells.AddDefaulted_GetRef();
			ResidualCell.Cell = PlannedCell.Cell;
			ResidualCell.Intent = PlannedCell.Intent;
			ResidualCell.Source = ELayoutResidualCellSource::DroppedOptionalChild;
			ResidualCell.SourceRegionDebugPath = Request.RegionDebugPath;
			ResidualCell.SourceContentEntryId = DropRecord.SourceContentEntryId;
			ResidualCell.RelatedDropDecisionId = DropRecord.DropDecisionId;
		}
	}

void AppendOwnedDroppedChildResidualCellsToRegionResult(
		const FString& RegionDebugPath,
		const FLayoutSolveResult& MergedSolveResult,
		const TArray<FLayoutDroppedOptionalChildRecord>& DroppedOptionalChildren,
		FLayoutSolveResult& RegionSolveResult)
	{
		TSet<FLayoutId> OwnedDropIds;
		for (const FLayoutDroppedOptionalChildRecord& DropRecord : DroppedOptionalChildren)
		{
			if (DropRecord.ParentRegionDebugPath == RegionDebugPath)
			{
				OwnedDropIds.Add(DropRecord.DropDecisionId);
			}
		}

		if (OwnedDropIds.IsEmpty())
		{
			return;
		}

		for (const FLayoutResidualCellRecord& ResidualCell : MergedSolveResult.ResidualUnoccupiedCells)
		{
			if (ResidualCell.Source != ELayoutResidualCellSource::DroppedOptionalChild
				|| !OwnedDropIds.Contains(ResidualCell.RelatedDropDecisionId))
			{
				continue;
			}

			const bool bAlreadyPresent = RegionSolveResult.ResidualUnoccupiedCells.ContainsByPredicate([&ResidualCell](const FLayoutResidualCellRecord& ExistingResidual)
			{
				return ExistingResidual.Cell == ResidualCell.Cell
					&& ExistingResidual.RelatedDropDecisionId == ResidualCell.RelatedDropDecisionId;
			});
			if (!bAlreadyPresent)
			{
				RegionSolveResult.ResidualUnoccupiedCells.Add(ResidualCell);
			}
		}
	}

void SuppressPassiveSeamPlacementsOnRegionResults(
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		const TMap<FString, int32>& RegionResultIndexByPath,
		FLayoutRegionSolveScheduleResult& ScheduleResult)
	{
		struct FSeamSuppressionRegionLookup
		{
			const FLayoutRegionSolveRequest* Request = nullptr;
			const FLayoutRegionSolveResult* Result = nullptr;
			TMap<FLayoutId, const FLayoutRegionContentEntrySolveSnapshot*> EntrySnapshotsById;
			TMap<FLayoutId, bool> EntryCoverageByEntryId;
			TMap<FIntVector, const FLayoutPlacedModule*> PlacementsByLocalCell;
			TMap<FIntVector, const FLayoutPlannedCell*> PlannedCellsByLocalCell;
			TMap<FIntVector, TSet<ELayoutFaceDirection>> CommittedAnchorFacesByLocalCell;
		};

		const auto FindContentEntrySnapshotById = [](
			const FSeamSuppressionRegionLookup& RegionLookup,
			const FName EntryId) -> const FLayoutRegionContentEntrySolveSnapshot*
		{
			return RegionLookup.EntrySnapshotsById.FindRef(EntryId);
		};

		const auto DoesEntrySnapshotExposeEntryCoverage = [](
			const FSeamSuppressionRegionLookup& RegionLookup,
			const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot) -> bool
		{
			if (EntrySnapshot.ContentKind != ELayoutRegionContentKind::Module
				|| RegionLookup.Request == nullptr
				|| !RegionLookup.Request->ModuleCatalog.Modules.IsValidIndex(EntrySnapshot.ModuleSnapshotIndex))
			{
				return false;
			}

			const FLayoutModuleSolveSnapshot& ModuleSnapshot =
				RegionLookup.Request->ModuleCatalog.Modules[EntrySnapshot.ModuleSnapshotIndex];
			return ModuleSnapshot.SupportsIntent(ELayoutCellIntent::Entry)
				|| ModuleSnapshot.Roles.Contains(ELayoutModuleRole::Entry)
				|| ModuleSnapshot.DerivedEndpointOffers.ContainsByPredicate([](const FLayoutDerivedEndpointOffer& Offer)
				{
					return Offer.Roles.Contains(ELayoutModuleRole::Entry)
						|| Offer.ConnectionTag == LayoutGameplayTags::FaceEntry;
				});
		};

		const auto DoesPlacementExposeEntryCoverage = [&FindContentEntrySnapshotById, &DoesEntrySnapshotExposeEntryCoverage](
			const FSeamSuppressionRegionLookup& RegionLookup,
			const FLayoutPlacedModule& Placement) -> bool
		{
			if (Placement.SourceContentEntryId.IsNone())
			{
				return false;
			}

			if (const bool* CachedCoverage = RegionLookup.EntryCoverageByEntryId.Find(Placement.SourceContentEntryId))
			{
				return *CachedCoverage;
			}

			const FLayoutRegionContentEntrySolveSnapshot* EntrySnapshot =
				FindContentEntrySnapshotById(RegionLookup, Placement.SourceContentEntryId);
			if (EntrySnapshot == nullptr)
			{
				return false;
			}

			return DoesEntrySnapshotExposeEntryCoverage(RegionLookup, *EntrySnapshot);
		};

		const auto DoesPlacementParticipateInPassiveSeam = [&FindContentEntrySnapshotById](
			const FSeamSuppressionRegionLookup& PassiveLookup,
			const FLayoutPlacedModule& Placement,
			const FLayoutPartitionSeamRecord& SeamRecord) -> bool
		{
			if (Placement.SourceContentEntryId.IsNone())
			{
				return false;
			}

			const FLayoutRegionContentEntrySolveSnapshot* EntrySnapshot =
				FindContentEntrySnapshotById(PassiveLookup, Placement.SourceContentEntryId);
			if (EntrySnapshot == nullptr)
			{
				return false;
			}

			return EntrySnapshot->SeamProviderIntents.ContainsByPredicate([&SeamRecord](const FLayoutSeamProviderIntent& SeamIntent)
			{
				return SeamIntent.bCanAcceptSeam
					&& SeamIntent.InterfaceFamily == SeamRecord.InterfaceFamily;
			});
		};

		TMap<FString, FSeamSuppressionRegionLookup> RegionLookupsByPath;
		for (const FLayoutRegionSolveResult& RegionResult : ScheduleResult.RegionResults)
		{
			FSeamSuppressionRegionLookup& RegionLookup = RegionLookupsByPath.FindOrAdd(RegionResult.RegionDebugPath);
			RegionLookup.Request = RequestsByPath.FindRef(RegionResult.RegionDebugPath);
			RegionLookup.Result = &RegionResult;

			for (const FLayoutPlacedModule& Placement : RegionResult.SolveResult.Placements)
			{
				RegionLookup.PlacementsByLocalCell.Add(Placement.Cell, &Placement);
			}
			for (const FLayoutPlannedCell& PlannedCell : RegionResult.SolveResult.PlannedCells)
			{
				RegionLookup.PlannedCellsByLocalCell.Add(PlannedCell.Cell, &PlannedCell);
			}
			for (const FLayoutCommittedEndpointAnchor& Anchor : RegionResult.CommittedEndpointAnchors)
			{
				RegionLookup.CommittedAnchorFacesByLocalCell.FindOrAdd(Anchor.LocalCell).Add(Anchor.FaceDirection);
			}
			if (RegionLookup.Request == nullptr)
			{
				continue;
			}

			// Cache project-specific seam suppression lookups once so the committed seam
			// cleanup pass does not repeatedly linearly rescan region placements and content entries.
			for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : RegionLookup.Request->ContentSetSnapshot.Entries)
			{
				RegionLookup.EntrySnapshotsById.Add(EntrySnapshot.EntryId, &EntrySnapshot);
				RegionLookup.EntryCoverageByEntryId.Add(
					EntrySnapshot.EntryId,
					DoesEntrySnapshotExposeEntryCoverage(RegionLookup, EntrySnapshot));
			}
		}

		TMap<FString, TSet<FIntVector>> SuppressedLocalCellsByRegion;
		for (const FLayoutPartitionSeamRecord& SeamRecord : PlannedPartitionSeams)
		{
			const int32* PassiveResultIndex = RegionResultIndexByPath.Find(SeamRecord.PassiveRegionDebugPath);
			const int32* OwnerResultIndex = RegionResultIndexByPath.Find(SeamRecord.OwnerRegionDebugPath);
			if (PassiveResultIndex == nullptr || !ScheduleResult.RegionResults.IsValidIndex(*PassiveResultIndex))
			{
				continue;
			}
			if (OwnerResultIndex == nullptr || !ScheduleResult.RegionResults.IsValidIndex(*OwnerResultIndex))
			{
				continue;
			}

			const FSeamSuppressionRegionLookup* PassiveLookup = RegionLookupsByPath.Find(SeamRecord.PassiveRegionDebugPath);
			const FSeamSuppressionRegionLookup* OwnerLookup = RegionLookupsByPath.Find(SeamRecord.OwnerRegionDebugPath);
			if (PassiveLookup == nullptr
				|| OwnerLookup == nullptr
				|| PassiveLookup->Request == nullptr
				|| OwnerLookup->Request == nullptr
				|| PassiveLookup->Result == nullptr
				|| OwnerLookup->Result == nullptr)
			{
				continue;
			}

			const FLayoutRegionSolveResult& PassiveRegionResult = *PassiveLookup->Result;
			const FLayoutRegionSolveResult& OwnerRegionResult = *OwnerLookup->Result;

			const FIntVector OwnerStep(
				FMath::Clamp(SeamRecord.OwnerEndCell.X - SeamRecord.OwnerStartCell.X, -1, 1),
				FMath::Clamp(SeamRecord.OwnerEndCell.Y - SeamRecord.OwnerStartCell.Y, -1, 1),
				FMath::Clamp(SeamRecord.OwnerEndCell.Z - SeamRecord.OwnerStartCell.Z, -1, 1));
			const FIntVector PassiveStep(
				FMath::Clamp(SeamRecord.PassiveEndCell.X - SeamRecord.PassiveStartCell.X, -1, 1),
				FMath::Clamp(SeamRecord.PassiveEndCell.Y - SeamRecord.PassiveStartCell.Y, -1, 1),
				FMath::Clamp(SeamRecord.PassiveEndCell.Z - SeamRecord.PassiveStartCell.Z, -1, 1));

			TSet<FIntVector>& SuppressedCells = SuppressedLocalCellsByRegion.FindOrAdd(SeamRecord.PassiveRegionDebugPath);
			for (int32 SegmentIndex = 0; SegmentIndex < SeamRecord.SegmentCount; ++SegmentIndex)
			{
				const FIntVector OwnerWorldCell = SeamRecord.OwnerStartCell + OwnerStep * SegmentIndex;
				const FIntVector PassiveWorldCell = SeamRecord.PassiveStartCell + PassiveStep * SegmentIndex;
				const bool bSameWorldCell = OwnerWorldCell == PassiveWorldCell;
				const FIntVector OwnerLocalCell = OwnerWorldCell - OwnerRegionResult.RegionCellOffset;
				const FIntVector PassiveLocalCell = PassiveWorldCell - PassiveRegionResult.RegionCellOffset;

				if (bSameWorldCell)
				{
					SuppressedCells.Add(PassiveLocalCell);
					continue;
				}

				const FLayoutPlacedModule* PassivePlacement = PassiveLookup->PlacementsByLocalCell.FindRef(PassiveLocalCell);
				if (PassivePlacement == nullptr)
				{
					continue;
				}

				const bool bOwnerIsParentOnParentChildSeam =
					SeamRecord.ParentRegionDebugPath == SeamRecord.OwnerRegionDebugPath
					&& SeamRecord.PassiveRegionDebugPath != SeamRecord.ParentRegionDebugPath;
				if (bOwnerIsParentOnParentChildSeam)
				{
					if (PassiveLookup->CommittedAnchorFacesByLocalCell.Contains(PassiveLocalCell))
					{
						continue;
					}

					// Parent-owned parent/child seam spans must always evict the passive child's
					// local realized placement, even when the parent's retained solution does not
					// keep a colocated placement on every committed seam segment.
					SuppressedCells.Add(PassiveLocalCell);
					continue;
				}

				const FLayoutPlacedModule* OwnerPlacement = OwnerLookup->PlacementsByLocalCell.FindRef(OwnerLocalCell);
				if (OwnerPlacement == nullptr)
				{
					continue;
				}

				const FLayoutPlannedCell* PassivePlannedCell = PassiveLookup->PlannedCellsByLocalCell.FindRef(PassiveLocalCell);
				const bool bPassiveCellNeedsEntryCoverage =
					SeamRecord.InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor
					|| (PassivePlannedCell != nullptr && PassivePlannedCell->Intent == ELayoutCellIntent::Entry);
				if (bPassiveCellNeedsEntryCoverage
					&& !DoesPlacementExposeEntryCoverage(*OwnerLookup, *OwnerPlacement))
				{
					continue;
				}

				const bool bPassivePlacementParticipatesInCommittedSeam =
					DoesPlacementParticipateInPassiveSeam(*PassiveLookup, *PassivePlacement, SeamRecord);
				const bool bRequiresParentPassiveFallbackSuppression =
					PassiveRegionResult.RegionDebugPath == SeamRecord.ParentRegionDebugPath
					&& !bPassivePlacementParticipatesInCommittedSeam
					&& !DoesPlacementExposeEntryCoverage(*PassiveLookup, *PassivePlacement);
				if (bPassivePlacementParticipatesInCommittedSeam
					|| bRequiresParentPassiveFallbackSuppression)
				{
					SuppressedCells.Add(PassiveLocalCell);
				}
			}
		}

		for (FLayoutRegionSolveResult& RegionResult : ScheduleResult.RegionResults)
		{
			const TSet<FIntVector>* SuppressedCells = SuppressedLocalCellsByRegion.Find(RegionResult.RegionDebugPath);
			if (SuppressedCells == nullptr || SuppressedCells->IsEmpty())
			{
				continue;
			}

			RegionResult.SolveResult.Placements.RemoveAll([SuppressedCells](const FLayoutPlacedModule& Placement)
			{
				return SuppressedCells->Contains(Placement.Cell);
			});

			RegionResult.SolveResult.ExportedEntryCells.RemoveAll([SuppressedCells](const FIntVector& EntryCell)
			{
				return SuppressedCells->Contains(EntryCell);
			});

			// Local project fix: suppressed passive module placements cannot retain counted-feature authority; direct-child commitments remain independent.
			LayoutZoneFeatureDemand::RemoveSuppressedPassiveModuleCommitments(
				*SuppressedCells,
				RegionResult.SolveResult.ZoneFeatureProviderCommitments);

			RegionResult.SolveResult.SparsePlacementCommitments.RemoveAll([SuppressedCells](const FLayoutSparsePlacementCommitment& Commitment)
			{
				return SuppressedCells->Contains(Commitment.Cell);
			});
		}
	}
}
