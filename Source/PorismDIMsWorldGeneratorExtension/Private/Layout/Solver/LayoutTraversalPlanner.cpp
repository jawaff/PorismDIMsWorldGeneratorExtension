// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Solver/LayoutPlacementOccupancy.h"
#include "Layout/Solver/LayoutSolveExecutionBudget.h"

#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Async/LayoutBackgroundSolveCancellation.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Layout/Types/LayoutEntryRootUtilities.h"
#include "Misc/ScopeExit.h"

namespace LayoutTraversalPlannerPrivate
{
	/** Charges sparse preparation and route expansion to the invocation ledger as well as local CSP limits. */
	bool ConsumeSparseRouteWork(LayoutProfileSolverInternal::FSolveContext& Context)
	{
		if (!LayoutSolveExecution::Checkpoint(Context.Result.FailureReason))
		{
			Context.bTimeBudgetExceeded = true;
			return false;
		}
		if (Context.bTimeBudgetExceeded || LayoutSolveCancellation::IsCurrentThreadCancellationRequested()
			|| (Context.MaxSolveDurationSeconds > 0.0
				&& FPlatformTime::Seconds() - Context.SolveStartTimeSeconds >= Context.MaxSolveDurationSeconds))
		{
			Context.bTimeBudgetExceeded = true;
			Context.Result.FailureReason = TEXT("Sparse structural route work canceled or exceeded its inherited deadline.");
			return false;
		}
		if (Context.CandidateAttemptCount >= Context.MaxCandidateAttempts)
		{
			Context.Result.FailureReason = TEXT("Sparse structural route work exhausted its inherited candidate budget.");
			return false;
		}
		if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::SparseWork, Context.Result.FailureReason))
		{
			Context.bTimeBudgetExceeded = true;
			return false;
		}
		++Context.CandidateAttemptCount;
		return true;
	}

	int32 GetBestHorizontalTraversalExposureForCell(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell);
	bool DoesAnyCandidateExposeHorizontalTraversalFaceForCell(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const ELayoutFaceDirection Direction,
		const FGameplayTag TraversalTag);

	using LayoutProfileSolverInternal::GetSolveCandidateDebugName;
	using LayoutProfileSolverInternal::GetSolveCandidateInternalAccessLinks;
	using LayoutProfileSolverInternal::GetSolveCandidateTraversalChannels;
	using LayoutProfileSolverInternal::TryGetSolveCandidateFaceRule;
	using LayoutProfileSolverInternal::TryGetSolvePlacementFaceRule;

	const TCHAR* ToDebugString(const ELayoutCellIntent Intent)
	{
		switch (Intent)
		{
		case ELayoutCellIntent::Boundary:
			return TEXT("Boundary");
		case ELayoutCellIntent::Entry:
			return TEXT("Entry");
		case ELayoutCellIntent::Core:
			return TEXT("Core");
		case ELayoutCellIntent::Interior:
			return TEXT("Interior");
		case ELayoutCellIntent::Connector:
			return TEXT("Connector");
		case ELayoutCellIntent::VerticalAccess:
			return TEXT("VerticalAccess");
		default:
			return TEXT("Unknown");
		}
	}

	bool IsBoundaryCell(const FIntVector& Cell, const FIntPoint& FootprintSize)
	{
		return Cell.X == 0
			|| Cell.Y == 0
			|| Cell.X == FootprintSize.X - 1
			|| Cell.Y == FootprintSize.Y - 1;
	}

	bool DoesCellMatchLevelPlacementPolicy(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		ELayoutLevelPlacementPolicy LevelPlacementPolicy,
		int32 SpecificLevel);

	ELayoutLevelFillMode GetLevelFillModeForCell(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell)
	{
		for (const FLayoutLevelFillRule& Rule : Context.ProfileSnapshot.LevelFillRules)
		{
			if (DoesCellMatchLevelPlacementPolicy(
				Context,
				Cell,
				Rule.LevelPlacementPolicy,
				Rule.SpecificLevel))
			{
				return Rule.FillMode;
			}
		}

		return ELayoutLevelFillMode::Default;
	}

	bool DoesCellMatchLevelPlacementPolicy(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const ELayoutLevelPlacementPolicy LevelPlacementPolicy,
		const int32 SpecificLevel)
	{
		switch (LevelPlacementPolicy)
		{
		case ELayoutLevelPlacementPolicy::AnyLevel:
			return true;
		case ELayoutLevelPlacementPolicy::GroundOnly:
			return Context.ModuleLevelByCell.FindRef(Cell) == 0;
		case ELayoutLevelPlacementPolicy::SpecificLevel:
			return Context.ModuleLevelByCell.FindRef(Cell) == SpecificLevel;
		case ELayoutLevelPlacementPolicy::TopLevelOnly:
		{
			const int32* TopLevel = Context.TopPlannedLevelByXY.Find(FIntPoint(Cell.X, Cell.Y));
			return TopLevel != nullptr && Context.ModuleLevelByCell.FindRef(Cell) == *TopLevel;
		}
		case ELayoutLevelPlacementPolicy::AboveGroundLevel:
			return Context.ModuleLevelByCell.FindRef(Cell) > 0;
		case ELayoutLevelPlacementPolicy::BelowTopLevel:
		{
			const int32* TopLevel = Context.TopPlannedLevelByXY.Find(FIntPoint(Cell.X, Cell.Y));
			return TopLevel != nullptr && Context.ModuleLevelByCell.FindRef(Cell) < *TopLevel;
		}
		default:
			return true;
		}
	}

	bool IsHorizontalDirection(const ELayoutFaceDirection Direction)
	{
		return Direction == ELayoutFaceDirection::PosX
			|| Direction == ELayoutFaceDirection::NegX
			|| Direction == ELayoutFaceDirection::PosY
			|| Direction == ELayoutFaceDirection::NegY;
	}

	bool TryGetHorizontalDirectionBetweenCells(
		const FIntVector& FromCell,
		const FIntVector& ToCell,
		ELayoutFaceDirection& OutDirection)
	{
		const FIntVector Delta = ToCell - FromCell;
		if (Delta == FIntVector(1, 0, 0))
		{
			OutDirection = ELayoutFaceDirection::PosX;
			return true;
		}

		if (Delta == FIntVector(-1, 0, 0))
		{
			OutDirection = ELayoutFaceDirection::NegX;
			return true;
		}

		if (Delta == FIntVector(0, 1, 0))
		{
			OutDirection = ELayoutFaceDirection::PosY;
			return true;
		}

		if (Delta == FIntVector(0, -1, 0))
		{
			OutDirection = ELayoutFaceDirection::NegY;
			return true;
		}

		return false;
	}

	bool IsFaceCompatibleWithOccupancy(
		const FLayoutFaceRule& FaceRule,
		const bool bNeighborPlanned,
		const bool bNeighborFilled)
	{
		if (FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
		{
			return true;
		}

		if (!bNeighborPlanned || !bNeighborFilled)
		{
			return FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor
				|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor
				|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor;
		}

		return FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor;
	}

	FString BuildCandidateDebugLabel(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const LayoutProfileSolverInternal::FSolveCandidate& Candidate)
	{
		const FLayoutId CandidateName = GetSolveCandidateDebugName(Context, Candidate);
		return FString::Printf(
			TEXT("%s yaw=%d"),
			CandidateName == NAME_None ? TEXT("<none>") : *CandidateName.ToString(),
			Candidate.YawRotationSteps);
	}

	FString BuildRouteConstraintRequirementSummary(const FLayoutRouteConstraintRecord& Constraint)
	{
		return Constraint.FaceRequirements.IsEmpty()
			? TEXT("<none>")
			: FString::JoinBy(Constraint.FaceRequirements, TEXT(","), [](const FLayoutRouteFaceRequirement& Requirement)
			{
				return FString::Printf(TEXT("%s:%s"),
					*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Requirement.FaceDirection)),
					Requirement.TraversalChannel.IsValid() ? *Requirement.TraversalChannel.ToString() : TEXT("None"));
			});
	}

	FString BuildStructuredRouteDiagnostic(
		const FString& Summary,
		const TArray<FString>& DetailLines,
		const TArray<FString>& CandidateFailureLines = {})
	{
		FString Message = Summary;
		for (const FString& DetailLine : DetailLines)
		{
			if (!DetailLine.IsEmpty())
			{
				Message += TEXT("\n  ");
				Message += DetailLine;
			}
		}

		if (!CandidateFailureLines.IsEmpty())
		{
			Message += TEXT("\nCandidate failures:");
			for (const FString& CandidateFailureLine : CandidateFailureLines)
			{
				if (!CandidateFailureLine.IsEmpty())
				{
					TArray<FString> CandidateLines;
					CandidateFailureLine.ParseIntoArrayLines(CandidateLines, false);
					if (CandidateLines.IsEmpty())
					{
						Message += TEXT("\n  - ");
						Message += CandidateFailureLine;
						continue;
					}

					Message += TEXT("\n  - ");
					Message += CandidateLines[0];
					for (int32 CandidateLineIndex = 1; CandidateLineIndex < CandidateLines.Num(); ++CandidateLineIndex)
					{
						Message += TEXT("\n    ");
						Message += CandidateLines[CandidateLineIndex];
					}
				}
			}
		}

		return Message;
	}

	FString BuildEmptyRouteDomainMessage(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FLayoutRouteConstraintRecord& Constraint)
	{
		const FString RequirementSummary = BuildRouteConstraintRequirementSummary(Constraint);
		const TArray<FString>* AdmissionFailures =
			Context.InitialDomainAdmissionFailuresByCell.Find(Constraint.Cell);
		return BuildStructuredRouteDiagnostic(
			TEXT("Route-constrained planned cell has no eligible module candidates before route validation."),
			{
				FString::Printf(TEXT("Cell: %s"), *Constraint.Cell.ToString()),
				FString::Printf(TEXT("Intent: %s"), ToDebugString(Constraint.Intent)),
				FString::Printf(TEXT("Required faces: [%s]"), *RequirementSummary),
				TEXT("Root cause: every catalog variant admitted by the compiled intent mask failed base candidate admission; the required route faces were not evaluated.")
			},
			AdmissionFailures != nullptr ? *AdmissionFailures : TArray<FString>{TEXT("No bounded candidate-admission reason was recorded.")});
	}

	TArray<FGameplayTag> GetSortedTags(const FGameplayTagContainer& Tags)
	{
		TArray<FGameplayTag> Result;
		Tags.GetGameplayTagArray(Result);
		Result.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
		{
			return Left.ToString() < Right.ToString();
		});
		return Result;
	}

	void AddRequiredTraversalRouteFace(
		LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const ELayoutFaceDirection Direction,
		const FGameplayTag TraversalTag,
		const bool bScoreAsMainRoute = true)
	{
		if (!TraversalTag.IsValid() || !Context.PlannedCellIntents.Contains(Cell) || !IsHorizontalDirection(Direction))
		{
			return;
		}

		const int32* ExistingIndex = Context.RouteConstraintIndexByCell.Find(Cell);
		FLayoutRouteConstraintRecord* Constraint = (ExistingIndex != nullptr && Context.Result.RouteConstraints.IsValidIndex(*ExistingIndex))
			? &Context.Result.RouteConstraints[*ExistingIndex]
			: nullptr;
		if (Constraint == nullptr)
		{
			const int32 NewIndex = Context.Result.RouteConstraints.AddDefaulted();
			Context.RouteConstraintIndexByCell.Add(Cell, NewIndex);
			Constraint = &Context.Result.RouteConstraints[NewIndex];
			Constraint->ConstraintId = FLayoutId(*FString::Printf(TEXT("RouteConstraint_%d_%d_%d"), Cell.X, Cell.Y, Cell.Z));
			Constraint->Cell = Cell;
			Constraint->Intent = Context.PlannedCellIntents.FindRef(Cell);
		}

		FLayoutRouteFaceRequirement* ExistingRequirement = Constraint->FaceRequirements.FindByPredicate([Direction](const FLayoutRouteFaceRequirement& Requirement)
		{
			return Requirement.FaceDirection == Direction;
		});
		const bool bLowExposureVerticalAccess =
			Constraint->Intent == ELayoutCellIntent::VerticalAccess
			&& LayoutTraversalPlannerPrivate::GetBestHorizontalTraversalExposureForCell(Context, Cell) < 2;
		if (bLowExposureVerticalAccess && ExistingRequirement == nullptr && !Constraint->FaceRequirements.IsEmpty())
		{
			FLayoutRouteFaceRequirement& ChosenRequirement = Constraint->FaceRequirements[0];
			const bool bExistingFaceSupported =
				LayoutTraversalPlannerPrivate::DoesAnyCandidateExposeHorizontalTraversalFaceForCell(
					Context,
					Cell,
					ChosenRequirement.FaceDirection,
					ChosenRequirement.TraversalChannel);
			const bool bNewFaceSupported =
				LayoutTraversalPlannerPrivate::DoesAnyCandidateExposeHorizontalTraversalFaceForCell(
					Context,
					Cell,
					Direction,
					TraversalTag);
			if (bNewFaceSupported && !bExistingFaceSupported)
			{
				ChosenRequirement.FaceDirection = Direction;
				ChosenRequirement.TraversalChannel = TraversalTag;
			}
			return;
		}
		if (ExistingRequirement != nullptr)
		{
			ExistingRequirement->TraversalChannel = TraversalTag;
		}
		else
		{
			FLayoutRouteFaceRequirement& Requirement = Constraint->FaceRequirements.AddDefaulted_GetRef();
			Requirement.FaceDirection = Direction;
			Requirement.TraversalChannel = TraversalTag;
		}

		Constraint->bScoreAsMainRoute = Constraint->bScoreAsMainRoute || bScoreAsMainRoute;
	}

	uint8 BuildReservationMask(const ELayoutCellReservationKind ReservationKind)
	{
		return static_cast<uint8>(1 << static_cast<uint8>(ReservationKind));
	}

	void AddCompiledReservation(
		LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const ELayoutCellReservationKind ReservationKind)
	{
		if (!Context.PlannedCellIntents.Contains(Cell))
		{
			return;
		}

		const uint8 ReservationMask = BuildReservationMask(ReservationKind);
		uint8& ReservationFlags = Context.ReservationFlagsByCell.FindOrAdd(Cell);
		if ((ReservationFlags & ReservationMask) != 0)
		{
			return;
		}

		ReservationFlags |= ReservationMask;
		FLayoutCellReservationRecord& Reservation = Context.Result.CompiledReservations.AddDefaulted_GetRef();
		Reservation.ReservationId = FLayoutId(*FString::Printf(TEXT("%s_%d_%d_%d"),
			*StaticEnum<ELayoutCellReservationKind>()->GetNameStringByValue(static_cast<int64>(ReservationKind)),
			Cell.X,
			Cell.Y,
			Cell.Z));
		Reservation.Cell = Cell;
		Reservation.Intent = Context.PlannedCellIntents.FindRef(Cell);
		Reservation.ReservationKind = ReservationKind;
	}

	void SeedRequestOwnedRequiredRouteConstraints(
		LayoutProfileSolverInternal::FSolveContext& Context)
	{
		for (const FLayoutRouteConstraintRecord& RouteConstraint : Context.RequiredRouteConstraints)
		{
			for (const FLayoutRouteFaceRequirement& Requirement : RouteConstraint.FaceRequirements)
			{
				AddRequiredTraversalRouteFace(
					Context,
					RouteConstraint.Cell,
					Requirement.FaceDirection,
					Requirement.TraversalChannel,
					RouteConstraint.bScoreAsMainRoute);
			}

			AddCompiledReservation(
				Context,
				RouteConstraint.Cell,
				ELayoutCellReservationKind::RequiredRoute);
		}
	}

	bool CellHasReservation(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const ELayoutCellReservationKind ReservationKind)
	{
		const uint8* ReservationFlags = Context.ReservationFlagsByCell.Find(Cell);
		return ReservationFlags != nullptr && ((*ReservationFlags & BuildReservationMask(ReservationKind)) != 0);
	}

	const FLayoutRouteConstraintRecord* FindRouteConstraint(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell)
	{
		const int32* ConstraintIndex = Context.RouteConstraintIndexByCell.Find(Cell);
		return (ConstraintIndex != nullptr && Context.Result.RouteConstraints.IsValidIndex(*ConstraintIndex))
			? &Context.Result.RouteConstraints[*ConstraintIndex]
			: nullptr;
	}

	TArray<FGameplayTag> GetRequiredTraversalTagsForRouteCell(const FLayoutRouteConstraintRecord& Constraint)
	{
		FGameplayTagContainer Tags;
		for (const FLayoutRouteFaceRequirement& Requirement : Constraint.FaceRequirements)
		{
			if (Requirement.TraversalChannel.IsValid())
			{
				Tags.AddTag(Requirement.TraversalChannel);
			}
		}

		return GetSortedTags(Tags);
	}

	bool CandidateConnectsRequiredTraversalTags(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const LayoutProfileSolverInternal::FSolveCandidate& Candidate,
		const TArray<FGameplayTag>& RequiredTraversalTags)
	{
		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate) || RequiredTraversalTags.Num() <= 1)
		{
			return true;
		}

		TSet<FGameplayTag> ReachableTags;
		ReachableTags.Add(RequiredTraversalTags[0]);
		bool bChanged = true;
		while (bChanged)
		{
			bChanged = false;
			for (const FLayoutInternalAccessLink& Link : GetSolveCandidateInternalAccessLinks(Context, Candidate))
			{
				if (!Link.FromTraversalChannel.IsValid() || !Link.ToTraversalChannel.IsValid())
				{
					continue;
				}

				if (ReachableTags.Contains(Link.FromTraversalChannel) && !ReachableTags.Contains(Link.ToTraversalChannel))
				{
					ReachableTags.Add(Link.ToTraversalChannel);
					bChanged = true;
				}

				if (Link.bBidirectional && ReachableTags.Contains(Link.ToTraversalChannel) && !ReachableTags.Contains(Link.FromTraversalChannel))
				{
					ReachableTags.Add(Link.FromTraversalChannel);
					bChanged = true;
				}
			}
		}

		for (const FGameplayTag& RequiredTraversalTag : RequiredTraversalTags)
		{
			if (!ReachableTags.Contains(RequiredTraversalTag))
			{
				return false;
			}
		}

		return true;
	}

	int32 GetCandidateHorizontalTraversalExposureScore(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const LayoutProfileSolverInternal::FSolveCandidate& Candidate)
	{
		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
		{
			return 0;
		}

		int32 Score = 0;
		for (const ELayoutFaceDirection Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY})
		{
			FLayoutFaceRule FaceRule;
			if (!TryGetSolveCandidateFaceRule(Context, Candidate, Direction, FaceRule)
				|| !IsFaceCompatibleWithOccupancy(FaceRule, true, true))
			{
				continue;
			}

			for (const FGameplayTag& TraversalChannel : GetSolveCandidateTraversalChannels(Context, Candidate))
			{
				if (FaceRule.ConnectedTraversalChannels.HasTagExact(TraversalChannel))
				{
					++Score;
					break;
				}
			}
		}

		return Score;
	}

	/** Offers zone/level-eligible owning-intent and Connector variants for unclaimed terrain.
	 * Virtual routes cannot use GroundOnly floors in upper air; final local CSP proves faces and occupancy. */
	TArray<LayoutProfileSolverInternal::FSolveCandidate> BuildSparseRouteDiscoveryCandidates(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell)
	{
		TArray<LayoutProfileSolverInternal::FSolveCandidate> Candidates;
		if (!Context.TerrainResidualRuleIdByCell.Contains(Cell))
		{
			return Candidates;
		}
		const FLayoutPlannedCell* OwningCell = Context.OwningTopologyCellsByPhysicalCell.Find(Cell);
		const ELayoutCellIntent OwningIntent = OwningCell != nullptr ? OwningCell->Intent : ELayoutCellIntent::Interior;
		TArray<int32> VariantIndices;
		for (const ELayoutCellIntent Intent : {OwningIntent, ELayoutCellIntent::Connector})
		{
			if (const auto* Indices = Context.VariantIndicesByIntent.Find(Intent))
				for (const int32 Index : *Indices) VariantIndices.AddUnique(Index);
		}
		for (const int32 VariantIndex : VariantIndices)
		{
			if (!Context.Variants.IsValidIndex(VariantIndex))
			{
				continue;
			}
			const LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant =
				Context.Variants[VariantIndex];
			if (!LayoutProfileSolverInternal::DoesCellMatchResolvedPlacementZone(
					Context,
					Cell,
					Variant.PlacementZone)
				|| !DoesCellMatchLevelPlacementPolicy(Context, Cell, Variant.LevelPlacementPolicy, Variant.SpecificLevel)
				|| !Context.ModuleSnapshots.IsValidIndex(Variant.ModuleSnapshotIndex)
				|| (!Context.ModuleSnapshots[Variant.ModuleSnapshotIndex].SupportsIntent(OwningIntent)
					&& !Context.ModuleSnapshots[Variant.ModuleSnapshotIndex].SupportsIntent(ELayoutCellIntent::Connector)))
			{
				continue;
			}
			LayoutProfileSolverInternal::FSolveCandidate& Candidate = Candidates.AddDefaulted_GetRef();
			Candidate.bEmpty = false;
			Candidate.YawRotationSteps = Variant.YawRotationSteps;
			Candidate.VariantIndex = VariantIndex;
			Candidate.ModuleSnapshotIndex = Variant.ModuleSnapshotIndex;
			Candidate.ModuleSnapshotId = Variant.ModuleSnapshotId;
		}
		return Candidates;
	}

	bool DoesAnyCandidateExposeHorizontalTraversalFaceForCell(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const ELayoutFaceDirection Direction,
		const FGameplayTag TraversalTag)
	{
		if (!TraversalTag.IsValid() || !IsHorizontalDirection(Direction))
		{
			return false;
		}

		auto CandidateSupportsFace = [&](const LayoutProfileSolverInternal::FSolveCandidate& Candidate, const FIntVector& LocalBundleCell)
		{
			FLayoutFaceRule FaceRule;
			return LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(Context, Candidate, LocalBundleCell, Direction, FaceRule)
				&& FaceRule.ConnectedTraversalChannels.HasTagExact(TraversalTag)
				&& IsFaceCompatibleWithOccupancy(FaceRule, true, true);
		};

		if (const TArray<LayoutProfileSolverInternal::FSolveCandidate>* Domain = Context.InitialDomains.Find(Cell);
			Domain != nullptr && !Domain->IsEmpty())
		{
			for (const LayoutProfileSolverInternal::FSolveCandidate& Candidate : *Domain)
			{
				if (CandidateSupportsFace(Candidate, FIntVector::ZeroValue))
				{
					return true;
				}
			}
		}

		for (const TPair<FIntVector, TArray<LayoutProfileSolverInternal::FSolveCandidate>>& DomainPair : Context.InitialDomains)
		{
			const FIntVector& RootCell = DomainPair.Key;
			if (RootCell == Cell)
			{
				continue;
			}

			for (const LayoutProfileSolverInternal::FSolveCandidate& Candidate : DomainPair.Value)
			{
				const LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant* Variant =
					LayoutProfileSolverInternal::FindSolveCandidateVariant(Context, Candidate);
				if (Variant == nullptr || !Context.ModuleSnapshots.IsValidIndex(Variant->ModuleSnapshotIndex))
				{
					continue;
				}

				const FLayoutModuleSolveSnapshot& ModuleSnapshot = Context.ModuleSnapshots[Variant->ModuleSnapshotIndex];
				if (ModuleSnapshot.OccupiedLocalCells.Num() <= 1)
				{
					continue;
				}

				for (const FIntVector& LocalBundleCell : ModuleSnapshot.OccupiedLocalCells)
				{
					const FIntVector WorldCell = LayoutPlacementOccupancy::ProjectLocalCellToWorld(
						RootCell,
						LocalBundleCell,
						ModuleSnapshot.BoundsCells,
						Candidate.YawRotationSteps);
					if (WorldCell == Cell && CandidateSupportsFace(Candidate, LocalBundleCell))
					{
						return true;
					}
				}
			}
		}

		const ELayoutCellIntent* Intent = Context.PlannedCellIntents.Find(Cell);
		if (Intent == nullptr)
		{
			return false;
		}

		const TArray<int32>* VariantIndices = Context.VariantIndicesByIntent.Find(*Intent);
		if (VariantIndices == nullptr)
		{
			return false;
		}

		for (const int32 VariantIndex : *VariantIndices)
		{
			if (!Context.Variants.IsValidIndex(VariantIndex))
			{
				continue;
			}

			const LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant = Context.Variants[VariantIndex];
			if (!LayoutProfileSolverInternal::DoesCellMatchResolvedPlacementZone(
					Context,
					Cell,
					Variant.PlacementZone)
				|| !DoesCellMatchLevelPlacementPolicy(Context, Cell, Variant.LevelPlacementPolicy, Variant.SpecificLevel))
			{
				continue;
			}

			LayoutProfileSolverInternal::FSolveCandidate Candidate;
			Candidate.YawRotationSteps = Variant.YawRotationSteps;
			Candidate.VariantIndex = VariantIndex;
			Candidate.ModuleSnapshotIndex = Variant.ModuleSnapshotIndex;
			Candidate.ModuleSnapshotId = Variant.ModuleSnapshotId;
			Candidate.bEmpty = false;
			if (CandidateSupportsFace(Candidate, FIntVector::ZeroValue))
			{
				return true;
			}
		}

		return false;
	}

	int32 GetBestHorizontalTraversalExposureForCell(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell)
	{
		auto ForEachCandidateAtOrCoveringCell = [&](const auto& Visitor)
		{
			bool bVisitedAny = false;
			if (const TArray<LayoutProfileSolverInternal::FSolveCandidate>* Domain = Context.InitialDomains.Find(Cell);
				Domain != nullptr && !Domain->IsEmpty())
			{
				for (const LayoutProfileSolverInternal::FSolveCandidate& Candidate : *Domain)
				{
					Visitor(Candidate, FIntVector::ZeroValue);
					bVisitedAny = true;
				}
			}

			for (const TPair<FIntVector, TArray<LayoutProfileSolverInternal::FSolveCandidate>>& DomainPair : Context.InitialDomains)
			{
				const FIntVector& RootCell = DomainPair.Key;
				if (RootCell == Cell)
				{
					continue;
				}

				for (const LayoutProfileSolverInternal::FSolveCandidate& Candidate : DomainPair.Value)
				{
					const LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant* Variant =
						LayoutProfileSolverInternal::FindSolveCandidateVariant(Context, Candidate);
					if (Variant == nullptr || !Context.ModuleSnapshots.IsValidIndex(Variant->ModuleSnapshotIndex))
					{
						continue;
					}

					const FLayoutModuleSolveSnapshot& ModuleSnapshot = Context.ModuleSnapshots[Variant->ModuleSnapshotIndex];
					if (ModuleSnapshot.OccupiedLocalCells.Num() <= 1)
					{
						continue;
					}

					for (const FIntVector& LocalBundleCell : ModuleSnapshot.OccupiedLocalCells)
					{
						const FIntVector WorldCell = LayoutPlacementOccupancy::ProjectLocalCellToWorld(
							RootCell,
							LocalBundleCell,
							ModuleSnapshot.BoundsCells,
							Candidate.YawRotationSteps);
						if (WorldCell == Cell)
						{
							Visitor(Candidate, LocalBundleCell);
							bVisitedAny = true;
							break;
						}
					}
				}
			}

			return bVisitedAny;
		};

		int32 BestScore = 0;
		const bool bVisitedCandidates = ForEachCandidateAtOrCoveringCell(
			[&](const LayoutProfileSolverInternal::FSolveCandidate& Candidate, const FIntVector& LocalBundleCell)
			{
				int32 CandidateScore = 0;
				for (const ELayoutFaceDirection Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY})
				{
					FLayoutFaceRule FaceRule;
					if (!LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(Context, Candidate, LocalBundleCell, Direction, FaceRule)
						|| !IsFaceCompatibleWithOccupancy(FaceRule, true, true))
					{
						continue;
					}

					for (const FGameplayTag& TraversalChannel : GetSolveCandidateTraversalChannels(Context, Candidate))
					{
						if (FaceRule.ConnectedTraversalChannels.HasTagExact(TraversalChannel))
						{
							++CandidateScore;
							break;
						}
					}
				}

				BestScore = FMath::Max(BestScore, CandidateScore);
			});
		if (bVisitedCandidates && BestScore > 0)
		{
			return BestScore;
		}
		for (const LayoutProfileSolverInternal::FSolveCandidate& Candidate :
			BuildSparseRouteDiscoveryCandidates(Context, Cell))
		{
			BestScore = FMath::Max(
				BestScore,
				GetCandidateHorizontalTraversalExposureScore(Context, Candidate));
		}
		if (BestScore > 0)
		{
			return BestScore;
		}

		const ELayoutCellIntent* Intent = Context.PlannedCellIntents.Find(Cell);
		if (Intent == nullptr)
		{
			return 0;
		}

		const TArray<int32>* VariantIndices = Context.VariantIndicesByIntent.Find(*Intent);
		if (VariantIndices == nullptr)
		{
			return 0;
		}

		for (const int32 VariantIndex : *VariantIndices)
		{
			if (!Context.Variants.IsValidIndex(VariantIndex))
			{
				continue;
			}

			const LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant = Context.Variants[VariantIndex];
			if (!LayoutProfileSolverInternal::DoesCellMatchResolvedPlacementZone(
					Context,
					Cell,
					Variant.PlacementZone)
				|| !DoesCellMatchLevelPlacementPolicy(Context, Cell, Variant.LevelPlacementPolicy, Variant.SpecificLevel))
			{
				continue;
			}

			LayoutProfileSolverInternal::FSolveCandidate Candidate;
			Candidate.YawRotationSteps = Variant.YawRotationSteps;
			Candidate.VariantIndex = VariantIndex;
			Candidate.ModuleSnapshotIndex = Variant.ModuleSnapshotIndex;
			Candidate.ModuleSnapshotId = Variant.ModuleSnapshotId;
			Candidate.bEmpty = false;
			BestScore = FMath::Max(BestScore, GetCandidateHorizontalTraversalExposureScore(Context, Candidate));
		}

		return BestScore;
	}

	uint8 BuildHorizontalRouteFaceMask(const FLayoutRouteConstraintRecord& Constraint)
	{
		uint8 Mask = 0;
		for (const FLayoutRouteFaceRequirement& Requirement : Constraint.FaceRequirements)
		{
			if (!IsHorizontalDirection(Requirement.FaceDirection))
			{
				continue;
			}

			Mask |= static_cast<uint8>(1 << static_cast<uint8>(Requirement.FaceDirection));
		}

		return Mask;
	}

	FString DescribeHorizontalRouteConstraintMask(uint8 FaceMask);

	/** Validates route face demand against cell exposure and reports first stable blocker for authoring diagnostics. */
	bool CanPathFitHorizontalRouteFaceCapacity(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const TArray<FIntVector>& Path,
		FString* OutFailureReason = nullptr)
	{
		if (OutFailureReason != nullptr)
		{
			OutFailureReason->Reset();
		}
		if (Path.Num() < 2)
		{
			return true;
		}

		TMap<FIntVector, uint8> AddedFaceMasksByCell;
		for (int32 Index = 0; Index + 1 < Path.Num(); ++Index)
		{
			ELayoutFaceDirection Direction = ELayoutFaceDirection::PosX;
			if (!TryGetHorizontalDirectionBetweenCells(Path[Index], Path[Index + 1], Direction))
			{
				continue;
			}

			AddedFaceMasksByCell.FindOrAdd(Path[Index]) |= static_cast<uint8>(1 << static_cast<uint8>(Direction));
			AddedFaceMasksByCell.FindOrAdd(Path[Index + 1]) |= static_cast<uint8>(1 << static_cast<uint8>(FLayoutDirectionUtils::GetOpposite(Direction)));
		}

		TSet<FIntVector> EvaluatedCells;
		for (const FIntVector& PathCell : Path)
		{
			if (EvaluatedCells.Contains(PathCell))
			{
				continue;
			}
			EvaluatedCells.Add(PathCell);

			const uint8* AddedFaceMask = AddedFaceMasksByCell.Find(PathCell);
			if (AddedFaceMask == nullptr)
			{
				continue;
			}

			const int32 HorizontalExposure = GetBestHorizontalTraversalExposureForCell(Context, PathCell);
			uint8 RequiredFaceMask = *AddedFaceMask;
			if (const FLayoutRouteConstraintRecord* ExistingConstraint = FindRouteConstraint(Context, PathCell))
			{
				RequiredFaceMask |= BuildHorizontalRouteFaceMask(*ExistingConstraint);
			}

			if (HorizontalExposure <= 0 || FMath::CountBits(RequiredFaceMask) > HorizontalExposure)
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("cell %s fails route face capacity: required faces=[%s] count=%d, maximum compatible horizontal traversal faces=%d."),
						*PathCell.ToString(),
						*DescribeHorizontalRouteConstraintMask(RequiredFaceMask),
						FMath::CountBits(RequiredFaceMask),
						HorizontalExposure);
				}
				return false;
			}
		}

		return true;
	}

	FString DescribeHorizontalRouteConstraintMask(const uint8 FaceMask)
	{
		TArray<FString> FaceNames;
		for (const ELayoutFaceDirection Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY})
		{
			if ((FaceMask & static_cast<uint8>(1 << static_cast<uint8>(Direction))) != 0)
			{
				FaceNames.Add(StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Direction)));
			}
		}

		return FaceNames.IsEmpty() ? TEXT("<none>") : FString::Join(FaceNames, TEXT(","));
	}

	bool CanInitialDomainsShareHorizontalTraversal(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const FIntVector& Neighbor,
		const ELayoutFaceDirection Direction);

	FString DescribeRouteAnchorCellState(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell)
	{
		const ELayoutCellIntent* Intent = Context.PlannedCellIntents.Find(Cell);
		const FString IntentName = Intent != nullptr
			? StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(static_cast<int64>(*Intent))
			: TEXT("<none>");
		const int32 HorizontalExposure = GetBestHorizontalTraversalExposureForCell(Context, Cell);
		const FLayoutRouteConstraintRecord* ExistingConstraint = FindRouteConstraint(Context, Cell);
		const uint8 ExistingFaceMask = ExistingConstraint != nullptr
			? BuildHorizontalRouteFaceMask(*ExistingConstraint)
			: 0;
		const TArray<LayoutProfileSolverInternal::FSolveCandidate>* Domain = Context.InitialDomains.Find(Cell);
		const int32 DomainSize = Domain != nullptr ? Domain->Num() : 0;
		const TArray<FString>* AdmissionFailures = Context.InitialDomainAdmissionFailuresByCell.Find(Cell);
		const FString FirstAdmissionFailure = AdmissionFailures != nullptr && !AdmissionFailures->IsEmpty()
			? (*AdmissionFailures)[0]
			: TEXT("<none>");
		return FString::Printf(
			TEXT("%s intent=%s domain=%d exposure=%d existingFaces=[%s] firstAdmissionFailure=%s"),
			*Cell.ToString(),
			*IntentName,
			DomainSize,
			HorizontalExposure,
			*DescribeHorizontalRouteConstraintMask(ExistingFaceMask),
			*FirstAdmissionFailure);
	}

	/** Describes exact candidate face directions and reciprocal neighbor edges for one rejected route anchor. */
	FString DescribeRouteAnchorDomainEdges(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell)
	{
		TArray<FString> CandidateDescriptions;
		if (const TArray<LayoutProfileSolverInternal::FSolveCandidate>* Domain = Context.InitialDomains.Find(Cell))
		{
			for (const LayoutProfileSolverInternal::FSolveCandidate& Candidate : *Domain)
			{
				uint8 TraversalFaceMask = 0;
				for (const ELayoutFaceDirection Direction : {
					ELayoutFaceDirection::PosX,
					ELayoutFaceDirection::NegX,
					ELayoutFaceDirection::PosY,
					ELayoutFaceDirection::NegY})
				{
					FLayoutFaceRule FaceRule;
					if (TryGetSolveCandidateFaceRule(Context, Candidate, Direction, FaceRule)
						&& IsFaceCompatibleWithOccupancy(FaceRule, true, true)
						&& !FaceRule.ConnectedTraversalChannels.IsEmpty())
					{
						TraversalFaceMask |= static_cast<uint8>(1 << static_cast<uint8>(Direction));
					}
				}
				CandidateDescriptions.Add(FString::Printf(
					TEXT("%s yaw=%d faces=[%s]"),
					*Candidate.ModuleSnapshotId.ToString(),
					Candidate.YawRotationSteps,
					*DescribeHorizontalRouteConstraintMask(TraversalFaceMask)));
			}
		}

		TArray<FString> NeighborEdges;
		for (const ELayoutFaceDirection Direction : {
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY})
		{
			const FIntVector Neighbor = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
			NeighborEdges.Add(FString::Printf(
				TEXT("%s:%s%s"),
				*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Direction)),
				Context.PlannedCellIntents.Contains(Neighbor) ? TEXT("planned/") : TEXT("absent/"),
				CanInitialDomainsShareHorizontalTraversal(Context, Cell, Neighbor, Direction)
					? TEXT("reciprocal")
					: TEXT("blocked")));
		}
		return FString::Printf(
			TEXT("candidates={%s} neighborEdges={%s}"),
			CandidateDescriptions.IsEmpty() ? TEXT("<none>") : *FString::Join(CandidateDescriptions, TEXT(" | ")),
			*FString::Join(NeighborEdges, TEXT(", ")));
	}

	bool CellCanServeAsRouteJunction(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell)
	{
		return GetBestHorizontalTraversalExposureForCell(Context, Cell) >= 2;
	}

	int32 GetCandidateRequiredTraversalRouteScore(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const LayoutProfileSolverInternal::FSolveCandidate& Candidate)
	{
		const FLayoutRouteConstraintRecord* Constraint = FindRouteConstraint(Context, Cell);
		if (Constraint == nullptr || !LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
		{
			return 0;
		}

		if (!Constraint->bScoreAsMainRoute)
		{
			return 0;
		}

		int32 Score = 0;
		const TArray<FGameplayTag> RequiredTraversalTags = GetRequiredTraversalTagsForRouteCell(*Constraint);
		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
			const FLayoutRouteFaceRequirement* RequiredTraversalTag = Constraint->FaceRequirements.FindByPredicate([Direction](const FLayoutRouteFaceRequirement& Requirement)
			{
				return Requirement.FaceDirection == Direction;
			});
			FLayoutFaceRule FaceRule;
			if (!LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(Context, Candidate, FIntVector::ZeroValue, Direction, FaceRule))
			{
				continue;
			}

			if (RequiredTraversalTag != nullptr && RequiredTraversalTag->TraversalChannel.IsValid() && FaceRule.ConnectedTraversalChannels.HasTagExact(RequiredTraversalTag->TraversalChannel))
			{
				Score += 10;
				continue;
			}

			for (const FGameplayTag& TraversalTag : RequiredTraversalTags)
			{
				if (FaceRule.ConnectedTraversalChannels.HasTagExact(TraversalTag))
				{
					++Score;
					break;
				}
			}
		}

		if (CandidateConnectsRequiredTraversalTags(Context, Candidate, RequiredTraversalTags))
		{
			Score += RequiredTraversalTags.Num() > 1 ? 8 : 0;
		}

		return Score;
	}

	struct FRouteConstraintCandidateFailureDetail
	{
		ELayoutRouteDomainFailureKind FailureKind =
			ELayoutRouteDomainFailureKind::None;
	};

	bool DoesCandidateSatisfyRequiredTraversalRoute(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const LayoutProfileSolverInternal::FSolveCandidate& Candidate,
		FString* OutFailureReason = nullptr,
		FRouteConstraintCandidateFailureDetail* OutFailureDetail = nullptr,
		const FIntVector& CandidateLocalCell = FIntVector::ZeroValue)
	{
		const FLayoutRouteConstraintRecord* Constraint = FindRouteConstraint(Context, Cell);
		if (Constraint == nullptr)
		{
			return true;
		}

		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
		{
			if (OutFailureDetail != nullptr)
			{
				OutFailureDetail->FailureKind =
					ELayoutRouteDomainFailureKind::EmptyCandidate;
			}
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = TEXT("Problem: empty candidate cannot satisfy a route-constrained cell.\nFix: Add a real module candidate for this planned cell.");
			}
			return false;
		}

		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
			const FLayoutRouteFaceRequirement* RequiredTraversalTag = Constraint->FaceRequirements.FindByPredicate([Direction](const FLayoutRouteFaceRequirement& Requirement)
			{
				return Requirement.FaceDirection == Direction;
			});
			if (RequiredTraversalTag == nullptr)
			{
				continue;
			}

			if (!RequiredTraversalTag->TraversalChannel.IsValid())
			{
				if (OutFailureDetail != nullptr)
				{
					OutFailureDetail->FailureKind =
						ELayoutRouteDomainFailureKind::MissingRequiredTraversalChannel;
				}
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("Problem: route requirement is missing a traversal channel.\nFace: %s\nFix: Rebuild the route constraint so this face asks for a real traversal channel."),
						*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Direction)));
				}
				return false;
			}

			FLayoutFaceRule FaceRule;
			if (!LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(Context, Candidate, CandidateLocalCell, Direction, FaceRule))
			{
				if (OutFailureDetail != nullptr)
				{
					OutFailureDetail->FailureKind =
						ELayoutRouteDomainFailureKind::MissingFaceRule;
				}
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("Problem: candidate is missing the required face rule.\nFace: %s\nFix: Author the missing face rule or use a different module for this route cell."),
						*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Direction)));
				}
				return false;
			}

			if (!FaceRule.ConnectedTraversalChannels.HasTagExact(RequiredTraversalTag->TraversalChannel))
			{
				if (OutFailureDetail != nullptr)
				{
					OutFailureDetail->FailureKind =
						ELayoutRouteDomainFailureKind::MissingFaceTraversalChannel;
				}
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("Problem: required traversal channel is missing on the route face.\nFace: %s\nRequired traversal: %s\nCandidate traversal: [%s]\nFix: Add the required traversal channel to this face, or use a module whose face already exposes it."),
						*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Direction)),
						*RequiredTraversalTag->TraversalChannel.ToString(),
						*FaceRule.ConnectedTraversalChannels.ToString());
				}
				return false;
			}

			if (!IsFaceCompatibleWithOccupancy(FaceRule, true, true))
			{
				if (OutFailureDetail != nullptr)
				{
					OutFailureDetail->FailureKind =
						ELayoutRouteDomainFailureKind::IncompatibleFaceOccupancy;
				}
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("Problem: route face cannot connect to the filled neighbor the path requires.\nFace: %s\nOccupancy: %s\nFix: Use a filled-neighbor-compatible occupancy policy on this face for routed cells."),
						*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Direction)),
						*StaticEnum<ELayoutFaceOccupancyPolicy>()->GetNameStringByValue(static_cast<int64>(FaceRule.OccupancyPolicy)));
				}
				return false;
			}
		}

		const TArray<FGameplayTag> RequiredTraversalTags = GetRequiredTraversalTagsForRouteCell(*Constraint);
		if (!CandidateConnectsRequiredTraversalTags(Context, Candidate, RequiredTraversalTags))
		{
			if (OutFailureDetail != nullptr)
			{
				OutFailureDetail->FailureKind =
					ELayoutRouteDomainFailureKind::MissingInternalTraversalConnectivity;
			}
			if (OutFailureReason != nullptr)
			{
				TArray<FString> RequiredTraversalNames;
				for (const FGameplayTag& RequiredTraversalTag : RequiredTraversalTags)
				{
					RequiredTraversalNames.Add(RequiredTraversalTag.ToString());
				}
				*OutFailureReason = FString::Printf(
					TEXT("Problem: required route channels are not internally connected inside the module.\nRequired channels: [%s]\nFix: Add internal access links that connect these channels, or collapse the faces onto one shared traversal channel."),
					RequiredTraversalNames.IsEmpty() ? TEXT("<none>") : *FString::Join(RequiredTraversalNames, TEXT(",")));
			}
			return false;
		}

		return true;
	}

	void AddTraversalTagsForVariantFace(
		const LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant,
		const ELayoutFaceDirection Direction,
		FGameplayTagContainer& OutTags)
	{
		const FLayoutFaceRule* FaceRule = Variant.WorldFaceRules.FindRule(Direction);
		if (FaceRule == nullptr || !IsHorizontalDirection(Direction))
		{
			return;
		}

		OutTags.AppendTags(FaceRule->ConnectedTraversalChannels);
	}

	FGameplayTag ChooseRouteAnchorTraversalTagForCell(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const FGameplayTag DefaultTraversalTag)
	{
		const ELayoutCellIntent* Intent = Context.PlannedCellIntents.Find(Cell);
		if (Intent == nullptr)
		{
			return DefaultTraversalTag;
		}

		FGameplayTagContainer CandidateTags;
		if (const TArray<int32>* VariantIndices = Context.VariantIndicesByIntent.Find(*Intent))
		{
			for (const int32 VariantIndex : *VariantIndices)
			{
				if (!Context.Variants.IsValidIndex(VariantIndex))
				{
					continue;
				}

				const LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant = Context.Variants[VariantIndex];
				for (const ELayoutFaceDirection Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY})
				{
					AddTraversalTagsForVariantFace(Variant, Direction, CandidateTags);
				}
			}
		}

		const TArray<FGameplayTag> SortedTags = GetSortedTags(CandidateTags);
		return !SortedTags.IsEmpty() ? SortedTags[0] : DefaultTraversalTag;
	}

	bool ShouldAllowBoundaryCellsAsIntermediateRouteSteps(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell)
	{
		// Upper-boundary-only profiles intentionally use the upper shell itself as the walkable
		// continuation ring, so same-level route planning must not reject non-goal boundary cells.
		// Stepped bridge/shift cells use the same shared exception in root and continuation solves;
		// ordinary shell Boundary cells remain governed by level fill policy.
		return (Context.ModuleLevelByCell.FindRef(Cell) > 0
			&& GetLevelFillModeForCell(Context, Cell) == ELayoutLevelFillMode::BoundaryOnly)
			|| Context.SteppedTraversalDeckCells.Contains(Cell);
	}

	bool DoesCandidateSupportProspectiveRouteFace(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const LayoutProfileSolverInternal::FSolveCandidate& Candidate,
		const FIntVector& CandidateLocalCell,
		const ELayoutFaceDirection Direction,
		const FGameplayTag TraversalTag)
	{
		FLayoutFaceRule FaceRule;
		return DoesCandidateSatisfyRequiredTraversalRoute(
				Context,
				Cell,
				Candidate,
				nullptr,
				nullptr,
				CandidateLocalCell)
			&& LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(
				Context,
				Candidate,
				CandidateLocalCell,
				Direction,
				FaceRule)
			&& FaceRule.ConnectedTraversalChannels.HasTagExact(TraversalTag)
			&& IsFaceCompatibleWithOccupancy(FaceRule, true, true);
	}

	bool DoesCellHaveCompatibleRouteEndpointCandidate(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const ELayoutFaceDirection Direction,
		const FGameplayTag TraversalTag)
	{
		if (const TArray<LayoutProfileSolverInternal::FSolveCandidate>* Domain = Context.InitialDomains.Find(Cell))
		{
			if (Domain->ContainsByPredicate([&Context, &Cell, Direction, TraversalTag](const LayoutProfileSolverInternal::FSolveCandidate& Candidate)
			{
				return DoesCandidateSupportProspectiveRouteFace(
					Context,
					Cell,
					Candidate,
					FIntVector::ZeroValue,
					Direction,
					TraversalTag);
			}))
			{
				return true;
			}
		}
		for (const LayoutProfileSolverInternal::FSolveCandidate& Candidate :
			BuildSparseRouteDiscoveryCandidates(Context, Cell))
		{
			if (DoesCandidateSupportProspectiveRouteFace(
					Context,
					Cell,
					Candidate,
					FIntVector::ZeroValue,
					Direction,
					TraversalTag))
			{
				return true;
			}
		}

		const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement* const FixedPlacement =
			Context.Placements.Find(Cell);
		if (FixedPlacement == nullptr || !LayoutProfileSolverInternal::FSolveContext::IsOccupiedPlacement(*FixedPlacement))
		{
			return false;
		}
		for (const TPair<FIntVector, TArray<LayoutProfileSolverInternal::FSolveCandidate>>& DomainPair : Context.InitialDomains)
		{
			if (DomainPair.Key != FixedPlacement->BundleRootCell)
			{
				continue;
			}
			for (const LayoutProfileSolverInternal::FSolveCandidate& Candidate : DomainPair.Value)
			{
				if (Candidate.VariantIndex != FixedPlacement->VariantIndex
					|| Candidate.YawRotationSteps != FixedPlacement->YawRotationSteps)
				{
					continue;
				}
				const LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant* Variant =
					LayoutProfileSolverInternal::FindSolveCandidateVariant(Context, Candidate);
				if (Variant == nullptr || !Context.ModuleSnapshots.IsValidIndex(Variant->ModuleSnapshotIndex))
				{
					continue;
				}
				const FLayoutModuleSolveSnapshot& ModuleSnapshot = Context.ModuleSnapshots[Variant->ModuleSnapshotIndex];
				if (ModuleSnapshot.OccupiedLocalCells.Num() <= 1)
				{
					continue;
				}
				for (const FIntVector& LocalCell : ModuleSnapshot.OccupiedLocalCells)
				{
					if (LayoutPlacementOccupancy::ProjectLocalCellToWorld(
							DomainPair.Key,
							LocalCell,
							ModuleSnapshot.BoundsCells,
							Candidate.YawRotationSteps) == Cell
						&& DoesCandidateSupportProspectiveRouteFace(
							Context,
							Cell,
							Candidate,
							LocalCell,
							Direction,
							TraversalTag))
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	int32 GetTraversalRouteCellCost(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const FIntVector& GoalCell,
		const bool bAllowDomainProvenBoundaryFallback)
	{
		const ELayoutCellIntent* Intent = Context.PlannedCellIntents.Find(Cell);
		if (Intent == nullptr)
		{
			return MAX_int32;
		}

		if (Context.TerrainSeamFaceMasksByCell.Contains(Cell)
			&& *Intent != ELayoutCellIntent::Entry)
		{
			return MAX_int32;
		}

		int32 Cost = 10;
		const bool bReservedTraversalContinuation =
			LayoutTraversalPlannerPrivate::CellHasReservation(Context, Cell, ELayoutCellReservationKind::RequiredRoute)
			|| LayoutTraversalPlannerPrivate::CellHasReservation(Context, Cell, ELayoutCellReservationKind::FutureCompoundConnection)
			|| LayoutTraversalPlannerPrivate::CellHasReservation(Context, Cell, ELayoutCellReservationKind::RouteJunction)
			|| LayoutTraversalPlannerPrivate::CellHasReservation(Context, Cell, ELayoutCellReservationKind::VerticalContinuation);
		switch (*Intent)
		{
		case ELayoutCellIntent::Core:
		case ELayoutCellIntent::Interior:
		case ELayoutCellIntent::Connector:
			Cost = 10;
			break;
		case ELayoutCellIntent::Entry:
			if (Cell != GoalCell && !bReservedTraversalContinuation)
			{
				return MAX_int32;
			}
			Cost = 12;
			break;
		case ELayoutCellIntent::VerticalAccess:
			if (Cell != GoalCell)
			{
				// Keep ordinary stair pockets out of generic same-level route expansion, but
				// allow stepped/root-prepared route cells that already carry multiple explicit
				// horizontal face obligations to act as intermediate corridor anchors. Without
				// that, broader stepped branch/corridor families can disconnect even though the
				// frozen request contract has already proven a real multi-face route demand on
				// that vertical-access cell.
				const FLayoutRouteConstraintRecord* ExistingConstraint = FindRouteConstraint(Context, Cell);
				const bool bCarriesMultiFaceHorizontalRouteDemand =
					ExistingConstraint != nullptr
					&& FMath::CountBits(BuildHorizontalRouteFaceMask(*ExistingConstraint)) >= 2;
				if (!bReservedTraversalContinuation && !bCarriesMultiFaceHorizontalRouteDemand)
				{
					return MAX_int32;
				}
			}
			Cost = 12;
			break;
		case ELayoutCellIntent::Boundary:
			if (Cell != GoalCell
				&& !bReservedTraversalContinuation
				&& !ShouldAllowBoundaryCellsAsIntermediateRouteSteps(Context, Cell)
				&& (!bAllowDomainProvenBoundaryFallback
					|| GetBestHorizontalTraversalExposureForCell(Context, Cell) < 2))
			{
				return MAX_int32;
			}
			// Domain-proven walkable shell remains a higher-cost fallback when
			// child reservation leaves no interior corridor around required anchors.
			Cost = IsBoundaryCell(Cell, Context.FootprintSize) ? 14 : 10;
			break;
		default:
			Cost = 20;
			break;
		}

		if (Cell != GoalCell && GetBestHorizontalTraversalExposureForCell(Context, Cell) < 2)
		{
			return MAX_int32;
		}

		const float CenterX = static_cast<float>(Context.FootprintSize.X - 1) * 0.5f;
		const float CenterY = static_cast<float>(Context.FootprintSize.Y - 1) * 0.5f;
		const int32 CenterBias = FMath::RoundToInt(FMath::Abs(static_cast<float>(Cell.X) - CenterX) + FMath::Abs(static_cast<float>(Cell.Y) - CenterY));
		return Cost + CenterBias;
	}

	struct FTraversalRouteSearchState
	{
		FIntVector Cell = FIntVector::ZeroValue;
		int8 IncomingDirectionIndex = INDEX_NONE;

		bool operator==(const FTraversalRouteSearchState& Other) const
		{
			return Cell == Other.Cell
				&& IncomingDirectionIndex == Other.IncomingDirectionIndex;
		}

		friend uint32 GetTypeHash(const FTraversalRouteSearchState& State)
		{
			return HashCombineFast(
				GetTypeHash(State.Cell),
				GetTypeHash(State.IncomingDirectionIndex));
		}
	};

	/** Returns whether one exact candidate can carry a route through both requested faces. */
	bool DoesCellHaveCompatibleRouteThroughCandidate(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const ELayoutFaceDirection IncomingDirection,
		const ELayoutFaceDirection OutgoingDirection,
		const FGameplayTag TraversalTag)
	{
		const TArray<LayoutProfileSolverInternal::FSolveCandidate>* Domain =
			Context.InitialDomains.Find(Cell);
		// Unclaimed sparse terrain has no CSP domain yet; its virtual Connector
		// offers still need one candidate carrying both faces before A* may cross it.
		TArray<LayoutProfileSolverInternal::FSolveCandidate> RouteCandidates = BuildSparseRouteDiscoveryCandidates(Context, Cell);
		if (Domain != nullptr) RouteCandidates.Append(*Domain);
		for (const LayoutProfileSolverInternal::FSolveCandidate& Candidate : RouteCandidates)
		{
			if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
			{
				continue;
			}
			FLayoutFaceRule IncomingFaceRule;
			FLayoutFaceRule OutgoingFaceRule;
			if (TryGetSolveCandidateFaceRule(
					Context,
					Candidate,
					IncomingDirection,
					IncomingFaceRule)
				&& TryGetSolveCandidateFaceRule(
					Context,
					Candidate,
					OutgoingDirection,
					OutgoingFaceRule)
				&& IncomingFaceRule.ConnectedTraversalChannels.HasTagExact(TraversalTag)
				&& OutgoingFaceRule.ConnectedTraversalChannels.HasTagExact(TraversalTag)
				&& IsFaceCompatibleWithOccupancy(IncomingFaceRule, true, true)
				&& IsFaceCompatibleWithOccupancy(OutgoingFaceRule, true, true))
			{
				return true;
			}
		}
		return false;
	}

	/** Searches one same-level route and retains the first rejected move. Exact endpoint proofs
	 * must come from selected sparse ports with nonzero face masks; composite upper cells need
	 * their proved local-cell faces, not root-only placeholder domains. Final joint CSP remains mandatory. */
	bool FindTraversalRoutePath(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& StartCell,
		const FIntVector& GoalCell,
		const FGameplayTag StartTraversalTag,
		const FGameplayTag GoalTraversalTag,
		TArray<FIntVector>& OutPath,
		FString* OutFailureReason = nullptr,
		const bool bAllowDomainProvenBoundaryFallback = false,
		const uint8 StartRouteFaceMask = 0,
		const uint8 GoalRouteFaceMask = 0,
		const TSet<FIntVector>* ExcludedCells = nullptr,
		LayoutProfileSolverInternal::FSolveContext* SparseWorkContext = nullptr,
		const bool bHasExactEndpointProofs = false)
	{
		check(!bHasExactEndpointProofs || (StartRouteFaceMask != 0 && GoalRouteFaceMask != 0));
		OutPath.Reset();
		if (OutFailureReason != nullptr)
		{
			OutFailureReason->Reset();
		}
		auto RecordFirstFailure = [OutFailureReason](const FString& FailureReason)
		{
			if (OutFailureReason != nullptr && OutFailureReason->IsEmpty())
			{
				*OutFailureReason = FailureReason;
			}
		};
		if (StartCell == GoalCell)
		{
			OutPath.Add(StartCell);
			return true;
		}

		if (StartCell.Z != GoalCell.Z
			|| !Context.PlannedCellIntents.Contains(StartCell)
			|| !Context.PlannedCellIntents.Contains(GoalCell))
		{
			RecordFirstFailure(FString::Printf(
				TEXT("route endpoints are invalid: start=%s goal=%s sameLevel=%d startPlanned=%d goalPlanned=%d."),
				*StartCell.ToString(),
				*GoalCell.ToString(),
				StartCell.Z == GoalCell.Z ? 1 : 0,
				Context.PlannedCellIntents.Contains(StartCell) ? 1 : 0,
				Context.PlannedCellIntents.Contains(GoalCell) ? 1 : 0));
			return false;
		}

		TSet<FTraversalRouteSearchState> OpenSet;
		TSet<FTraversalRouteSearchState> ClosedSet;
		TMap<FTraversalRouteSearchState, int32> CostByState;
		TMap<FTraversalRouteSearchState, FTraversalRouteSearchState> CameFrom;
		auto GetGoalDistance = [&GoalCell](const FIntVector& Cell) -> int32
		{
			return FMath::Abs(Cell.X - GoalCell.X) + FMath::Abs(Cell.Y - GoalCell.Y);
		};
		auto IsPreferredState = [&GetGoalDistance](
			const FTraversalRouteSearchState& Left,
			const FTraversalRouteSearchState& Right) -> bool
		{
			const int32 LeftDistance = GetGoalDistance(Left.Cell);
			const int32 RightDistance = GetGoalDistance(Right.Cell);
			if (LeftDistance != RightDistance)
			{
				return LeftDistance < RightDistance;
			}
			if (Left.Cell.Y != Right.Cell.Y)
			{
				return Left.Cell.Y < Right.Cell.Y;
			}
			if (Left.Cell.X != Right.Cell.X)
			{
				return Left.Cell.X < Right.Cell.X;
			}
			return Left.IncomingDirectionIndex < Right.IncomingDirectionIndex;
		};
		const FTraversalRouteSearchState StartState{
			StartCell,
			static_cast<int8>(INDEX_NONE)};
		OpenSet.Add(StartState);
		CostByState.Add(StartState, 0);

		while (!OpenSet.IsEmpty())
		{
			if (SparseWorkContext != nullptr && !ConsumeSparseRouteWork(*SparseWorkContext))
			{
				RecordFirstFailure(SparseWorkContext->Result.FailureReason);
				return false;
			}
			FTraversalRouteSearchState CurrentState;
			int32 CurrentCost = MAX_int32;
			bool bFoundCurrentState = false;
			for (const FTraversalRouteSearchState& CandidateState : OpenSet)
			{
				const int32* CandidateCost = CostByState.Find(CandidateState);
				if (CandidateCost != nullptr
					&& (!bFoundCurrentState
						|| *CandidateCost < CurrentCost
						|| (*CandidateCost == CurrentCost
							&& IsPreferredState(CandidateState, CurrentState))))
				{
					CurrentState = CandidateState;
					CurrentCost = *CandidateCost;
					bFoundCurrentState = true;
				}
			}
			if (!bFoundCurrentState)
			{
				break;
			}

			OpenSet.Remove(CurrentState);
			const FIntVector& CurrentCell = CurrentState.Cell;
			if (CurrentCell == GoalCell)
			{
				FTraversalRouteSearchState PathState = CurrentState;
				while (true)
				{
					OutPath.Insert(PathState.Cell, 0);
					if (PathState == StartState)
					{
						return true;
					}

					const FTraversalRouteSearchState* PreviousState =
						CameFrom.Find(PathState);
					if (PreviousState == nullptr)
					{
						OutPath.Reset();
						return false;
					}

					PathState = *PreviousState;
				}
			}

			ClosedSet.Add(CurrentState);
			for (const ELayoutFaceDirection Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY})
			{
				const FIntVector NeighborCell = CurrentCell + FLayoutDirectionUtils::ToCellDelta(Direction);
				const FTraversalRouteSearchState NeighborState{
					NeighborCell,
					static_cast<int8>(FLayoutDirectionUtils::GetOpposite(Direction))};
				if (ClosedSet.Contains(NeighborState)
					|| !Context.PlannedCellIntents.Contains(NeighborCell)
					|| (ExcludedCells != nullptr
						&& NeighborCell != GoalCell
						&& ExcludedCells->Contains(NeighborCell)))
				{
					continue;
				}
				if (CurrentCell == StartCell
					&& StartRouteFaceMask != 0
					&& !LayoutFaceMaskContainsDirection(StartRouteFaceMask, Direction))
				{
					continue;
				}
				if (NeighborCell == GoalCell
					&& GoalRouteFaceMask != 0
					&& !LayoutFaceMaskContainsDirection(
						GoalRouteFaceMask,
						FLayoutDirectionUtils::GetOpposite(Direction)))
				{
					continue;
				}
				if (CurrentState.IncomingDirectionIndex != INDEX_NONE
					&& !DoesCellHaveCompatibleRouteThroughCandidate(
						Context,
						CurrentCell,
						static_cast<ELayoutFaceDirection>(
							CurrentState.IncomingDirectionIndex),
						Direction,
						StartTraversalTag))
				{
					RecordFirstFailure(FString::Printf(
						TEXT("cell %s has no exact candidate carrying traversal channel %s through faces %s and %s."),
						*CurrentCell.ToString(),
						StartTraversalTag.IsValid()
							? *StartTraversalTag.ToString()
							: TEXT("<none>"),
						*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(
							static_cast<int64>(CurrentState.IncomingDirectionIndex)),
						*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(
							static_cast<int64>(Direction))));
					continue;
				}
				if (CurrentCell == StartCell && !bHasExactEndpointProofs
					&& !DoesCellHaveCompatibleRouteEndpointCandidate(Context, StartCell, Direction, StartTraversalTag))
				{
					RecordFirstFailure(FString::Printf(
						TEXT("start endpoint %s has no candidate exposing %s for traversal channel %s."),
						*StartCell.ToString(),
						*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Direction)),
						StartTraversalTag.IsValid() ? *StartTraversalTag.ToString() : TEXT("<none>")));
					continue;
				}
				if (NeighborCell == GoalCell && !bHasExactEndpointProofs
					&& !DoesCellHaveCompatibleRouteEndpointCandidate(
						Context,
						GoalCell,
						FLayoutDirectionUtils::GetOpposite(Direction),
						GoalTraversalTag))
				{
					RecordFirstFailure(FString::Printf(
						TEXT("goal endpoint %s has no candidate exposing %s for traversal channel %s."),
						*GoalCell.ToString(),
						*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(FLayoutDirectionUtils::GetOpposite(Direction))),
						GoalTraversalTag.IsValid() ? *GoalTraversalTag.ToString() : TEXT("<none>")));
					continue;
				}

				const int32 StepCost = (NeighborCell == GoalCell && bHasExactEndpointProofs) ? 10 : GetTraversalRouteCellCost(
					Context,
					NeighborCell,
					GoalCell,
					bAllowDomainProvenBoundaryFallback);
				if (StepCost == MAX_int32)
				{
					const ELayoutCellIntent* NeighborIntent = Context.PlannedCellIntents.Find(NeighborCell);
					const int32 HorizontalExposure = GetBestHorizontalTraversalExposureForCell(Context, NeighborCell);
					if (Context.TerrainSeamFaceMasksByCell.Contains(NeighborCell)
						&& (NeighborIntent == nullptr || *NeighborIntent != ELayoutCellIntent::Entry))
					{
						RecordFirstFailure(FString::Printf(
							TEXT("cell %s is terrain-seam intent=%s; non-Entry terrain-seam cells are excluded from same-level route intermediates."),
							*NeighborCell.ToString(),
							NeighborIntent != nullptr ? ToDebugString(*NeighborIntent) : TEXT("<none>")));
					}
					else if (NeighborIntent != nullptr && *NeighborIntent == ELayoutCellIntent::Entry)
					{
						RecordFirstFailure(FString::Printf(TEXT("cell %s is an unreserved Entry and cannot be a route intermediate."), *NeighborCell.ToString()));
					}
					else if (NeighborIntent != nullptr && *NeighborIntent == ELayoutCellIntent::VerticalAccess)
					{
						const FLayoutRouteConstraintRecord* ExistingConstraint = FindRouteConstraint(Context, NeighborCell);
						const bool bCarriesMultiFaceHorizontalRouteDemand =
							ExistingConstraint != nullptr
							&& FMath::CountBits(BuildHorizontalRouteFaceMask(*ExistingConstraint)) >= 2;
						const bool bReservedTraversalContinuation =
							LayoutTraversalPlannerPrivate::CellHasReservation(Context, NeighborCell, ELayoutCellReservationKind::RequiredRoute)
							|| LayoutTraversalPlannerPrivate::CellHasReservation(Context, NeighborCell, ELayoutCellReservationKind::FutureCompoundConnection)
							|| LayoutTraversalPlannerPrivate::CellHasReservation(Context, NeighborCell, ELayoutCellReservationKind::RouteJunction)
							|| LayoutTraversalPlannerPrivate::CellHasReservation(Context, NeighborCell, ELayoutCellReservationKind::VerticalContinuation);
						RecordFirstFailure(!bReservedTraversalContinuation && !bCarriesMultiFaceHorizontalRouteDemand
							? FString::Printf(TEXT("cell %s is a VerticalAccess intermediate without route-continuation reservation or two horizontal route faces."), *NeighborCell.ToString())
							: FString::Printf(TEXT("cell %s is a VerticalAccess intermediate with horizontal traversal exposure=%d; route intermediates require at least 2."), *NeighborCell.ToString(), HorizontalExposure));
					}
					else if (NeighborIntent != nullptr && *NeighborIntent == ELayoutCellIntent::Boundary)
					{
						const bool bReservedBoundary =
							LayoutTraversalPlannerPrivate::CellHasReservation(Context, NeighborCell, ELayoutCellReservationKind::RequiredRoute)
							|| LayoutTraversalPlannerPrivate::CellHasReservation(Context, NeighborCell, ELayoutCellReservationKind::FutureCompoundConnection)
							|| LayoutTraversalPlannerPrivate::CellHasReservation(Context, NeighborCell, ELayoutCellReservationKind::RouteJunction)
							|| LayoutTraversalPlannerPrivate::CellHasReservation(Context, NeighborCell, ELayoutCellReservationKind::VerticalContinuation);
						const bool bSteppedDeckCell = Context.SteppedTraversalDeckCells.Contains(NeighborCell);
						RecordFirstFailure(!bReservedBoundary
							&& !ShouldAllowBoundaryCellsAsIntermediateRouteSteps(Context, NeighborCell)
							&& HorizontalExposure < 2
							? FString::Printf(
								TEXT("cell %s is an unreserved Boundary intermediate without level-fill/stepped-deck authority or a domain candidate exposing two horizontal traversal faces (exposure=%d, steppedDeck=%s)."),
								*NeighborCell.ToString(),
								HorizontalExposure,
								bSteppedDeckCell ? TEXT("true") : TEXT("false"))
							: FString::Printf(TEXT("cell %s is a Boundary intermediate with horizontal traversal exposure=%d; route intermediates require at least 2 (steppedDeck=%s)."),
								*NeighborCell.ToString(),
								HorizontalExposure,
								bSteppedDeckCell ? TEXT("true") : TEXT("false")));
					}
					else
					{
						RecordFirstFailure(FString::Printf(TEXT("cell %s intent=%s has horizontal traversal exposure=%d; route intermediates require at least 2."),
							*NeighborCell.ToString(),
							NeighborIntent != nullptr ? ToDebugString(*NeighborIntent) : TEXT("<none>"),
							HorizontalExposure));
					}
					continue;
				}

				const int32 NewCost = CurrentCost + StepCost;
				const int32* ExistingCost = CostByState.Find(NeighborState);
				const FTraversalRouteSearchState* ExistingPreviousState =
					CameFrom.Find(NeighborState);
				if (ExistingCost == nullptr
					|| NewCost < *ExistingCost
					|| (NewCost == *ExistingCost
						&& ExistingPreviousState != nullptr
						&& IsPreferredState(CurrentState, *ExistingPreviousState)))
				{
					CostByState.Add(NeighborState, NewCost);
					CameFrom.Add(NeighborState, CurrentState);
					OpenSet.Add(NeighborState);
				}
			}
		}

		if (!bAllowDomainProvenBoundaryFallback)
		{
			return FindTraversalRoutePath(
				Context,
				StartCell,
				GoalCell,
				StartTraversalTag,
				GoalTraversalTag,
				OutPath,
				OutFailureReason,
				true,
				StartRouteFaceMask,
				GoalRouteFaceMask,
				ExcludedCells,
				SparseWorkContext,
				bHasExactEndpointProofs);
		}
		return false;
	}

	bool IsFlexibleVerticalRouteEndpoint(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell)
	{
		const ELayoutCellIntent* Intent = Context.PlannedCellIntents.Find(Cell);
		const ELayoutCellIntent* LowerIntent = Context.PlannedCellIntents.Find(Cell - FIntVector(0, 0, 1));
		return (Intent != nullptr && *Intent == ELayoutCellIntent::VerticalAccess)
			|| (LowerIntent != nullptr && *LowerIntent == ELayoutCellIntent::VerticalAccess);
	}

	void MarkRequiredTraversalRoutePath(
		LayoutProfileSolverInternal::FSolveContext& Context,
		const TArray<FIntVector>& Path,
		const FGameplayTag StartTraversalTag,
		const FGameplayTag TargetTraversalTag,
		const bool bRequireExactVerticalEndpointFaces = false)
	{
		for (int32 Index = 0; Index + 1 < Path.Num(); ++Index)
		{
			ELayoutFaceDirection Direction = ELayoutFaceDirection::PosX;
			if (!TryGetHorizontalDirectionBetweenCells(Path[Index], Path[Index + 1], Direction))
			{
				continue;
			}

			const bool bUsesStartTag = StartTraversalTag == TargetTraversalTag
				|| Index < FMath::Max(1, (Path.Num() - 1) / 2);
			const FGameplayTag TraversalTag = bUsesStartTag ? StartTraversalTag : TargetTraversalTag;
			const bool bFromIsFlexibleVerticalEndpoint = IsFlexibleVerticalRouteEndpoint(Context, Path[Index]);
			const bool bToIsFlexibleVerticalEndpoint = IsFlexibleVerticalRouteEndpoint(Context, Path[Index + 1]);
			if (!bRequireExactVerticalEndpointFaces
				&& (bFromIsFlexibleVerticalEndpoint || bToIsFlexibleVerticalEndpoint))
			{
				AddCompiledReservation(
					Context,
					Path[Index],
					bFromIsFlexibleVerticalEndpoint
						? ELayoutCellReservationKind::VerticalContinuation
						: ELayoutCellReservationKind::RequiredRoute);
				AddCompiledReservation(
					Context,
					Path[Index + 1],
					bToIsFlexibleVerticalEndpoint
						? ELayoutCellReservationKind::VerticalContinuation
						: ELayoutCellReservationKind::RequiredRoute);
				continue;
			}
			AddRequiredTraversalRouteFace(Context, Path[Index], Direction, TraversalTag);
			AddRequiredTraversalRouteFace(Context, Path[Index + 1], FLayoutDirectionUtils::GetOpposite(Direction), TraversalTag);
			AddCompiledReservation(Context, Path[Index], ELayoutCellReservationKind::RequiredRoute);
			AddCompiledReservation(Context, Path[Index + 1], ELayoutCellReservationKind::RequiredRoute);
			if (StartTraversalTag != TargetTraversalTag && Index == FMath::Max(0, (Path.Num() - 1) / 2))
			{
				AddCompiledReservation(Context, Path[Index], ELayoutCellReservationKind::RouteJunction);
				AddCompiledReservation(Context, Path[Index + 1], ELayoutCellReservationKind::RouteJunction);
			}
		}
	}

	struct FTraversalRouteAnchor
	{
		FIntVector Cell = FIntVector::ZeroValue;
		FGameplayTag TraversalTag;
	};

	/** Returns whether one exact initial domain can expose horizontal traversal. */
	bool DoesInitialDomainExposeHorizontalTraversal(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell)
	{
		const TArray<LayoutProfileSolverInternal::FSolveCandidate>* Domain =
			Context.InitialDomains.Find(Cell);
		if (Domain == nullptr)
		{
			return false;
		}
		for (const LayoutProfileSolverInternal::FSolveCandidate& Candidate : *Domain)
		{
			if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate)
				|| !Context.Variants.IsValidIndex(Candidate.VariantIndex))
			{
				continue;
			}
			const LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant =
				Context.Variants[Candidate.VariantIndex];
			for (const ELayoutFaceDirection Direction : {
				ELayoutFaceDirection::PosX,
				ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY,
				ELayoutFaceDirection::NegY})
			{
				const FLayoutFaceRule* FaceRule = Variant.WorldFaceRules.FindRule(Direction);
				if (FaceRule != nullptr
					&& !FaceRule->ConnectedTraversalChannels.IsEmpty())
				{
					return true;
				}
			}
		}
		return false;
	}

	/** Returns whether exact initial domains admit one reciprocal horizontal traversal edge. */
	bool CanInitialDomainsShareHorizontalTraversal(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const FIntVector& Neighbor,
		const ELayoutFaceDirection Direction)
	{
		const TArray<LayoutProfileSolverInternal::FSolveCandidate>* CellDomain =
			Context.InitialDomains.Find(Cell);
		const TArray<LayoutProfileSolverInternal::FSolveCandidate>* NeighborDomain =
			Context.InitialDomains.Find(Neighbor);
		if (CellDomain == nullptr || NeighborDomain == nullptr)
		{
			return false;
		}

		for (const LayoutProfileSolverInternal::FSolveCandidate& Candidate : *CellDomain)
		{
			if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
			{
				continue;
			}
			FLayoutFaceRule FaceRule;
			if (!TryGetSolveCandidateFaceRule(Context, Candidate, Direction, FaceRule)
				|| FaceRule.ConnectedTraversalChannels.IsEmpty()
				|| !IsFaceCompatibleWithOccupancy(FaceRule, true, true))
			{
				continue;
			}

			for (const LayoutProfileSolverInternal::FSolveCandidate& NeighborCandidate : *NeighborDomain)
			{
				if (!LayoutProfileSolverInternal::IsOccupiedCandidate(NeighborCandidate))
				{
					continue;
				}
				FLayoutFaceRule NeighborFaceRule;
				if (!TryGetSolveCandidateFaceRule(
						Context,
						NeighborCandidate,
						FLayoutDirectionUtils::GetOpposite(Direction),
						NeighborFaceRule)
					|| !IsFaceCompatibleWithOccupancy(NeighborFaceRule, true, true)
					|| !FaceRule.ConnectedTraversalChannels.HasAnyExact(
						NeighborFaceRule.ConnectedTraversalChannels)
					|| !FaceRule.GetEffectiveConnectionTags().HasAnyExact(
						NeighborFaceRule.GetEffectiveAllowedConnectionTags())
					|| !NeighborFaceRule.GetEffectiveConnectionTags().HasAnyExact(
						FaceRule.GetEffectiveAllowedConnectionTags()))
				{
					continue;
				}
				return true;
			}
		}
		return false;
	}

	/** Selects one stable representative for each unanchored non-sparse traversal component. */
	TArray<FIntVector> BuildTraversalComponentRepresentatives(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const TArray<FIntVector>& ExistingAnchorCells)
	{
		TSet<FIntVector> EligibleCells;
		for (const TPair<FIntVector, TArray<LayoutProfileSolverInternal::FSolveCandidate>>& Pair :
			Context.InitialDomains)
		{
			if (!Context.TerrainResidualRuleIdByCell.Contains(Pair.Key)
				&& DoesInitialDomainExposeHorizontalTraversal(Context, Pair.Key))
			{
				EligibleCells.Add(Pair.Key);
			}
		}

		TArray<FIntVector> SortedCells = EligibleCells.Array();
		SortedCells.Sort([](const FIntVector& Left, const FIntVector& Right)
		{
			if (Left.Z != Right.Z) return Left.Z < Right.Z;
			if (Left.Y != Right.Y) return Left.Y < Right.Y;
			return Left.X < Right.X;
		});
		TSet<FIntVector> Visited;
		TArray<FIntVector> Representatives;
		for (const FIntVector& RootCell : SortedCells)
		{
			if (Visited.Contains(RootCell))
			{
				continue;
			}
			TArray<FIntVector> Component;
			TArray<FIntVector> Pending = {RootCell};
			Visited.Add(RootCell);
			while (!Pending.IsEmpty())
			{
				const FIntVector Cell = Pending.Pop(EAllowShrinking::No);
				Component.Add(Cell);
				for (const ELayoutFaceDirection Direction : {
					ELayoutFaceDirection::PosX,
					ELayoutFaceDirection::NegX,
					ELayoutFaceDirection::PosY,
					ELayoutFaceDirection::NegY})
				{
					const FIntVector Neighbor =
						Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
					if (EligibleCells.Contains(Neighbor)
						&& !Visited.Contains(Neighbor)
						&& CanInitialDomainsShareHorizontalTraversal(
							Context,
							Cell,
							Neighbor,
							Direction))
					{
						Visited.Add(Neighbor);
						Pending.Add(Neighbor);
					}
				}
			}
			if (Component.ContainsByPredicate(
				[&ExistingAnchorCells](const FIntVector& Cell)
				{
					return ExistingAnchorCells.Contains(Cell);
				}))
			{
				continue;
			}

			Component.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				if (Left.Z != Right.Z) return Left.Z < Right.Z;
				if (Left.Y != Right.Y) return Left.Y < Right.Y;
				return Left.X < Right.X;
			});
			const bool bHasSameLevelAnchor = ExistingAnchorCells.ContainsByPredicate(
				[&RootCell](const FIntVector& AnchorCell)
				{
					return AnchorCell.Z == RootCell.Z;
				});
			const TSet<FIntVector> ComponentCells(Component);
			const auto CanAttachOutsideComponent = [&](const FIntVector& Cell)
			{
				for (const ELayoutFaceDirection Direction : {
					ELayoutFaceDirection::PosX,
					ELayoutFaceDirection::NegX,
					ELayoutFaceDirection::PosY,
					ELayoutFaceDirection::NegY})
				{
					const FIntVector Neighbor =
						Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
					if (!ComponentCells.Contains(Neighbor)
						&& Context.PlannedCellIntents.Contains(Neighbor)
						&& CanInitialDomainsShareHorizontalTraversal(
							Context,
							Cell,
							Neighbor,
							Direction))
					{
						return true;
					}
				}
				return false;
			};
			const bool bHasExternalAttachmentCell =
				Component.ContainsByPredicate(CanAttachOutsideComponent);
			const FIntVector* BestCell = nullptr;
			int32 BestDistance = MAX_int32;
			for (const FIntVector& Cell : Component)
			{
				if (bHasExternalAttachmentCell && !CanAttachOutsideComponent(Cell))
				{
					continue;
				}
				int32 NearestDistance = ExistingAnchorCells.IsEmpty() ? 0 : MAX_int32;
				for (const FIntVector& AnchorCell : ExistingAnchorCells)
				{
					if (bHasSameLevelAnchor && AnchorCell.Z != Cell.Z)
					{
						continue;
					}
					NearestDistance = FMath::Min(
						NearestDistance,
						FMath::Abs(Cell.X - AnchorCell.X)
							+ FMath::Abs(Cell.Y - AnchorCell.Y)
							+ FMath::Abs(Cell.Z - AnchorCell.Z));
				}
				if (BestCell == nullptr || NearestDistance < BestDistance)
				{
					BestCell = &Cell;
					BestDistance = NearestDistance;
				}
			}
			if (BestCell != nullptr)
			{
				Representatives.Add(*BestCell);
			}
		}
		return Representatives;
	}
}

bool LayoutProfileSolverInternal::ConsumeSparseStructuralWork(FSolveContext& Context)
{
	return LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(Context);
}

bool LayoutProfileSolverInternal::BuildSparseStructuralTraversalPorts(
	FSolveContext& PreparedContext,
	const TArray<FLayoutVerticalAccessHostGroup>& SelectedHostGroups,
	const TArray<int32>& SelectedOptionIndices,
	TArray<FSparseStructuralTraversalPort>& OutPorts,
	FString& OutFailureReason,
	const TArray<FIntVector>& NormalInterfaceCells)
{
	OutPorts.Reset();
	TArray<FSparseStructuralTraversalPort> PendingPorts;
	OutFailureReason.Reset();
	if (SelectedHostGroups.Num() != SelectedOptionIndices.Num())
	{
		OutFailureReason = TEXT("Sparse structural host groups and selected option indices have different counts.");
		return false;
	}

	for (const auto& Cell : NormalInterfaceCells)
	{
		if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(PreparedContext))
		{
			OutFailureReason = PreparedContext.Result.FailureReason;
			return false;
		}
		const FLayoutPlannedCell* OwningCell = PreparedContext.OwningTopologyCellsByPhysicalCell.Find(Cell);
		// Exact landing claims may consume authored ground, never upper residual air.
		const bool bClaimedGround = OwningCell != nullptr && !OwningCell->bIsBridgeCell
			&& GetFinalizedCellModuleLevel(PreparedContext, Cell) == 0;
		if (!PreparedContext.PlannedCellIntents.Contains(Cell)
			|| (PreparedContext.OwningTerrainResidualRuleIdByCell.Contains(Cell) && !bClaimedGround)
			|| PreparedContext.TerrainResidualRuleIdByCell.Contains(Cell)
			|| PreparedContext.ChildReservationCells.Contains(Cell))
		{
			OutFailureReason = TEXT("Selected normal interface lacks active normal-zone authority.");
			return false;
		}
	}
	TMap<FWalkableNodeKey, TSet<FWalkableNodeKey>> InternalGraph;
	auto ResolveCandidatePort = [&PreparedContext, &InternalGraph](
		const FIntVector& RootCell,
		const FLayoutId ModuleSnapshotId,
		const int32 YawRotationSteps,
		const FIntVector& LocalBundleCell,
		const uint8 FaceMask,
		TMap<FGameplayTag, uint8>& OutChannelFaces) -> bool
	{
		if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(PreparedContext)) return false;
		const TArray<FSolveCandidate>* Domain = PreparedContext.InitialDomains.Find(RootCell);
		const FSolveCandidate* Candidate = Domain != nullptr
			? Domain->FindByPredicate([ModuleSnapshotId, YawRotationSteps](const FSolveCandidate& DomainCandidate)
			{
				return DomainCandidate.ModuleSnapshotId == ModuleSnapshotId
					&& DomainCandidate.YawRotationSteps == YawRotationSteps;
			})
			: nullptr;
		if (Candidate == nullptr)
		{
			return false;
		}
		const FSolveContext::FOrientedModuleVariant* Variant = FindSolveCandidateVariant(PreparedContext, *Candidate);
		if (Variant == nullptr || !PreparedContext.ModuleSnapshots.IsValidIndex(Variant->ModuleSnapshotIndex)) return false;
		const FLayoutModuleSolveSnapshot& Snapshot = PreparedContext.ModuleSnapshots[Variant->ModuleSnapshotIndex];
		auto WorldCell = [&](const FIntVector& LocalCell)
		{
			return LayoutPlacementOccupancy::ProjectLocalCellToWorld(RootCell, LocalCell, Snapshot.BoundsCells, YawRotationSteps);
		};
		for (const FLayoutDerivedInternalTraversalLink& Link : GetSolveCandidateDerivedInternalTraversalLinks(PreparedContext, *Candidate))
		{
			if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(PreparedContext)) return false;
			const FWalkableNodeKey From{WorldCell(Link.FromLocalCell), Link.FromTraversalChannel};
			const FWalkableNodeKey To{WorldCell(Link.ToLocalCell), Link.ToTraversalChannel};
			InternalGraph.FindOrAdd(From).Add(To);
			if (Link.bBidirectional) InternalGraph.FindOrAdd(To).Add(From);
		}
		for (const FLayoutInternalAccessLink& Link : GetSolveCandidateInternalAccessLinks(PreparedContext, *Candidate))
		{
			const FWalkableNodeKey From{WorldCell(LocalBundleCell), Link.FromTraversalChannel};
			const FWalkableNodeKey To{WorldCell(LocalBundleCell), Link.ToTraversalChannel};
			InternalGraph.FindOrAdd(From).Add(To);
			if (Link.bBidirectional) InternalGraph.FindOrAdd(To).Add(From);
		}

		for (const ELayoutFaceDirection Direction : {
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY})
		{
			if (!LayoutFaceMaskContainsDirection(FaceMask, Direction))
			{
				continue;
			}
			if (Snapshot.OccupiedLocalCells.Num() > 1)
			{
				const FLayoutLocalCellFaceRuleSnapshot* Local = Snapshot.GeneratedLocalCellFaceRules.FindByPredicate(
					[&LocalBundleCell](const FLayoutLocalCellFaceRuleSnapshot& Cell) { return Cell.LocalCell == LocalBundleCell; });
				if (Local == nullptr || !Local->ExposedFaceRules.ContainsByPredicate(
					[Candidate, Direction](const FLayoutFaceRule& Face)
					{ return FLayoutDirectionUtils::RotateYaw(Face.Direction, Candidate->YawRotationSteps) == Direction; }))
				{
					continue; // Internal composite faces cannot borrow the root's external face.
				}
			}
			FLayoutFaceRule FaceRule;
			if (TryGetSolveCandidateLocalFaceRule(
					PreparedContext, *Candidate, LocalBundleCell, Direction, FaceRule)
				&& LayoutTraversalPlannerPrivate::IsFaceCompatibleWithOccupancy(FaceRule, true, true))
			{
				for (const FGameplayTag& Channel : FaceRule.ConnectedTraversalChannels)
				{
					OutChannelFaces.FindOrAdd(Channel) |= LayoutFaceDirectionMask(Direction);
				}
			}
		}
		return true;
	};

	auto AppendChannelPorts = [&PendingPorts](
		const FSparseStructuralTraversalPort& BasePort,
		const TMap<FGameplayTag, uint8>& ChannelFaces)
	{
		if (ChannelFaces.IsEmpty())
		{
			FSparseStructuralTraversalPort& Claim = PendingPorts.Add_GetRef(BasePort);
			Claim.bStructuralOnly = true;
			return;
		}
		TArray<FGameplayTag> Channels;
		ChannelFaces.GetKeys(Channels);
		Channels.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
		{
			return Left.ToString() < Right.ToString();
		});
		for (const FGameplayTag& Channel : Channels)
		{
			FSparseStructuralTraversalPort& Port = PendingPorts.Add_GetRef(BasePort);
			Port.TraversalChannel = Channel;
			Port.RouteFaceMask = ChannelFaces.FindChecked(Channel);
		}
	};

	for (const TPair<FIntVector, ELayoutCellIntent>& CellIntentPair : PreparedContext.PlannedCellIntents)
	{
		if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(PreparedContext))
		{
			OutFailureReason = PreparedContext.Result.FailureReason;
			return false;
		}
		if (CellIntentPair.Value != ELayoutCellIntent::Entry && !NormalInterfaceCells.Contains(CellIntentPair.Key))
		{
			continue;
		}
		const FLayoutCellCandidateDomainRestriction* Restriction =
			PreparedContext.CandidateDomainRestrictionsByCell.Find(CellIntentPair.Key);
		if (Restriction == nullptr || Restriction->AllowedCandidates.Num() != 1)
		{
			OutFailureReason = FString::Printf(
				TEXT("Sparse structural gate %s has no singleton selected candidate proof."),
				*CellIntentPair.Key.ToString());
			return false;
		}
		uint8 RouteFaceMask = 0;
		for (const ELayoutFaceDirection Direction : {
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY})
		{
			if (PreparedContext.PlannedCellIntents.Contains(
				CellIntentPair.Key + FLayoutDirectionUtils::ToCellDelta(Direction)))
			{
				RouteFaceMask |= LayoutFaceDirectionMask(Direction);
			}
		}
		TMap<FGameplayTag, uint8> ChannelFaces;
		const FLayoutCandidateVariantIdentity& GateCandidate = Restriction->AllowedCandidates[0];
		if (!ResolveCandidatePort(
				CellIntentPair.Key,
				GateCandidate.ModuleSnapshotId,
				GateCandidate.YawRotationSteps,
				FIntVector::ZeroValue,
				RouteFaceMask,
				ChannelFaces)
			|| (ChannelFaces.IsEmpty() && PreparedContext.ProfileSnapshot.bRequireAllTraversalChannelsReachable))
		{
			OutFailureReason = FString::Printf(
				TEXT("Sparse structural gate proof %s has no exact traversable face into local routing space."),
				*Restriction->RestrictionId.ToString());
			return false;
		}
		FSparseStructuralTraversalPort Port;
		Port.ProofId = Restriction->RestrictionId;
		Port.Cell = CellIntentPair.Key;
		const FSolveCandidate* ExactGate = PreparedContext.InitialDomains.FindChecked(Port.Cell).FindByPredicate(
			[&](const FSolveCandidate& Candidate) { return Candidate.ModuleSnapshotId == GateCandidate.ModuleSnapshotId
				&& Candidate.YawRotationSteps == GateCandidate.YawRotationSteps; });
		const FSolveContext::FOrientedModuleVariant* GateVariant = FindSolveCandidateVariant(PreparedContext, *ExactGate);
		const FLayoutModuleSolveSnapshot& GateSnapshot = PreparedContext.ModuleSnapshots[GateVariant->ModuleSnapshotIndex];
		Port.ClaimedCells.Add(Port.Cell);
		for (const FIntVector& LocalCell : GateSnapshot.OccupiedLocalCells)
		{
			Port.ClaimedCells.AddUnique(LayoutPlacementOccupancy::ProjectLocalCellToWorld(
				Port.Cell, LocalCell, GateSnapshot.BoundsCells, ExactGate->YawRotationSteps));
		}
		TArray<FLayoutFaceRule> GateFaces;
		TArray<ELayoutFaceDirection> ExteriorDirections;
		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
			FLayoutFaceRule Face;
			if (TryGetSolveCandidateLocalFaceRule(PreparedContext, *ExactGate, FIntVector::ZeroValue, Direction, Face)) GateFaces.Add(Face);
			if (!CellFaceHasAnyPlannedNeighborCarrier(PreparedContext, Port.Cell, Direction)) ExteriorDirections.Add(Direction);
		}
		const int32 FirstGatePort = PendingPorts.Num();
		AppendChannelPorts(Port, ChannelFaces);
		for (int32 Index = FirstGatePort; Index < PendingPorts.Num(); ++Index)
		{
			PendingPorts[Index].bIsEntryRoot = CellIntentPair.Value == ELayoutCellIntent::Entry
				&& LayoutEntryRootUtilities::CanReachWalkableAreaFromEntryRoot(
				GateFaces, GetSolveCandidateInternalAccessLinks(PreparedContext, *ExactGate), ExteriorDirections,
				PendingPorts[Index].TraversalChannel);
		}
	}

	for (const FLayoutCommittedTraversalAnchor& Anchor : PreparedContext.CommittedTraversalAnchors)
	{
		if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(PreparedContext))
		{
			OutFailureReason = PreparedContext.Result.FailureReason;
			return false;
		}
		const auto* Restriction = PreparedContext.CandidateDomainRestrictionsByCell.Find(Anchor.Cell);
		if (!Anchor.TraversalChannel.IsValid() || !PreparedContext.PlannedCellIntents.Contains(Anchor.Cell))
		{
			OutFailureReason = TEXT("Sparse traversal commitment requires a planned cell and valid channel.");
			return false;
		}
		// A later child import or existing endpoint can supply this contact. Assignment
		// binds every request-owned anchor after all offers exist, and rejects missing ones.
		if (Restriction == nullptr || Restriction->AllowedCandidates.Num() != 1) continue;
		const auto& Identity = Restriction->AllowedCandidates[0];
		TMap<FGameplayTag, uint8> ChannelFaces;
		if (!ResolveCandidatePort(Anchor.Cell, Identity.ModuleSnapshotId, Identity.YawRotationSteps,
			FIntVector::ZeroValue, 0x0f, ChannelFaces))
		{
			OutFailureReason = TEXT("Sparse traversal commitment has no selected candidate in its domain.");
			return false;
		}
		const auto& Domain = PreparedContext.InitialDomains.FindChecked(Anchor.Cell);
		const auto* Candidate = Domain.FindByPredicate([&](const auto& Value)
			{ return Value.ModuleSnapshotId == Identity.ModuleSnapshotId && Value.YawRotationSteps == Identity.YawRotationSteps; });
		if (!GetSolveCandidateTraversalChannels(PreparedContext, *Candidate).HasTagExact(Anchor.TraversalChannel))
		{
			OutFailureReason = TEXT("Selected sparse interface lacks its committed traversal channel.");
			return false;
		}
		FSparseStructuralTraversalPort& Port = PendingPorts.AddDefaulted_GetRef();
		Port.ProofId = Restriction->RestrictionId;
		Port.Cell = Anchor.Cell;
		Port.ClaimedCells.Add(Anchor.Cell);
		Port.TraversalChannel = Anchor.TraversalChannel;
		Port.RouteFaceMask = ChannelFaces.FindRef(Anchor.TraversalChannel);
		Port.TraversalCommitment = Anchor;
	}

	for (const FLayoutCommittedEndpointAnchor& Commitment : PreparedContext.CommittedEndpointAnchors)
	{
		if (!PreparedContext.PlannedCellIntents.Contains(Commitment.LocalCell)
			|| Commitment.CommitmentId.IsNone()
			|| Commitment.TraversalChannels.IsEmpty())
		{
			continue;
		}
		TMap<FGameplayTag, uint8> ChannelFaces;
		for (const FGameplayTag& Channel : Commitment.TraversalChannels)
		{
			ChannelFaces.Add(Channel, LayoutFaceDirectionMask(Commitment.FaceDirection));
		}
		FSparseStructuralTraversalPort Port;
		Port.ProofId = Commitment.CommitmentId;
		Port.Cell = Commitment.LocalCell;
		Port.EndpointCommitment = Commitment;
		AppendChannelPorts(Port, ChannelFaces);
	}

	for (int32 GroupIndex = 0; GroupIndex < SelectedHostGroups.Num(); ++GroupIndex)
	{
		const FLayoutVerticalAccessHostGroup& Group = SelectedHostGroups[GroupIndex];
		if (!Group.Options.IsValidIndex(SelectedOptionIndices[GroupIndex]))
		{
			OutFailureReason = FString::Printf(
				TEXT("Sparse structural host group %s selected invalid option index %d."),
				*Group.GroupId.ToString(),
				SelectedOptionIndices[GroupIndex]);
			return false;
		}
		const FLayoutVerticalAccessHostOption& Option = Group.Options[SelectedOptionIndices[GroupIndex]];
		if (!Option.bHasExactCandidateWitness)
		{
			OutFailureReason = FString::Printf(
				TEXT("Sparse structural host group %s selected an option without exact candidate evidence."),
				*Group.GroupId.ToString());
			return false;
		}
		TMap<FGameplayTag, uint8> LowerChannelFaces;
		if (!ResolveCandidatePort(
				Option.LowerCell,
				Option.LowerModuleSnapshotId,
				Option.LowerYawRotationSteps,
				FIntVector::ZeroValue,
				Option.LowerTraversalPortFaceMask,
				LowerChannelFaces)
			|| (LowerChannelFaces.IsEmpty() && PreparedContext.ProfileSnapshot.bRequireAllTraversalChannelsReachable))
		{
			TArray<FString> DomainDetails;
			if (const TArray<FSolveCandidate>* Domain = PreparedContext.InitialDomains.Find(Option.LowerCell))
			{
				for (const FSolveCandidate& Candidate : *Domain)
				{
					DomainDetails.Add(FString::Printf(
						TEXT("%s/yaw%d"),
						*Candidate.ModuleSnapshotId.ToString(),
						Candidate.YawRotationSteps));
				}
			}
			DomainDetails.Append(PreparedContext.InitialDomainAdmissionFailuresByCell.FindRef(Option.LowerCell));
			OutFailureReason = FString::Printf(
				TEXT("Selected host %s has no exact lower traversal port at %s for %s/yaw%d mask=%u. Domain=[%s]"),
				*Group.GroupId.ToString(),
				*Option.LowerCell.ToString(),
				*Option.LowerModuleSnapshotId.ToString(),
				Option.LowerYawRotationSteps,
				Option.LowerTraversalPortFaceMask,
				*FString::Join(DomainDetails, TEXT(", ")));
			return false;
		}
		FSparseStructuralTraversalPort LowerPort;
		LowerPort.ProofId = Group.GroupId;
		LowerPort.Cell = Option.LowerCell;
		LowerPort.ClaimedCells = Option.OccupiedCells;
		LowerPort.RequiredSupportCells = Option.RequiredFilledSupportCells;
		LowerPort.RequiredClearanceCells = Option.RequiredEmptyClearanceCells;
		AppendChannelPorts(LowerPort, LowerChannelFaces);

		const bool bCompositeLanding = Option.UpperCandidateLocalCell != FIntVector::ZeroValue;
		const FIntVector UpperRootCell = bCompositeLanding ? Option.LowerCell : Option.UpperCell;
		const FLayoutId UpperModuleSnapshotId = bCompositeLanding ? Option.LowerModuleSnapshotId : Option.UpperModuleSnapshotId;
		const int32 UpperYawRotationSteps = bCompositeLanding ? Option.LowerYawRotationSteps : Option.UpperYawRotationSteps;
		TMap<FGameplayTag, uint8> UpperChannelFaces;
		if (!ResolveCandidatePort(
				UpperRootCell,
				UpperModuleSnapshotId,
				UpperYawRotationSteps,
				Option.UpperCandidateLocalCell,
				Option.UpperTraversalPortFaceMask,
				UpperChannelFaces)
			|| (UpperChannelFaces.IsEmpty() && PreparedContext.ProfileSnapshot.bRequireAllTraversalChannelsReachable))
		{
			OutFailureReason = FString::Printf(TEXT("Selected host %s has no exact upper traversal port."), *Group.GroupId.ToString());
			return false;
		}
		FSparseStructuralTraversalPort UpperPort;
		UpperPort.ProofId = Group.GroupId;
		UpperPort.Cell = Option.UpperCell;
		UpperPort.ClaimedCells = Option.OccupiedCells;
		AppendChannelPorts(UpperPort, UpperChannelFaces);
	}

	for (FSparseStructuralTraversalPort& Port : PendingPorts)
	{
		if (Port.bStructuralOnly) continue;
		TArray<FWalkableNodeKey> Frontier{{Port.Cell, Port.TraversalChannel}};
		while (!Frontier.IsEmpty())
		{
			if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(PreparedContext))
			{
				OutFailureReason = PreparedContext.Result.FailureReason;
				return false;
			}
			const FWalkableNodeKey Node = Frontier.Pop(EAllowShrinking::No);
			if (Port.InternallyReachableNodes.Contains(Node)) continue;
			Port.InternallyReachableNodes.Add(Node);
			if (const TSet<FWalkableNodeKey>* Neighbors = InternalGraph.Find(Node)) Frontier.Append(Neighbors->Array());
		}
	}
	OutPorts = MoveTemp(PendingPorts);
	return true;
}

bool LayoutProfileSolverInternal::TrySolveSparseStructuralPortAssignment(
	FSolveContext& PreparedBaseContext,
	const TArray<FSparseStructuralTraversalPort>& SelectedPorts,
	FSolveContext& OutSolvedContext,
	FSparseStructuralRouteAssignment& OutAssignment,
	FString& OutFailureReason)
{
	OutSolvedContext = FSolveContext{};
	OutAssignment = FSparseStructuralRouteAssignment{};
	OutFailureReason.Reset();
	if (SelectedPorts.IsEmpty())
	{
		OutFailureReason = TEXT("Sparse structural route assignment requires a selected structural claim.");
		return false;
	}
	const bool bRequireConnections = PreparedBaseContext.ProfileSnapshot.bRequireAllTraversalChannelsReachable;

	TArray<FSparseStructuralTraversalPort> SortedPorts = SelectedPorts;
	for (const auto& Anchor : PreparedBaseContext.CommittedTraversalAnchors)
	{
		bool bMatched = false;
		for (auto& Port : SortedPorts)
		{
			if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(PreparedBaseContext))
			{
				OutFailureReason = PreparedBaseContext.Result.FailureReason;
				return false;
			}
			if (Port.Cell == Anchor.Cell && Port.TraversalChannel == Anchor.TraversalChannel && !Port.bStructuralOnly)
			{
				if (!Port.TraversalCommitment.IsSet()) Port.TraversalCommitment = Anchor;
				bMatched = true;
			}
		}
		if (!bMatched)
		{
			OutFailureReason = FString::Printf(TEXT("Committed traversal anchor %s has no selected interface offer."), *Anchor.Cell.ToString());
			return false;
		}
	}
	SortedPorts.Sort([](const FSparseStructuralTraversalPort& Left, const FSparseStructuralTraversalPort& Right)
	{
		if (Left.Cell.Z != Right.Cell.Z) return Left.Cell.Z < Right.Cell.Z;
		if (Left.Cell.Y != Right.Cell.Y) return Left.Cell.Y < Right.Cell.Y;
		if (Left.Cell.X != Right.Cell.X) return Left.Cell.X < Right.Cell.X;
		if (Left.ProofId != Right.ProofId) return Left.ProofId.LexicalLess(Right.ProofId);
		return Left.TraversalChannel.ToString() < Right.TraversalChannel.ToString();
	});
	TSet<FIntVector> PortCells;
	TSet<FIntVector> SupportCells;
	TSet<FIntVector> ClearanceCells;
	TMap<FIntVector, FLayoutId> SelectedProofOwnerByCell;
	for (const FSparseStructuralTraversalPort& Port : SortedPorts)
	{
		if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(PreparedBaseContext))
		{
			OutFailureReason = PreparedBaseContext.Result.FailureReason;
			return false;
		}
		const bool bValidOffer = Port.bStructuralOnly
			? !bRequireConnections && !Port.TraversalChannel.IsValid() && Port.RouteFaceMask == 0
				&& !Port.EndpointCommitment.IsSet() && !Port.TraversalCommitment.IsSet()
				&& !Port.ChildProof.IsValid() && !Port.ClaimedCells.IsEmpty()
			: Port.TraversalChannel.IsValid() && (Port.RouteFaceMask != 0 || Port.ChildProof.IsValid() || Port.TraversalCommitment.IsSet());
		if (Port.ProofId.IsNone()
			|| !bValidOffer
			|| !PreparedBaseContext.PlannedCellIntents.Contains(Port.Cell))
		{
			OutFailureReason = FString::Printf(
				TEXT("Sparse structural port %s at %s lacks exact proof, channel, route face, or planned-cell authority."),
				*Port.ProofId.ToString(),
				*Port.Cell.ToString());
			return false;
		}
		if (Port.TraversalCommitment.IsSet()
			&& (Port.Cell != Port.TraversalCommitment->Cell || Port.TraversalChannel != Port.TraversalCommitment->TraversalChannel
				|| !PreparedBaseContext.CommittedTraversalAnchors.ContainsByPredicate([&](const auto& Anchor)
					{ return Anchor.Cell == Port.Cell && Anchor.TraversalChannel == Port.TraversalChannel; })))
		{
			OutFailureReason = TEXT("Sparse traversal port does not match its request-owned commitment.");
			return false;
		}
		TArray<FIntVector> ProofCells = Port.ClaimedCells;
		// Endpoint contacts constrain occupancy but do not independently own a provider.
		// A selected gate/stair may satisfy them without acquiring conflicting ownership.
		if (!Port.EndpointCommitment.IsSet() && !Port.ChildProof.IsValid()) ProofCells.AddUnique(Port.Cell);
		for (const FIntVector& ProofCell : ProofCells)
		{
			if (const FLayoutId* ExistingOwner = SelectedProofOwnerByCell.Find(ProofCell);
				ExistingOwner != nullptr && *ExistingOwner != Port.ProofId)
			{
				OutFailureReason = FString::Printf(
					TEXT("Sparse structural proofs %s and %s overlap occupied claim %s."),
					*ExistingOwner->ToString(),
					*Port.ProofId.ToString(),
					*ProofCell.ToString());
				return false;
			}
			SelectedProofOwnerByCell.Add(ProofCell, Port.ProofId);
		}
		if (Port.EndpointCommitment.IsSet())
		{
			const FLayoutCommittedEndpointAnchor& Commitment = Port.EndpointCommitment.GetValue();
			const bool bMatchesLocation = Port.ChildProof.IsValid()
				? PreparedBaseContext.SparseStructuralChildProofs.Contains(Port.ChildProof)
					&& Port.Cell == Commitment.LocalCell + Port.ChildProof->RegionCellOffset
						+ FLayoutDirectionUtils::ToCellDelta(Commitment.FaceDirection)
				: Commitment.LocalCell == Port.Cell
					&& LayoutFaceMaskContainsDirection(Port.RouteFaceMask, Commitment.FaceDirection);
			if (Commitment.CommitmentId != Port.ProofId || !bMatchesLocation
				|| !Commitment.TraversalChannels.HasTagExact(Port.TraversalChannel))
			{
				OutFailureReason = FString::Printf(
					TEXT("Sparse structural endpoint proof %s does not match its selected cell, face, or traversal channel."),
					*Port.ProofId.ToString());
				return false;
			}
			OutAssignment.RetainedEndpointCommitments.Add(Commitment);
			if (Port.ChildProof.IsValid()) OutAssignment.RetainedChildProofs.AddUnique(Port.ChildProof);
		}
		PortCells.Add(Port.Cell);
		for (const FIntVector& Cell : Port.ClaimedCells)
		{
			if (!PreparedBaseContext.ChildReservationCells.Contains(Cell)) PortCells.Add(Cell);
		}
		SupportCells.Append(Port.RequiredSupportCells);
		ClearanceCells.Append(Port.RequiredClearanceCells);
		OutAssignment.RetainedProofIds.AddUnique(Port.ProofId);
	}

	for (const FIntVector& Cell : ClearanceCells)
	{
		const FSolveContext::FSolvePlacement* Placement = FindPlacementOrFixedNeighbor(PreparedBaseContext, Cell);
		if (PortCells.Contains(Cell) || SupportCells.Contains(Cell)
			|| (Placement != nullptr && FSolveContext::IsOccupiedPlacement(*Placement)))
		{
			OutFailureReason = FString::Printf(TEXT("Sparse structural clearance conflicts with filled claim at %s."), *Cell.ToString());
			return false;
		}
	}
	for (const FIntVector& Cell : SupportCells)
	{
		if (!PreparedBaseContext.FixedNeighborPlacements.Contains(Cell)) PortCells.Add(Cell);
	}
	TSet<FIntVector> NormalWorkCells;
	for (const auto& Pair : PreparedBaseContext.PlannedCellIntents)
	{
		if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(PreparedBaseContext))
		{
			OutFailureReason = PreparedBaseContext.Result.FailureReason;
			return false;
		}
		if (!PreparedBaseContext.OwningTerrainResidualRuleIdByCell.Contains(Pair.Key)
			&& !PreparedBaseContext.TerrainResidualRuleIdByCell.Contains(Pair.Key)
			&& !PreparedBaseContext.ChildReservationCells.Contains(Pair.Key))
		{
			NormalWorkCells.Add(Pair.Key);
		}
	}
	FSolveContext WorkingBase = PreparedBaseContext;
	ON_SCOPE_EXIT
	{
		PreparedBaseContext.CandidateAttemptCount = WorkingBase.CandidateAttemptCount;
		PreparedBaseContext.bTimeBudgetExceeded |= WorkingBase.bTimeBudgetExceeded;
	};
	TSet<FIntVector> ExcludedRouteCells = ClearanceCells;
	const int32 RemainingCandidateBudget = FMath::Max(
		0,
		WorkingBase.MaxCandidateAttempts - WorkingBase.CandidateAttemptCount);
	const int32 MaxRouteAttempts = FMath::Min(8, RemainingCandidateBudget);
	FString LastFailureReason;
	FString FirstProofFailureReason;
	bool bHasProvedAssignment = false;
	bool bStructureOnly = !bRequireConnections && PreparedBaseContext.CommittedTraversalAnchors.IsEmpty();
	TArray<TArray<FIntVector>> AcceptedPaths;
	TArray<TPair<int32, int32>> AcceptedEdges;
	TArray<TPair<int32, int32>> DeferredOptionalEdges;
	TUniquePtr<LayoutSolveExecution::FOptionalImprovementScope> ImprovementScope;
	for (int32 AttemptIndex = 0; AttemptIndex < MaxRouteAttempts; ++AttemptIndex)
	{
		if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(WorkingBase))
		{
			LastFailureReason = WorkingBase.Result.FailureReason;
			break;
		}
		++OutAssignment.AttemptCount;
		FSolveContext TrialContext = WorkingBase;
		TrialContext.bHasCompleteSparseStructuralRouteAssignment = true;
		BuildRequiredTraversalRoutesForPreparedPlan(TrialContext);
		for (const FSparseStructuralTraversalPort& Port : SortedPorts)
		{
			if (Port.ChildProof.IsValid() && Port.EndpointCommitment.IsSet())
			{
				LayoutTraversalPlannerPrivate::AddRequiredTraversalRouteFace(TrialContext, Port.Cell,
					FLayoutDirectionUtils::GetOpposite(Port.EndpointCommitment->FaceDirection), Port.TraversalChannel);
			}
		}
		TArray<TArray<FIntVector>> AttemptPaths = AcceptedPaths;
		TArray<TPair<int32, int32>> AttemptEdges = AcceptedEdges;
		for (int32 Index = 0; Index < AcceptedPaths.Num(); ++Index)
		{
			const FGameplayTag Channel = SortedPorts[AcceptedEdges[Index].Key].TraversalChannel;
			LayoutTraversalPlannerPrivate::MarkRequiredTraversalRoutePath(TrialContext, AcceptedPaths[Index], Channel, Channel, true);
		}
		bool bCompiledAllRoutes = true;
		TSet<int32> ReachedPorts;
		bool bHasTraversalRoot = false;
		for (int32 Index = 0; Index < SortedPorts.Num(); ++Index)
		{
			if (SortedPorts[Index].bStructuralOnly) ReachedPorts.Add(Index);
			else if (SortedPorts[Index].bIsEntryRoot)
			{
				ReachedPorts.Add(Index);
				bHasTraversalRoot = true;
			}
		}
		if (!bHasTraversalRoot)
		{
			for (int32 Index = 0; Index < SortedPorts.Num(); ++Index)
			{
				if (!SortedPorts[Index].bStructuralOnly) { ReachedPorts.Add(Index); break; }
			}
		}
		// Internal directed links can carry connectivity to another level/channel before any corridor is added.
		auto ExpandInternalReachability = [&]()
		{
			bool bChanged;
			do
			{
				bChanged = false;
				for (int32 From = 0; From < SortedPorts.Num(); ++From)
				{
					if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(TrialContext)) return false;
					if (!ReachedPorts.Contains(From)) continue;
					for (int32 To = 0; To < SortedPorts.Num(); ++To)
					{
						if (!ReachedPorts.Contains(To) && (SortedPorts[From].InternallyReachableNodes.Contains(
							{SortedPorts[To].Cell, SortedPorts[To].TraversalChannel})
							|| AcceptedEdges.ContainsByPredicate([&](const auto& Edge)
								{ return (Edge.Key == From && Edge.Value == To) || (Edge.Key == To && Edge.Value == From); })))
						{
							ReachedPorts.Add(To);
							bChanged = true;
						}
					}
				}
			} while (bChanged);
			return true;
		};
		bCompiledAllRoutes = ExpandInternalReachability();
		while (!bStructureOnly && bCompiledAllRoutes && ReachedPorts.Num() < SortedPorts.Num())
		{
			// A child anchor requires its own route, not every optional stair route.
			// Prove the mandatory baseline before spending allowance on improvements.
			if (!bRequireConnections && !bHasProvedAssignment)
			{
				bool bMissingCommitment = false;
				for (int32 Index = 0; Index < SortedPorts.Num(); ++Index)
				{
					bMissingCommitment |= SortedPorts[Index].TraversalCommitment.IsSet() && !ReachedPorts.Contains(Index);
				}
				if (!bMissingCommitment) break;
			}
			TArray<TPair<int32, int32>> Edges;
			for (int32 From = 0; From < SortedPorts.Num(); ++From)
			{
				if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(TrialContext))
				{
					bCompiledAllRoutes = false;
					break;
				}
				if (!ReachedPorts.Contains(From)) continue;
				for (int32 To = 0; To < SortedPorts.Num(); ++To)
				{
					if (!ReachedPorts.Contains(To) && SortedPorts[From].Cell.Z == SortedPorts[To].Cell.Z
						&& SortedPorts[From].TraversalChannel == SortedPorts[To].TraversalChannel
						&& SortedPorts[From].Cell != SortedPorts[To].Cell
						&& SortedPorts[From].RouteFaceMask != 0 && SortedPorts[To].RouteFaceMask != 0
						&& !DeferredOptionalEdges.ContainsByPredicate([&](const auto& Edge)
							{ return (Edge.Key == From && Edge.Value == To) || (Edge.Key == To && Edge.Value == From); }))
					{
						Edges.Emplace(From, To);
					}
				}
			}
			if (!bCompiledAllRoutes) break;
			Edges.Sort([&](const TPair<int32, int32>& Left, const TPair<int32, int32>& Right)
			{
				if (!bRequireConnections && !bHasProvedAssignment)
				{
					const bool bLeftCommitted = SortedPorts[Left.Value].TraversalCommitment.IsSet();
					const bool bRightCommitted = SortedPorts[Right.Value].TraversalCommitment.IsSet();
					if (bLeftCommitted != bRightCommitted) return bLeftCommitted;
				}
				auto Distance = [&](const TPair<int32, int32>& Edge)
				{
					const FIntVector Delta = SortedPorts[Edge.Key].Cell - SortedPorts[Edge.Value].Cell;
					return FMath::Abs(Delta.X) + FMath::Abs(Delta.Y);
				};
				if (Distance(Left) != Distance(Right)) return Distance(Left) < Distance(Right);
				return Left.Key != Right.Key ? Left.Key < Right.Key : Left.Value < Right.Value;
			});
			bool bConnected = false;
			for (const TPair<int32, int32>& Edge : Edges)
			{
				const FSparseStructuralTraversalPort& RootPort = SortedPorts[Edge.Key];
				const FSparseStructuralTraversalPort& TargetPort = SortedPorts[Edge.Value];
				TArray<FIntVector> Path;
				FString RouteFailureReason;
				// Selected interfaces are graph nodes, not unconstrained corridor interiors.
				// Reach another POI through its own exact approach or proved internal link.
				TSet<FIntVector> EdgeExcludedCells = ExcludedRouteCells;
				for (const auto& Port : SortedPorts)
				{
					if (Port.Cell != RootPort.Cell && Port.Cell != TargetPort.Cell) EdgeExcludedCells.Add(Port.Cell);
				}
				if (!LayoutTraversalPlannerPrivate::FindTraversalRoutePath(
					TrialContext, RootPort.Cell, TargetPort.Cell, RootPort.TraversalChannel, TargetPort.TraversalChannel,
					Path, &RouteFailureReason, false, RootPort.RouteFaceMask, TargetPort.RouteFaceMask,
					&EdgeExcludedCells, &TrialContext, /*bHasExactEndpointProofs=*/true))
				{
					LastFailureReason = RouteFailureReason;
					if (TrialContext.bTimeBudgetExceeded || TrialContext.CandidateAttemptCount >= TrialContext.MaxCandidateAttempts) break;
					continue; // Another component edge may work; a failed pair is not a disconnected graph proof.
				}
				LayoutTraversalPlannerPrivate::MarkRequiredTraversalRoutePath(
					TrialContext, Path, RootPort.TraversalChannel, TargetPort.TraversalChannel, true);
				AttemptPaths.Add(MoveTemp(Path));
				AttemptEdges.Add(Edge);
				ReachedPorts.Add(Edge.Value);
				bCompiledAllRoutes = ExpandInternalReachability();
				bConnected = true;
				break;
			}
			// Extend a proved optional assignment by one corridor, so a later rejection
			// cannot discard an earlier legal subset. Required search still proves its full network.
			if (bConnected && bHasProvedAssignment && !bRequireConnections) break;
			if (!bConnected)
			{
				bool bMissingCommittedRoute = false;
				for (int32 Index = 0; Index < SortedPorts.Num(); ++Index)
				{
					bMissingCommittedRoute |= SortedPorts[Index].TraversalCommitment.IsSet() && !ReachedPorts.Contains(Index);
				}
				bCompiledAllRoutes = !bRequireConnections && !bMissingCommittedRoute && !TrialContext.bTimeBudgetExceeded
					&& TrialContext.CandidateAttemptCount < TrialContext.MaxCandidateAttempts;
				if (LastFailureReason.IsEmpty())
				{
					LastFailureReason = TEXT("No compatible exact-port edge joins the remaining traversal components.");
					for (int32 Index = 0; Index < SortedPorts.Num(); ++Index)
					{
						if (!ReachedPorts.Contains(Index))
						{
							LastFailureReason += FString::Printf(TEXT(" Unreached=%s channel=%s mask=%u root=%d internal=%d reached=%d/%d."),
								*SortedPorts[Index].Cell.ToString(), *SortedPorts[Index].TraversalChannel.ToString(),
								SortedPorts[Index].RouteFaceMask, SortedPorts[Index].bIsEntryRoot,
								SortedPorts[Index].InternallyReachableNodes.Num(), ReachedPorts.Num(), SortedPorts.Num());
							break;
						}
					}
				}
				if (bCompiledAllRoutes)
				{
					// Optional connectivity also serves components disconnected from Entry. Starting
					// another component grants no route proof; its paths still require corridor CSP.
					for (int32 Index = 0; Index < SortedPorts.Num(); ++Index)
					{
						if (!ReachedPorts.Contains(Index))
						{
							ReachedPorts.Add(Index);
							break;
						}
					}
					bCompiledAllRoutes = ExpandInternalReachability();
					continue;
				}
				break;
			}
		}

		if (bHasProvedAssignment && bCompiledAllRoutes && AttemptPaths.Num() == AcceptedPaths.Num()) break;
		bool bSolved = false;
		FSolveContext AttemptSolveContext = TrialContext;
		if (bCompiledAllRoutes)
		{
			TSet<FIntVector> ClaimedWorkCells = PortCells;
			ClaimedWorkCells.Append(NormalWorkCells);
			for (const TArray<FIntVector>& Path : AttemptPaths)
			{
				ClaimedWorkCells.Append(Path);
			}
			FString LocalViewFailureReason;
			// Promote support only when every remaining occupied alternative requires it.
			// Repeat to closure: a support module may itself require another landing/cell.
			auto IncludeMandatorySupport = [&]()
			{
				// Exact selected bundles own their shadow-cell faces before CSP commits
				// placements. Normal candidates at those coordinates are alternatives to
				// the bundle, not additional walls whose support must also be supplied.
				TMap<FIntVector, TPair<const FSolveCandidate*, FIntVector>> CompositeFaces;
				for (const auto& Root : PortCells)
				{
					if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(TrialContext))
					{
						LocalViewFailureReason = TrialContext.Result.FailureReason;
						return false;
					}
					const auto* Restriction = TrialContext.CandidateDomainRestrictionsByCell.Find(Root);
					const auto* RootDomain = TrialContext.InitialDomains.Find(Root);
					if (!Restriction || Restriction->AllowedCandidates.Num() != 1 || !RootDomain || RootDomain->Num() != 1) continue;
					const auto& Candidate = (*RootDomain)[0];
					if (!IsOccupiedCandidate(Candidate) || !TrialContext.ModuleSnapshots.IsValidIndex(Candidate.ModuleSnapshotIndex)) continue;
					const auto& Module = TrialContext.ModuleSnapshots[Candidate.ModuleSnapshotIndex];
					if (Module.OccupiedLocalCells.Num() <= 1) continue;
					for (const auto& LocalCell : Module.OccupiedLocalCells)
					{
						if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(TrialContext))
						{
							LocalViewFailureReason = TrialContext.Result.FailureReason;
							return false;
						}
						const auto Cell = LayoutPlacementOccupancy::ProjectLocalCellToWorld(Root, LocalCell, Module.BoundsCells, Candidate.YawRotationSteps);
						if (CompositeFaces.Contains(Cell))
						{
							LocalViewFailureReason = FString::Printf(TEXT("Selected structural bundles overlap at %s."), *Cell.ToString());
							return false;
						}
						CompositeFaces.Add(Cell, {&Candidate, LocalCell});
					}
				}
				TArray<FIntVector> PendingCells = ClaimedWorkCells.Array();
				TSet<FIntVector> RequiredFilledCells = PortCells;
				for (const auto& Path : AttemptPaths) RequiredFilledCells.Append(Path);
				for (int32 Index = 0; Index < PendingCells.Num(); ++Index)
				{
					const FIntVector Cell = PendingCells[Index];
					const auto* Placement = FindPlacementOrFixedNeighbor(TrialContext, Cell);
					const TArray<FSolveCandidate>* Domain = TrialContext.InitialDomains.Find(Cell);
					// An optional empty offer owes no support until another selected claim
					// requires it filled. Revisit it if that obligation arrives later.
					if ((Placement == nullptr || !FSolveContext::IsOccupiedPlacement(*Placement))
						&& !RequiredFilledCells.Contains(Cell) && Domain != nullptr
						&& Domain->ContainsByPredicate([](const auto& C) { return !IsOccupiedCandidate(C); })) continue;
					for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
					{
						const auto Direction = static_cast<ELayoutFaceDirection>(FaceIndex);
						bool bHasOccupiedAlternative = false;
						bool bAllRequireFilled = true;
						auto RequiresSupport = [](const FLayoutFaceRule& Face)
						{
							return Face.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor
								|| Face.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor;
						};
						if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(TrialContext))
						{
							LocalViewFailureReason = TrialContext.Result.FailureReason;
							return false;
						}
						FLayoutFaceRule Face;
						if (Placement != nullptr && FSolveContext::IsOccupiedPlacement(*Placement))
						{
							bHasOccupiedAlternative = true;
							bAllRequireFilled = TryGetSolvePlacementFaceRule(TrialContext, *Placement, Direction, Face)
								&& RequiresSupport(Face);
						}
						else if (const auto* ExactFace = CompositeFaces.Find(Cell))
						{
							bHasOccupiedAlternative = true;
							bAllRequireFilled = TryGetSolveCandidateLocalFaceRule(TrialContext, *ExactFace->Key,
								ExactFace->Value, Direction, Face) && RequiresSupport(Face);
						}
						else if (Domain != nullptr)
						{
							for (const FSolveCandidate& Candidate : *Domain)
							{
								if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(TrialContext))
								{
									LocalViewFailureReason = TrialContext.Result.FailureReason;
									return false;
								}
								if (!IsOccupiedCandidate(Candidate)) continue;
								bHasOccupiedAlternative = true;
								if (!TryGetSolveCandidateLocalFaceRule(TrialContext, Candidate, FIntVector::ZeroValue, Direction, Face)
									|| !RequiresSupport(Face))
								{
									bAllRequireFilled = false;
									break;
								}
							}
						}
						if (!bHasOccupiedAlternative || !bAllRequireFilled) continue;
						const FIntVector Neighbor = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
						if (ClearanceCells.Contains(Neighbor))
						{
							LocalViewFailureReason = FString::Printf(TEXT("Mandatory structural support conflicts with clearance at %s."), *Neighbor.ToString());
							return false;
						}
						// External terrain/neighbor contracts and child ownership stay with their
						// existing validators; a support closure cannot claim those cells.
						if (!TrialContext.OwningTopologyCellsByPhysicalCell.Contains(Neighbor)
							|| TrialContext.ChildReservationCells.Contains(Neighbor)) continue;
						const auto* FixedSupport = FindPlacementOrFixedNeighbor(TrialContext, Neighbor);
						if (FixedSupport != nullptr && FSolveContext::IsOccupiedPlacement(*FixedSupport)) continue;
						if (!RequiredFilledCells.Contains(Neighbor))
						{
							RequiredFilledCells.Add(Neighbor);
							ClaimedWorkCells.Add(Neighbor);
							PendingCells.Add(Neighbor);
						}
					}
				}
				return true;
			};
			const bool bSupportReady = IncludeMandatorySupport();
			AttemptSolveContext.CandidateAttemptCount = TrialContext.CandidateAttemptCount;
			AttemptSolveContext.bTimeBudgetExceeded |= TrialContext.bTimeBudgetExceeded;
			if (bSupportReady && TryBuildSparseStructuralLocalSolveView(
					TrialContext,
					ClaimedWorkCells,
					AttemptSolveContext,
					LocalViewFailureReason,
					/*bReusePreparedVariants=*/true,
					/*bPrepareIndexedSearch=*/false,
					/*bPrepareDomains=*/false))
			{
				AttemptSolveContext.RequiredRouteConstraints = TrialContext.Result.RouteConstraints;
				AttemptSolveContext.bHasCompleteSparseStructuralRouteAssignment = true;
				// Partial placement cannot prove disconnection while required corridor cells remain unsolved.
				// Run the normal placed-graph audit once the complete tentative assignment exists.
				AttemptSolveContext.bDeferSparseStructuralReachabilityUntilAssignmentComplete = true;
				bSolved = PrepareSolveContextThroughRouteDomainStage(AttemptSolveContext, /*bReusePreparedVariants=*/true)
					&& ContinuePreparedSolveContextAfterRouteDomainStage(AttemptSolveContext, true);
				if (bSolved)
				{
					AttemptSolveContext.bDeferSparseStructuralReachabilityUntilAssignmentComplete = false;
					FString ReachabilityFailureReason;
					bSolved = !bRequireConnections || ValidateReachability(AttemptSolveContext, ReachabilityFailureReason);
					if (bSolved && !AttemptSolveContext.CommittedTraversalAnchors.IsEmpty())
					{
						const auto Reached = BuildReachableWalkableNodesFromPlacedRoots(AttemptSolveContext);
						for (const auto& Anchor : AttemptSolveContext.CommittedTraversalAnchors)
						{
							if (!LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(AttemptSolveContext))
							{
								bSolved = false;
								ReachabilityFailureReason = AttemptSolveContext.Result.FailureReason;
								break;
							}
							if (!Reached.Contains({Anchor.Cell, Anchor.TraversalChannel}))
							{
								bSolved = false;
								ReachabilityFailureReason = FString::Printf(TEXT("Committed traversal anchor %s on channel %s remains unreachable."),
									*Anchor.Cell.ToString(), *Anchor.TraversalChannel.ToString());
								break;
							}
						}
					}
					if (!bSolved)
					{
						TArray<FString> RoutePlacementDetails;
						for (const FLayoutRouteConstraintRecord& Constraint : AttemptSolveContext.Result.RouteConstraints)
						{
							const FSolveContext::FSolvePlacement* Placement = AttemptSolveContext.Placements.Find(Constraint.Cell);
							RoutePlacementDetails.Add(FString::Printf(
								TEXT("%s:faces=%d:module=%s"),
								*Constraint.Cell.ToString(),
								Constraint.FaceRequirements.Num(),
								Placement != nullptr && FSolveContext::IsOccupiedPlacement(*Placement)
									? *Placement->ModuleSnapshotId.ToString()
									: TEXT("<empty>")));
						}
						AttemptSolveContext.Result.FailureReason = FString::Printf(
							TEXT("%s Route assignment: %s"),
							*ReachabilityFailureReason,
							*FString::Join(RoutePlacementDetails, TEXT(", ")));
					}
				}
			}
			else
			{
				AttemptSolveContext.Result.FailureReason = LocalViewFailureReason;
			}
			if (!bSolved)
			{
				LastFailureReason = AttemptSolveContext.Result.FailureReason;
				if (FirstProofFailureReason.IsEmpty())
				{
					FirstProofFailureReason = LastFailureReason;
				}
			}
		}
		WorkingBase.CandidateAttemptCount = FMath::Max(
			WorkingBase.CandidateAttemptCount, AttemptSolveContext.CandidateAttemptCount);
		WorkingBase.bTimeBudgetExceeded |= AttemptSolveContext.bTimeBudgetExceeded;
		if (bSolved)
		{
			for (const FIntVector& Cell : SupportCells)
			{
				const FSolveContext::FSolvePlacement* Placement = FindPlacementOrFixedNeighbor(AttemptSolveContext, Cell);
				if (Placement == nullptr || !FSolveContext::IsOccupiedPlacement(*Placement))
				{
					OutFailureReason = FString::Printf(TEXT("Sparse structural support remains empty at %s."), *Cell.ToString());
					return false;
				}
			}
			for (const FIntVector& Cell : ClearanceCells)
			{
				const FSolveContext::FSolvePlacement* Placement = FindPlacementOrFixedNeighbor(AttemptSolveContext, Cell);
				if (Placement != nullptr && FSolveContext::IsOccupiedPlacement(*Placement))
				{
					OutFailureReason = FString::Printf(TEXT("Sparse structural corridor occupies clearance at %s."), *Cell.ToString());
					return false;
				}
			}
			const bool bCanCommit = LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(AttemptSolveContext);
			WorkingBase.CandidateAttemptCount = AttemptSolveContext.CandidateAttemptCount;
			WorkingBase.bTimeBudgetExceeded |= AttemptSolveContext.bTimeBudgetExceeded;
			if (!bCanCommit)
			{
				LastFailureReason = AttemptSolveContext.Result.FailureReason;
				break;
			}
			OutAssignment.Paths = AttemptPaths;
			OutSolvedContext = MoveTemp(AttemptSolveContext);
			if (bRequireConnections) return true;
			AcceptedPaths = MoveTemp(AttemptPaths);
			AcceptedEdges = MoveTemp(AttemptEdges);
			bHasProvedAssignment = true;
			bStructureOnly = false;
			if (!ImprovementScope)
			{
				ImprovementScope = MakeUnique<LayoutSolveExecution::FOptionalImprovementScope>();
				WorkingBase.MaxCandidateAttempts = WorkingBase.CandidateAttemptCount
					+ FMath::Max(0, WorkingBase.MaxCandidateAttempts - WorkingBase.CandidateAttemptCount) / 2;
				if (WorkingBase.MaxSolveDurationSeconds > 0.0)
				{
					const double Elapsed = FPlatformTime::Seconds() - WorkingBase.SolveStartTimeSeconds;
					WorkingBase.MaxSolveDurationSeconds = Elapsed + FMath::Max(0.0, WorkingBase.MaxSolveDurationSeconds - Elapsed) / 2.0;
				}
			}
			continue;
		}

		if (AttemptSolveContext.bTimeBudgetExceeded
			|| WorkingBase.CandidateAttemptCount >= WorkingBase.MaxCandidateAttempts)
		{
			LastFailureReason = AttemptSolveContext.Result.FailureReason;
			break;
		}
		if (bStructureOnly)
		{
			// A failed structural baseline needs another POI/support assignment.
			// Optional corridors must not spend the remaining budget trying to
			// repair incompatible fixed providers or mandatory face contracts.
			break;
		}
		FIntVector RejectedCell = AttemptSolveContext.Result.RouteDomainFilterDiagnostics.bFailedConstraint
			? AttemptSolveContext.Result.RouteDomainFilterDiagnostics.FailedConstraintCell
			: FIntVector(MAX_int32, MAX_int32, MAX_int32);
		if (PortCells.Contains(RejectedCell)
			|| ExcludedRouteCells.Contains(RejectedCell)
			|| !PreparedBaseContext.PlannedCellIntents.Contains(RejectedCell))
		{
			RejectedCell = FIntVector(MAX_int32, MAX_int32, MAX_int32);
			for (int32 PathIndex = AttemptPaths.Num() - 1; PathIndex >= 0 && RejectedCell.X == MAX_int32; --PathIndex)
			{
				const TArray<FIntVector>& Path = AttemptPaths[PathIndex];
				// Keep selected endpoint approaches fixed; retry corridor cells before the final port edge.
				for (int32 CellIndex = Path.Num() - 3; CellIndex > 0; --CellIndex)
				{
					if (!PortCells.Contains(Path[CellIndex]) && !ExcludedRouteCells.Contains(Path[CellIndex]))
					{
						RejectedCell = Path[CellIndex];
						break;
					}
				}
			}
		}
		if (RejectedCell.X == MAX_int32)
		{
			if (bHasProvedAssignment && AttemptEdges.Num() > AcceptedEdges.Num())
			{
				// Assignment-local scheduling choice, not proof that this port pair is infeasible.
				DeferredOptionalEdges.Add(AttemptEdges.Last());
				continue;
			}
			break;
		}
		ExcludedRouteCells.Add(RejectedCell);
		OutAssignment.RejectedRouteCells.Add(RejectedCell);
	}

	ImprovementScope.Reset();
	if (bHasProvedAssignment)
	{
		WorkingBase.MaxCandidateAttempts = PreparedBaseContext.MaxCandidateAttempts;
		WorkingBase.MaxSolveDurationSeconds = PreparedBaseContext.MaxSolveDurationSeconds;
		WorkingBase.bTimeBudgetExceeded = PreparedBaseContext.bTimeBudgetExceeded;
		if (LayoutTraversalPlannerPrivate::ConsumeSparseRouteWork(WorkingBase))
		{
			OutSolvedContext.CandidateAttemptCount = WorkingBase.CandidateAttemptCount;
			OutSolvedContext.MaxCandidateAttempts = WorkingBase.MaxCandidateAttempts;
			OutSolvedContext.MaxSolveDurationSeconds = WorkingBase.MaxSolveDurationSeconds;
			OutFailureReason.Reset();
			return true;
		}
		LastFailureReason = WorkingBase.Result.FailureReason;
	}
	OutSolvedContext = FSolveContext{};
	OutAssignment.Paths.Reset();
	OutFailureReason = LastFailureReason.IsEmpty()
		? TEXT("Sparse structural exact-port assignment exhausted its bounded route alternatives.")
		: LastFailureReason;
	if (!FirstProofFailureReason.IsEmpty() && FirstProofFailureReason != LastFailureReason)
	{
		OutFailureReason = FString::Printf(TEXT("First proof: %s Last route: %s"), *FirstProofFailureReason, *OutFailureReason);
	}
	return false;
}

void LayoutProfileSolverInternal::BuildRequiredTraversalRoutesForPreparedPlan(FSolveContext& Context)
{
	PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_RoutePreparation, STAT_PorismLayout_RoutePreparation);
	Context.RouteConstraintIndexByCell.Reset();
	Context.ReservationFlagsByCell.Reset();
	Context.Result.RouteConstraints.Reset();
	Context.Result.CompiledReservations.Reset();
	LayoutTraversalPlannerPrivate::SeedRequestOwnedRequiredRouteConstraints(Context);
	if (Context.bHasCompleteSparseStructuralRouteAssignment)
	{
		return;
	}

	const bool bHasSparseTerrainRouteAuthority =
		!Context.TerrainResidualRuleIdByCell.IsEmpty();
	if (Context.bDeferFinalReachabilityAudit && !bHasSparseTerrainRouteAuthority)
	{
		// Legacy recursive regions defer route construction to merged schedule validation.
		return;
	}

	const FGameplayTag DefaultTraversalTag = LayoutGameplayTags::TraversalPrimary;
	if (!DefaultTraversalTag.IsValid())
	{
		return;
	}

	using LayoutTraversalPlannerPrivate::FTraversalRouteAnchor;
	auto GetTraversalRouteAnchorSortKey = [](const FTraversalRouteAnchor& Anchor) -> FString
	{
		return FString::Printf(
			TEXT("%d|%d|%d|%s"),
			Anchor.Cell.Z,
			Anchor.Cell.Y,
			Anchor.Cell.X,
			Anchor.TraversalTag.IsValid() ? *Anchor.TraversalTag.ToString() : TEXT("<none>"));
	};
	auto IsPreferredTraversalRouteAnchor = [&GetTraversalRouteAnchorSortKey](const FTraversalRouteAnchor& Left, const FTraversalRouteAnchor& Right) -> bool
	{
		if (Left.Cell.Z != Right.Cell.Z)
		{
			return Left.Cell.Z < Right.Cell.Z;
		}
		if (Left.Cell.Y != Right.Cell.Y)
		{
			return Left.Cell.Y < Right.Cell.Y;
		}
		if (Left.Cell.X != Right.Cell.X)
		{
			return Left.Cell.X < Right.Cell.X;
		}

		return GetTraversalRouteAnchorSortKey(Left) < GetTraversalRouteAnchorSortKey(Right);
	};
	auto IsPreferredTraversalPath = [](const TArray<FIntVector>& Left, const TArray<FIntVector>& Right) -> bool
	{
		const int32 SharedLength = FMath::Min(Left.Num(), Right.Num());
		for (int32 Index = 0; Index < SharedLength; ++Index)
		{
			if (Left[Index].Z != Right[Index].Z)
			{
				return Left[Index].Z < Right[Index].Z;
			}
			if (Left[Index].Y != Right[Index].Y)
			{
				return Left[Index].Y < Right[Index].Y;
			}
			if (Left[Index].X != Right[Index].X)
			{
				return Left[Index].X < Right[Index].X;
			}
		}

		return Left.Num() < Right.Num();
	};

	TMap<FIntVector, int32> PlannedComponentByCell;
	TArray<FIntVector> SortedPlannedCells;
	Context.PlannedCellIntents.GetKeys(SortedPlannedCells);
	SortedPlannedCells.Sort([](const FIntVector& Left, const FIntVector& Right)
	{
		if (Left.Z != Right.Z) return Left.Z < Right.Z;
		if (Left.Y != Right.Y) return Left.Y < Right.Y;
		return Left.X < Right.X;
	});
	int32 NextPlannedComponentId = 0;
	for (const FIntVector& RootCell : SortedPlannedCells)
	{
		if (PlannedComponentByCell.Contains(RootCell))
		{
			continue;
		}

		TArray<FIntVector> Pending = {RootCell};
		PlannedComponentByCell.Add(RootCell, NextPlannedComponentId);
		while (!Pending.IsEmpty())
		{
			const FIntVector Current = Pending.Pop(EAllowShrinking::No);
			for (const FIntVector& Delta : {
				FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
				FIntVector(0, 1, 0), FIntVector(0, -1, 0)})
			{
				const FIntVector Neighbor = Current + Delta;
				if (Context.PlannedCellIntents.Contains(Neighbor)
					&& !PlannedComponentByCell.Contains(Neighbor))
				{
					PlannedComponentByCell.Add(Neighbor, NextPlannedComponentId);
					Pending.Add(Neighbor);
				}
			}
		}
		++NextPlannedComponentId;
	}

	TMap<int32, TArray<FTraversalRouteAnchor>> AnchorsByComponent;
	TMap<FIntVector, TArray<FString>> AnchorOriginsByCell;
	auto AddRouteAnchor = [&AnchorsByComponent, &AnchorOriginsByCell, &PlannedComponentByCell, DefaultTraversalTag](
		const FIntVector& Cell,
		const FGameplayTag TraversalTag,
		const FString& Origin)
	{
		const int32* ComponentId = PlannedComponentByCell.Find(Cell);
		if (ComponentId == nullptr)
		{
			return;
		}
		const FGameplayTag EffectiveTraversalTag = TraversalTag.IsValid() ? TraversalTag : DefaultTraversalTag;
		AnchorOriginsByCell.FindOrAdd(Cell).AddUnique(Origin);
		TArray<FTraversalRouteAnchor>& Anchors = AnchorsByComponent.FindOrAdd(*ComponentId);
		if (Anchors.ContainsByPredicate([&Cell, &EffectiveTraversalTag](const FTraversalRouteAnchor& ExistingAnchor)
		{
			return ExistingAnchor.Cell == Cell
				&& ExistingAnchor.TraversalTag == EffectiveTraversalTag;
		}))
		{
			return;
		}

		FTraversalRouteAnchor& Anchor = Anchors.AddDefaulted_GetRef();
		Anchor.Cell = Cell;
		Anchor.TraversalTag = EffectiveTraversalTag;
	};

	for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
	{
		if (PlannedCell.Intent == ELayoutCellIntent::Boundary)
		{
			LayoutTraversalPlannerPrivate::AddCompiledReservation(Context, PlannedCell.Cell, ELayoutCellReservationKind::BoundaryShell);
		}
		else if (PlannedCell.Intent == ELayoutCellIntent::Interior || PlannedCell.Intent == ELayoutCellIntent::Core)
		{
			LayoutTraversalPlannerPrivate::AddCompiledReservation(Context, PlannedCell.Cell, ELayoutCellReservationKind::InteriorFill);
		}

		if (PlannedCell.Intent == ELayoutCellIntent::Entry)
		{
			const FGameplayTag AnchorTraversalTag = LayoutTraversalPlannerPrivate::ChooseRouteAnchorTraversalTagForCell(Context, PlannedCell.Cell, DefaultTraversalTag);
			AddRouteAnchor(PlannedCell.Cell, AnchorTraversalTag, TEXT("authored or inherited Entry"));
		}
		else if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
		{
			const FGameplayTag AnchorTraversalTag = LayoutTraversalPlannerPrivate::ChooseRouteAnchorTraversalTagForCell(Context, PlannedCell.Cell, DefaultTraversalTag);
			AddRouteAnchor(PlannedCell.Cell, AnchorTraversalTag, TEXT("VerticalAccess lower endpoint"));
			const FIntVector UpperLandingCell = PlannedCell.Cell + FIntVector(0, 0, 1);
			if (Context.PlannedCellIntents.Contains(UpperLandingCell))
			{
				AddRouteAnchor(UpperLandingCell, AnchorTraversalTag, TEXT("VerticalAccess upper landing"));
				LayoutTraversalPlannerPrivate::AddCompiledReservation(Context, UpperLandingCell, ELayoutCellReservationKind::VerticalContinuation);
			}
			LayoutTraversalPlannerPrivate::AddCompiledReservation(Context, PlannedCell.Cell, ELayoutCellReservationKind::VerticalContinuation);
		}
	}

	for (const FLayoutCommittedTraversalAnchor& Anchor : Context.CommittedTraversalAnchors)
	{
		if (!Context.PlannedCellIntents.Contains(Anchor.Cell))
		{
			continue;
		}

		const FGameplayTag AnchorTraversalTag = Anchor.TraversalChannel.IsValid()
			? Anchor.TraversalChannel
			: LayoutTraversalPlannerPrivate::ChooseRouteAnchorTraversalTagForCell(Context, Anchor.Cell, DefaultTraversalTag);
		AddRouteAnchor(Anchor.Cell, AnchorTraversalTag, TEXT("committed traversal anchor"));
		LayoutTraversalPlannerPrivate::AddCompiledReservation(Context, Anchor.Cell, ELayoutCellReservationKind::FutureCompoundConnection);
	}

	for (const FLayoutSolveBoundaryPoint& BoundaryPoint : Context.IncomingBoundaryPoints)
	{
		if (BoundaryPoint.ConnectedTraversalChannels.IsEmpty())
		{
			continue;
		}

		// Only explicit committed contacts should hard-anchor the route network. Other incoming
		// traversable filled neighbors stay as compatibility context so optional sibling/parent-child
		// openings do not overconstrain the solve when one valid door/contact is enough.
		if (BoundaryPoint.bRepresentsFilledNeighbor && BoundaryPoint.CommitmentId == NAME_None)
		{
			continue;
		}

		const FIntVector InteriorCell = BoundaryPoint.LocalCell + FLayoutDirectionUtils::ToCellDelta(BoundaryPoint.FaceDirection);
		if (Context.PlannedCellIntents.Contains(InteriorCell))
		{
			TArray<FGameplayTag> BoundaryTraversalTags = LayoutTraversalPlannerPrivate::GetSortedTags(BoundaryPoint.ConnectedTraversalChannels);
			AddRouteAnchor(
				InteriorCell,
				BoundaryTraversalTags.IsEmpty() ? DefaultTraversalTag : BoundaryTraversalTags[0],
				FString::Printf(TEXT("committed boundary contact '%s'"), *BoundaryPoint.CommitmentId.ToString()));
			LayoutTraversalPlannerPrivate::AddCompiledReservation(Context, InteriorCell, ELayoutCellReservationKind::FutureCompoundConnection);
		}
	}

	for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& FixedNeighborPair : Context.FixedNeighborPlacements)
	{
		const FIntVector ContinuationCell = FixedNeighborPair.Key + FIntVector(0, 0, 1);
		if (!Context.PlannedCellIntents.Contains(ContinuationCell) || !FSolveContext::IsOccupiedPlacement(FixedNeighborPair.Value))
		{
			continue;
		}

		FLayoutFaceRule TopFaceRule;
		if (!LayoutTraversalPlannerPrivate::TryGetSolvePlacementFaceRule(Context, FixedNeighborPair.Value, ELayoutFaceDirection::PosZ, TopFaceRule)
			|| TopFaceRule.ConnectedTraversalChannels.IsEmpty())
		{
			continue;
		}

		TArray<FGameplayTag> ContinuationTraversalTags = LayoutTraversalPlannerPrivate::GetSortedTags(TopFaceRule.ConnectedTraversalChannels);
		AddRouteAnchor(
			ContinuationCell,
			ContinuationTraversalTags.IsEmpty() ? DefaultTraversalTag : ContinuationTraversalTags[0],
			TEXT("fixed-neighbor vertical continuation"));
		LayoutTraversalPlannerPrivate::AddCompiledReservation(Context, ContinuationCell, ELayoutCellReservationKind::VerticalContinuation);
	}

	TArray<FIntVector> ExistingAnchorCells;
	for (const TPair<int32, TArray<FTraversalRouteAnchor>>& Pair : AnchorsByComponent)
	{
		for (const FTraversalRouteAnchor& Anchor : Pair.Value)
		{
			ExistingAnchorCells.AddUnique(Anchor.Cell);
		}
	}
	if (bHasSparseTerrainRouteAuthority)
	{
		for (const FIntVector& Representative :
			LayoutTraversalPlannerPrivate::BuildTraversalComponentRepresentatives(
				Context,
				ExistingAnchorCells))
		{
			AddRouteAnchor(
				Representative,
				LayoutTraversalPlannerPrivate::ChooseRouteAnchorTraversalTagForCell(
					Context,
					Representative,
					DefaultTraversalTag),
				TEXT("derived traversal-capable structural component representative"));
		}
	}

	TArray<int32> AnchorComponentIds;
	AnchorsByComponent.GetKeys(AnchorComponentIds);
	AnchorComponentIds.Sort();
	for (const int32 ComponentId : AnchorComponentIds)
	{
		TArray<FTraversalRouteAnchor>& Anchors = AnchorsByComponent.FindChecked(ComponentId);
		Anchors.Sort([&IsPreferredTraversalRouteAnchor](const FTraversalRouteAnchor& Left, const FTraversalRouteAnchor& Right)
		{
			return IsPreferredTraversalRouteAnchor(Left, Right);
		});

		if (Anchors.Num() < 2)
		{
			continue;
		}

		TArray<FTraversalRouteAnchor> ConnectedAnchors;
		ConnectedAnchors.Add(Anchors[0]);
		TArray<FTraversalRouteAnchor> RemainingAnchors;
		RemainingAnchors.Append(Anchors.GetData() + 1, Anchors.Num() - 1);
		TMap<FIntVector, TArray<FString>> RouteBlockersByCell;
		auto RecordRouteBlocker = [&RouteBlockersByCell](
			const FIntVector& Cell,
			const FIntVector& SourceCell,
			FString&& Blocker)
		{
			if (Blocker.IsEmpty())
			{
				return;
			}
			TArray<FString>& Blockers = RouteBlockersByCell.FindOrAdd(Cell);
			if (Blockers.Num() < 8)
			{
				Blockers.Add(FString::Printf(
					TEXT("from %s: %s"),
					*SourceCell.ToString(),
					*Blocker));
			}
		};
		while (!RemainingAnchors.IsEmpty())
		{
			int32 BestTargetIndex = INDEX_NONE;
			TArray<FIntVector> BestPath;
			FGameplayTag BestStartTraversalTag = DefaultTraversalTag;
			int32 BestPathCost = MAX_int32;
			FTraversalRouteAnchor BestSourceAnchor;

			for (int32 TargetIndex = 0; TargetIndex < RemainingAnchors.Num(); ++TargetIndex)
			{
				const FTraversalRouteAnchor& TargetAnchor = RemainingAnchors[TargetIndex];
				TArray<const FTraversalRouteAnchor*> CandidateSourceAnchors;
				for (const FTraversalRouteAnchor& ConnectedAnchor : ConnectedAnchors)
				{
					if (ConnectedAnchors.Num() == 1
						|| LayoutTraversalPlannerPrivate::CellCanServeAsRouteJunction(Context, ConnectedAnchor.Cell))
					{
						CandidateSourceAnchors.Add(&ConnectedAnchor);
					}
				}
				if (CandidateSourceAnchors.IsEmpty())
				{
					for (const FTraversalRouteAnchor& ConnectedAnchor : ConnectedAnchors)
					{
						CandidateSourceAnchors.Add(&ConnectedAnchor);
					}
				}
				CandidateSourceAnchors.Sort([&IsPreferredTraversalRouteAnchor](const FTraversalRouteAnchor& Left, const FTraversalRouteAnchor& Right)
				{
					return IsPreferredTraversalRouteAnchor(Left, Right);
				});

				for (const FTraversalRouteAnchor* ConnectedAnchorPtr : CandidateSourceAnchors)
				{
					if (ConnectedAnchorPtr == nullptr)
					{
						continue;
					}

					const FTraversalRouteAnchor& ConnectedAnchor = *ConnectedAnchorPtr;
					TArray<FIntVector> CandidatePath;
					FString RouteBlocker;
					if (!LayoutTraversalPlannerPrivate::FindTraversalRoutePath(
							Context,
							ConnectedAnchor.Cell,
							TargetAnchor.Cell,
							ConnectedAnchor.TraversalTag,
							TargetAnchor.TraversalTag,
							CandidatePath,
							&RouteBlocker))
					{
						RecordRouteBlocker(
							TargetAnchor.Cell,
							ConnectedAnchor.Cell,
							MoveTemp(RouteBlocker));
						continue;
					}
					if (!LayoutTraversalPlannerPrivate::CanPathFitHorizontalRouteFaceCapacity(
							Context,
							CandidatePath,
							&RouteBlocker))
					{
						RecordRouteBlocker(
							TargetAnchor.Cell,
							ConnectedAnchor.Cell,
							MoveTemp(RouteBlocker));
						continue;
					}

					int32 CandidatePathCost = 0;
					for (const FIntVector& PathCell : CandidatePath)
					{
						CandidatePathCost += LayoutTraversalPlannerPrivate::GetTraversalRouteCellCost(
							Context,
							PathCell,
							TargetAnchor.Cell,
							true);
					}

					const bool bIsBetterTarget = BestTargetIndex == INDEX_NONE
						|| IsPreferredTraversalRouteAnchor(TargetAnchor, RemainingAnchors[BestTargetIndex]);
					const bool bSameTargetAsBest = BestTargetIndex != INDEX_NONE
						&& TargetAnchor.Cell == RemainingAnchors[BestTargetIndex].Cell
						&& TargetAnchor.TraversalTag == RemainingAnchors[BestTargetIndex].TraversalTag;
					const bool bIsBetterSource = IsPreferredTraversalRouteAnchor(ConnectedAnchor, BestSourceAnchor);
					const bool bPreferCandidate = BestTargetIndex == INDEX_NONE
						|| CandidatePathCost < BestPathCost
						|| (CandidatePathCost == BestPathCost
							&& (bIsBetterTarget
								|| (bSameTargetAsBest
									&& (bIsBetterSource
										|| (!bIsBetterSource
											&& !IsPreferredTraversalRouteAnchor(BestSourceAnchor, ConnectedAnchor)
											&& IsPreferredTraversalPath(CandidatePath, BestPath))))));
					if (bPreferCandidate)
					{
						BestTargetIndex = TargetIndex;
						BestPathCost = CandidatePathCost;
						BestStartTraversalTag = ConnectedAnchor.TraversalTag;
						BestSourceAnchor = ConnectedAnchor;
						BestPath = MoveTemp(CandidatePath);
					}
				}
			}

			if (BestTargetIndex == INDEX_NONE || BestPath.IsEmpty())
			{
				const FTraversalRouteAnchor& TargetAnchor = RemainingAnchors[0];
				TArray<FIntVector> SameLevelCells;
				for (const TPair<FIntVector, ELayoutCellIntent>& CellIntentPair : Context.PlannedCellIntents)
				{
					if (CellIntentPair.Key.Z == TargetAnchor.Cell.Z)
					{
						SameLevelCells.Add(CellIntentPair.Key);
					}
				}
				SameLevelCells.Sort([](const FIntVector& Left, const FIntVector& Right)
				{
					return Left.Y != Right.Y ? Left.Y < Right.Y : Left.X < Right.X;
				});
				FLayoutValidationMessage& Message = Context.Result.Messages.AddDefaulted_GetRef();
				const bool bProfileRequiresStrictReachability =
					Context.ProfileSnapshot.bRequireAllTraversalChannelsReachable;
				const bool bEnforceInRegionalSolve =
					bProfileRequiresStrictReachability
					&& !Context.bDeferFinalReachabilityAudit;
				Message.Severity = bEnforceInRegionalSolve
					? ELayoutValidationSeverity::Error
					: ELayoutValidationSeverity::Warning;
				Message.Message = LayoutTraversalPlannerPrivate::BuildStructuredRouteDiagnostic(
					bEnforceInRegionalSolve
						? TEXT("Required traversal anchor could not be connected to the existing route network.")
						: bProfileRequiresStrictReachability
							? TEXT("Regional traversal component remains unresolved; merged schedule validation must connect it. Successful local route claims remain applied.")
							: TEXT("Optional traversal component could not be connected; successful route claims remain applied."),
					{
						FString::Printf(TEXT("Level: %d"), TargetAnchor.Cell.Z),
						FString::Printf(TEXT("Target anchor cell: %s"), *TargetAnchor.Cell.ToString()),
						FString::Printf(TEXT("Target traversal channel: %s"), TargetAnchor.TraversalTag.IsValid() ? *TargetAnchor.TraversalTag.ToString() : TEXT("<none>")),
						FString::Printf(
							TEXT("Enforcement: profileStrict=%s regionalAuditDeferred=%s"),
							bProfileRequiresStrictReachability ? TEXT("true") : TEXT("false"),
							Context.bDeferFinalReachabilityAudit ? TEXT("true") : TEXT("false")),
						FString::Printf(
							TEXT("Target anchor origin: %s"),
							*FString::Join(AnchorOriginsByCell.FindRef(TargetAnchor.Cell), TEXT(", "))),
						FString::Printf(TEXT("Target anchor state: %s"), *LayoutTraversalPlannerPrivate::DescribeRouteAnchorCellState(Context, TargetAnchor.Cell)),
						FString::Printf(TEXT("Target anchor exact edges: %s"), *LayoutTraversalPlannerPrivate::DescribeRouteAnchorDomainEdges(Context, TargetAnchor.Cell)),
						FString::Printf(
							TEXT("Route-search blockers: %s"),
							*FString::Join(
								RouteBlockersByCell.FindRef(TargetAnchor.Cell),
								TEXT(" | "))),
						FString::Printf(TEXT("Connected anchors: %s"), *FString::JoinBy(ConnectedAnchors, TEXT(", "), [](const FTraversalRouteAnchor& Anchor)
						{
							return FString::Printf(
								TEXT("%s[%s]"),
								*Anchor.Cell.ToString(),
								Anchor.TraversalTag.IsValid() ? *Anchor.TraversalTag.ToString() : TEXT("<none>"));
						})),
						FString::Printf(TEXT("Connected anchor states: %s"), *FString::JoinBy(ConnectedAnchors, TEXT(" | "), [&Context](const FTraversalRouteAnchor& Anchor)
						{
							return LayoutTraversalPlannerPrivate::DescribeRouteAnchorCellState(Context, Anchor.Cell);
						})),
						FString::Printf(TEXT("Same-level cells: %s"), *FString::JoinBy(SameLevelCells, TEXT(" | "), [&Context](const FIntVector& Cell)
						{
							return LayoutTraversalPlannerPrivate::DescribeRouteAnchorCellState(Context, Cell);
						})),
						TEXT("Root cause: no remaining traversal anchor could be connected to the current route tree through planned same-level cells."),
						TEXT("Fix: move blocking child regions or walls, add traversable interior space between anchors, or adjust entry/seam commitments so the required contact lands on a connected corridor.")
					});
				if (bEnforceInRegionalSolve)
				{
					Context.Result.FailureReason = Message.Message;
					return;
				}
				break;
			}

			const FTraversalRouteAnchor TargetAnchor = RemainingAnchors[BestTargetIndex];
			LayoutTraversalPlannerPrivate::MarkRequiredTraversalRoutePath(Context, BestPath, BestStartTraversalTag, TargetAnchor.TraversalTag);
			ConnectedAnchors.Add(TargetAnchor);
			RemainingAnchors.RemoveAt(BestTargetIndex, 1, EAllowShrinking::No);
		}
	}
}

bool LayoutProfileSolverInternal::CellHasReservation(
	const FSolveContext& Context,
	const FIntVector& Cell,
	const ELayoutCellReservationKind ReservationKind)
{
	return LayoutTraversalPlannerPrivate::CellHasReservation(Context, Cell, ReservationKind);
}

bool LayoutProfileSolverInternal::IsRequiredTraversalRouteCell(const FSolveContext& Context, const FIntVector& Cell)
{
	return LayoutTraversalPlannerPrivate::FindRouteConstraint(Context, Cell) != nullptr;
}

int32 LayoutProfileSolverInternal::GetCandidateRequiredTraversalRouteScore(
	const FSolveContext& Context,
	const FIntVector& Cell,
	const FSolveCandidate& Candidate)
{
	return LayoutTraversalPlannerPrivate::GetCandidateRequiredTraversalRouteScore(Context, Cell, Candidate);
}

bool LayoutProfileSolverInternal::ShouldUseFullReachableHorizontalFaceScore(const FSolveContext& Context, const FIntVector& Cell)
{
	return LayoutTraversalPlannerPrivate::CellHasReservation(Context, Cell, ELayoutCellReservationKind::RequiredRoute)
		|| LayoutTraversalPlannerPrivate::CellHasReservation(Context, Cell, ELayoutCellReservationKind::RouteJunction)
		|| LayoutTraversalPlannerPrivate::CellHasReservation(Context, Cell, ELayoutCellReservationKind::FutureCompoundConnection)
		|| LayoutTraversalPlannerPrivate::CellHasReservation(Context, Cell, ELayoutCellReservationKind::VerticalContinuation);
}

bool LayoutProfileSolverInternal::ShouldPreferStructuredFillTraversalExposure(const FSolveContext& Context, const FIntVector& Cell)
{
	const ELayoutCellIntent* Intent = Context.PlannedCellIntents.Find(Cell);
	if (Intent == nullptr || IsRequiredTraversalRouteCell(Context, Cell))
	{
		return false;
	}

	if (*Intent != ELayoutCellIntent::Core
		&& *Intent != ELayoutCellIntent::Interior
		&& *Intent != ELayoutCellIntent::Connector)
	{
		return false;
	}

	return LayoutTraversalPlannerPrivate::CellHasReservation(Context, Cell, ELayoutCellReservationKind::InteriorFill)
		|| LayoutTraversalPlannerPrivate::CellHasReservation(Context, Cell, ELayoutCellReservationKind::ReachabilityBranch);
}

bool LayoutProfileSolverInternal::ShouldPreferLeftStructuredFillTraversalExposure(
	const FSolveContext& Context,
	const FSolveCandidate& Left,
	const FSolveCandidate& Right,
	bool& bOutHasPreference)
{
	bOutHasPreference = false;
	const int32 LeftExposureScore = LayoutTraversalPlannerPrivate::GetCandidateHorizontalTraversalExposureScore(Context, Left);
	const int32 RightExposureScore = LayoutTraversalPlannerPrivate::GetCandidateHorizontalTraversalExposureScore(Context, Right);
	if (LeftExposureScore == RightExposureScore)
	{
		return false;
	}

	bOutHasPreference = true;
	if (LeftExposureScore <= 0 || RightExposureScore <= 0)
	{
		return LeftExposureScore > RightExposureScore;
	}

	return LeftExposureScore < RightExposureScore;
}

int32 LayoutProfileSolverInternal::GetReservationSelectionPriority(const FSolveContext& Context, const FIntVector& Cell)
{
	const ELayoutCellIntent* Intent = Context.PlannedCellIntents.Find(Cell);
	if (IsRequiredTraversalRouteCell(Context, Cell)
		|| LayoutTraversalPlannerPrivate::CellHasReservation(Context, Cell, ELayoutCellReservationKind::VerticalContinuation)
		|| LayoutTraversalPlannerPrivate::CellHasReservation(Context, Cell, ELayoutCellReservationKind::FutureCompoundConnection)
		|| (Intent != nullptr && (*Intent == ELayoutCellIntent::Entry || *Intent == ELayoutCellIntent::VerticalAccess)))
	{
		return 0;
	}

	if (LayoutTraversalPlannerPrivate::CellHasReservation(Context, Cell, ELayoutCellReservationKind::BoundaryShell))
	{
		return 1;
	}

	return 2;
}

int32 LayoutProfileSolverInternal::GetBestHorizontalTraversalExposureForCellForTests(
	const FSolveContext& Context,
	const FIntVector& Cell)
{
	return LayoutTraversalPlannerPrivate::GetBestHorizontalTraversalExposureForCell(Context, Cell);
}

TArray<FIntVector> LayoutProfileSolverInternal::BuildTraversalComponentRepresentativesForTests(
	const FSolveContext& Context,
	const TArray<FIntVector>& ExistingAnchorCells)
{
	return LayoutTraversalPlannerPrivate::BuildTraversalComponentRepresentatives(
		Context,
		ExistingAnchorCells);
}

bool LayoutProfileSolverInternal::ShouldAllowBoundaryCellsAsIntermediateRouteStepForTests(
	const FSolveContext& Context,
	const FIntVector& Cell)
{
	return LayoutTraversalPlannerPrivate::ShouldAllowBoundaryCellsAsIntermediateRouteSteps(Context, Cell);
}

ELayoutLevelFillMode LayoutProfileSolverInternal::GetLevelFillModeForCellForTests(
	const FSolveContext& Context,
	const FIntVector& Cell)
{
	return LayoutTraversalPlannerPrivate::GetLevelFillModeForCell(Context, Cell);
}

bool LayoutProfileSolverInternal::ApplyRequiredTraversalRouteDomainConstraints(FSolveContext& Context)
{
	Context.Result.RouteDomainFilterDiagnostics =
		FLayoutRouteDomainFilterDiagnostics{};
	if (Context.Result.RouteConstraints.IsEmpty())
	{
		return true;
	}

	FLayoutRouteDomainFilterDiagnostics& Diagnostics =
		Context.Result.RouteDomainFilterDiagnostics;
	Diagnostics.bApplied = true;
	Diagnostics.TightestRemainingDomainSize = MAX_int32;

	for (const FLayoutRouteConstraintRecord& RouteConstraint : Context.Result.RouteConstraints)
	{
		++Diagnostics.ConstrainedCellCount;
		if (RouteConstraint.Intent == ELayoutCellIntent::Boundary)
		{
			++Diagnostics.BoundaryConstrainedCellCount;
			if (RouteConstraint.FaceRequirements.Num() > 1)
			{
				++Diagnostics.MultiFaceBoundaryConstrainedCellCount;
			}
		}

		TArray<FSolveCandidate>* Domain = Context.InitialDomains.Find(RouteConstraint.Cell);
		if (Domain == nullptr)
		{
			continue;
		}

		if (Domain->IsEmpty())
		{
			const FSolveContext::FSolvePlacement* const FixedPlacement =
				Context.Placements.Find(RouteConstraint.Cell);
			const TArray<FSolveCandidate>* const RootDomain = FixedPlacement != nullptr
				? Context.InitialDomains.Find(FixedPlacement->BundleRootCell)
				: nullptr;
			const FSolveCandidate* const OwningCandidate = RootDomain != nullptr
				? RootDomain->FindByPredicate([FixedPlacement](const FSolveCandidate& Candidate)
				{
					return FixedPlacement != nullptr
						&& Candidate.VariantIndex == FixedPlacement->VariantIndex
						&& Candidate.YawRotationSteps == FixedPlacement->YawRotationSteps;
				})
				: nullptr;
			if (FixedPlacement != nullptr
				&& FSolveContext::IsOccupiedPlacement(*FixedPlacement)
				&& OwningCandidate != nullptr)
			{
				FString FailureReason;
				LayoutTraversalPlannerPrivate::FRouteConstraintCandidateFailureDetail FailureDetail;
				if (LayoutTraversalPlannerPrivate::DoesCandidateSatisfyRequiredTraversalRoute(
						Context,
						RouteConstraint.Cell,
						*OwningCandidate,
						&FailureReason,
						&FailureDetail,
						FixedPlacement->LocalBundleCell))
				{
					++Diagnostics.TotalEligibleCandidateCount;
					Diagnostics.TightestRemainingDomainSize = FMath::Min(
						Diagnostics.TightestRemainingDomainSize,
						1);
					continue;
				}

				FLayoutValidationMessage& Message = Context.Result.Messages.AddDefaulted_GetRef();
				Message.Severity = ELayoutValidationSeverity::Error;
				Message.Message = FString::Printf(
					TEXT("Fixed bundle cell %s owned by root %s cannot satisfy its shadow-local route contract. %s"),
					*RouteConstraint.Cell.ToString(),
					*FixedPlacement->BundleRootCell.ToString(),
					FailureReason.IsEmpty() ? TEXT("No compatible shadow-local traversal face remains.") : *FailureReason);
				Context.Result.FailureReason = Message.Message;
				return false;
			}

			FLayoutValidationMessage& Message = Context.Result.Messages.AddDefaulted_GetRef();
			Message.Severity = ELayoutValidationSeverity::Error;
			Message.Message = LayoutTraversalPlannerPrivate::BuildEmptyRouteDomainMessage(Context, RouteConstraint);
			Context.Result.FailureReason = Message.Message;
			return false;
		}

		TArray<FString> CandidateFailureSummaries;
		TArray<int32> CandidateFailureCountsByKind;
		CandidateFailureCountsByKind.Init(
			0,
			static_cast<int32>(ELayoutRouteDomainFailureKind::MissingInternalTraversalConnectivity) + 1);
		const int32 InitialCandidateCount = Domain->Num();
		Diagnostics.TotalEligibleCandidateCount += InitialCandidateCount;

		for (int32 CandidateIndex = Domain->Num() - 1; CandidateIndex >= 0; --CandidateIndex)
		{
			FString FailureReason;
			LayoutTraversalPlannerPrivate::FRouteConstraintCandidateFailureDetail
				FailureDetail;
			if (!LayoutTraversalPlannerPrivate::DoesCandidateSatisfyRequiredTraversalRoute(
				Context,
				RouteConstraint.Cell,
				(*Domain)[CandidateIndex],
				&FailureReason,
				&FailureDetail))
			{
				const int32 FailureKindIndex =
					static_cast<int32>(FailureDetail.FailureKind);
				if (CandidateFailureCountsByKind.IsValidIndex(FailureKindIndex))
				{
					++CandidateFailureCountsByKind[FailureKindIndex];
				}
				if (!FailureReason.IsEmpty() && CandidateFailureSummaries.Num() < Context.MaxFailureDetails)
				{
					CandidateFailureSummaries.Add(FString::Printf(
						TEXT("%s\n%s"),
						*LayoutTraversalPlannerPrivate::BuildCandidateDebugLabel(Context, (*Domain)[CandidateIndex]),
						*FailureReason));
				}

				Domain->RemoveAtSwap(CandidateIndex, 1, EAllowShrinking::No);
			}
		}

		const int32 RemainingCandidateCount = Domain->Num();
		Diagnostics.EliminatedCandidateCount +=
			FMath::Max(0, InitialCandidateCount - RemainingCandidateCount);
		if (Domain->IsEmpty())
		{
			Diagnostics.bFailedConstraint = true;
			Diagnostics.FailedConstraintCell = RouteConstraint.Cell;
			Diagnostics.FailedConstraintEligibleCandidateCount =
				InitialCandidateCount;
			Diagnostics.FailedConstraintRequiredFaceCount =
				RouteConstraint.FaceRequirements.Num();
			Diagnostics.TightestRemainingDomainSize = 0;
			ELayoutRouteDomainFailureKind DominantFailureKind =
				ELayoutRouteDomainFailureKind::None;
			int32 DominantFailureCount = 0;
			for (int32 FailureKindIndex = 0;
				FailureKindIndex < CandidateFailureCountsByKind.Num();
				++FailureKindIndex)
			{
				const int32 FailureCount =
					CandidateFailureCountsByKind[FailureKindIndex];
				if (FailureCount > DominantFailureCount)
				{
					DominantFailureCount = FailureCount;
					DominantFailureKind =
						static_cast<ELayoutRouteDomainFailureKind>(
							FailureKindIndex);
				}
			}
			Diagnostics.FailedConstraintDominantFailureKind =
				DominantFailureKind;
			Diagnostics.FailedConstraintDominantFailureCount =
				DominantFailureCount;
			const FString ConstraintSummary = LayoutTraversalPlannerPrivate::BuildRouteConstraintRequirementSummary(RouteConstraint);

			FLayoutValidationMessage& Message = Context.Result.Messages.AddDefaulted_GetRef();
			Message.Severity = ELayoutValidationSeverity::Error;
			Message.Message = LayoutTraversalPlannerPrivate::BuildStructuredRouteDiagnostic(
				TEXT("Route-constrained planned cell started with eligible candidates, but none satisfied the required route faces."),
				{
					FString::Printf(TEXT("Cell: %s"), *RouteConstraint.Cell.ToString()),
					FString::Printf(TEXT("Intent: %s"), LayoutTraversalPlannerPrivate::ToDebugString(RouteConstraint.Intent)),
					FString::Printf(TEXT("Eligible candidates before route validation: %d"), InitialCandidateCount),
					FString::Printf(TEXT("Required faces: [%s]"), *ConstraintSummary),
					TEXT("Root cause: candidates supported the planned intent, but every eligible candidate failed route-specific face, occupancy, or internal traversal requirements."),
					TEXT("Fix: Use the candidate failures below to update the intended route face, occupancy policy, or internal traversal links on one of the eligible modules.")
				},
				CandidateFailureSummaries.IsEmpty() ? TArray<FString>{TEXT("<none>")} : CandidateFailureSummaries);
			Context.Result.FailureReason = Message.Message;

			FLayoutValidationMessage& ConstraintMessage = Context.Result.Messages.AddDefaulted_GetRef();
			ConstraintMessage.Severity = ELayoutValidationSeverity::Error;
			ConstraintMessage.Message = LayoutTraversalPlannerPrivate::BuildStructuredRouteDiagnostic(
				TEXT("Compiled route constraint removed every candidate."),
				{
					FString::Printf(TEXT("ConstraintId: %s"), *RouteConstraint.ConstraintId.ToString()),
					FString::Printf(TEXT("Cell: %s"), *RouteConstraint.Cell.ToString()),
					FString::Printf(TEXT("Required faces: [%s]"), *ConstraintSummary)
				});
			return false;
		}

		Diagnostics.TightestRemainingDomainSize =
			FMath::Min(
				Diagnostics.TightestRemainingDomainSize,
				RemainingCandidateCount);
		if (RemainingCandidateCount == 1)
		{
			++Diagnostics.SingleRemainingCandidateCellCount;
		}
		if (RemainingCandidateCount <= 4)
		{
			++Diagnostics.AtMostFourRemainingCandidateCellCount;
		}

		Domain->StableSort([&Context, Cell = RouteConstraint.Cell](const FSolveCandidate& Left, const FSolveCandidate& Right)
		{
			const int32 LeftScore = GetCandidateRequiredTraversalRouteScore(Context, Cell, Left);
			const int32 RightScore = GetCandidateRequiredTraversalRouteScore(Context, Cell, Right);
			if (LeftScore != RightScore)
			{
				return LeftScore > RightScore;
			}

			return false;
		});
	}

	if (Diagnostics.TightestRemainingDomainSize == MAX_int32)
	{
		Diagnostics.TightestRemainingDomainSize = 0;
	}
	return true;
}
