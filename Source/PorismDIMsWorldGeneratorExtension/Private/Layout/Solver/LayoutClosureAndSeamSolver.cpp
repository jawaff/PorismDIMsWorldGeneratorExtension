// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutProfileSolverInternal.h"

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"
#include "Layout/Solver/LayoutPlacementOccupancy.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Types/LayoutGameplayTags.h"

namespace LayoutClosureAndSeamSolverPrivate
{
	FString TagsToStableKey(const FGameplayTagContainer& Tags)
	{
		TArray<FGameplayTag> SortedTags;
		Tags.GetGameplayTagArray(SortedTags);
		SortedTags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
		{
			return Left.ToString() < Right.ToString();
		});

		TArray<FString> Parts;
		Parts.Reserve(SortedTags.Num());
		for (const FGameplayTag& Tag : SortedTags)
		{
			Parts.Add(Tag.ToString());
		}

		return FString::Join(Parts, TEXT("|"));
	}

	void GatherEffectiveProviderIntents(
		const FLayoutModuleSolveSnapshot& ModuleSnapshot,
		TArray<const FLayoutClosureProviderIntent*>& OutProviderIntents)
	{
		OutProviderIntents.Reset();
		for (const FLayoutClosureProviderIntent& ProviderIntent : ModuleSnapshot.ClosureProviderIntents)
		{
			OutProviderIntents.Add(&ProviderIntent);
		}
	}

	TArray<FLayoutDerivedSpanOffer> BuildWorldSpanOffersForYaw(const TArray<FLayoutDerivedSpanOffer>& DerivedSpanOffers, const int32 YawRotationSteps)
	{
		TArray<FLayoutDerivedSpanOffer> WorldSpanOffers;
		WorldSpanOffers.Reserve(DerivedSpanOffers.Num());
		for (const FLayoutDerivedSpanOffer& DerivedSpanOffer : DerivedSpanOffers)
		{
			FLayoutDerivedSpanOffer& WorldSpanOffer = WorldSpanOffers.AddDefaulted_GetRef();
			WorldSpanOffer = DerivedSpanOffer;
			WorldSpanOffer.FaceDirection = FLayoutDirectionUtils::RotateYaw(DerivedSpanOffer.FaceDirection, YawRotationSteps);
		}

		return WorldSpanOffers;
	}

	bool IsCurrentBatchSupportedClosureRequirement(
		const FLayoutClosureRequirement& ClosureRequirement,
		const FLayoutProfileSolveSnapshot& ProfileSnapshot,
		FString& OutUnsupportedReason)
	{
		if (ClosureRequirement.Zone != ELayoutPlacementZone::Perimeter)
		{
			OutUnsupportedReason = FString::Printf(
				TEXT("Closure '%s' targets zone %s, but the current runtime only validates perimeter-zone closure coverage."),
				*ClosureRequirement.ClosureId.ToString(),
				*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(ClosureRequirement.Zone)));
			return false;
		}

		if (ClosureRequirement.BoundsPolicy.Mode != ELayoutBoundsPolicyMode::SolvedFootprint)
		{
			OutUnsupportedReason = FString::Printf(
				TEXT("Closure '%s' uses ExplicitLocalBounds, but the current runtime only validates SolvedFootprint-based closure coverage."),
				*ClosureRequirement.ClosureId.ToString());
			return false;
		}

		if (ClosureRequirement.BoundsPolicy.InsetCells != 0)
		{
			OutUnsupportedReason = FString::Printf(
				TEXT("Closure '%s' uses InsetCells=%d, but the current runtime only validates perimeter-edge closure coverage with InsetCells=0."),
				*ClosureRequirement.ClosureId.ToString(),
				ClosureRequirement.BoundsPolicy.InsetCells);
			return false;
		}

		if (ClosureRequirement.BoundsPolicy.MinLevel < 0
			|| ClosureRequirement.BoundsPolicy.MaxLevel >= ProfileSnapshot.LevelCount
			|| ClosureRequirement.BoundsPolicy.MaxLevel < ClosureRequirement.BoundsPolicy.MinLevel)
		{
			OutUnsupportedReason = FString::Printf(
				TEXT("Closure '%s' cites an invalid level range %d..%d for LevelCount=%d."),
				*ClosureRequirement.ClosureId.ToString(),
				ClosureRequirement.BoundsPolicy.MinLevel,
				ClosureRequirement.BoundsPolicy.MaxLevel,
				ProfileSnapshot.LevelCount);
			return false;
		}

		return true;
	}

	void AddCompiledClosureSegment(
		const FLayoutClosureRequirement& ClosureRequirement,
		const FIntVector& Cell,
		const ELayoutFaceDirection FaceDirection,
		TArray<FLayoutClosureCoverageSegmentRecord>& OutSegments)
	{
		FLayoutClosureCoverageSegmentRecord& Segment = OutSegments.AddDefaulted_GetRef();
		Segment.ClosureId = ClosureRequirement.ClosureId;
		Segment.BoundaryZone = ClosureRequirement.Zone;
		Segment.Cell = Cell;
		Segment.FaceDirection = FaceDirection;
		Segment.MinThicknessCells = ClosureRequirement.MinThicknessCells;
	}

	void BuildSolvedFootprintClosureSegments(
		const FLayoutClosureRequirement& ClosureRequirement,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const TMap<FIntVector, ELayoutCellIntent>& PlannedCellIntents,
		TArray<FLayoutClosureCoverageSegmentRecord>& OutSegments)
	{
		const int32 MinLevel = ClosureRequirement.BoundsPolicy.MinLevel;
		const int32 MaxLevel = ClosureRequirement.BoundsPolicy.MaxLevel;
		static constexpr ELayoutFaceDirection HorizontalDirections[] =
		{
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegY,
			ELayoutFaceDirection::PosY
		};

		for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
		{
			if (PlannedCell.bIsBridgeCell)
			{
				continue;
			}

			const FIntVector& Cell = PlannedCell.Cell;
			const int32 ModuleLevel = PlannedCell.ModuleLevelIndex != INDEX_NONE
				? PlannedCell.ModuleLevelIndex
				: Cell.Z;
			if (ModuleLevel < MinLevel || ModuleLevel > MaxLevel)
			{
				continue;
			}

			for (const ELayoutFaceDirection FaceDirection : HorizontalDirections)
			{
				const FIntVector NeighborCell = Cell + FLayoutDirectionUtils::ToCellDelta(FaceDirection);
				if (NeighborCell.Z != Cell.Z || PlannedCellIntents.Contains(NeighborCell))
				{
					continue;
				}

				AddCompiledClosureSegment(ClosureRequirement, Cell, FaceDirection, OutSegments);
			}
		}
	}

	struct FCompiledClosureProviderSpan
	{
		FLayoutId SpanOfferId;
		FName ClosureId;
		FIntVector Cell = FIntVector::ZeroValue;
		ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;
		int32 ThicknessCells = 1;
		bool bSealsBoundary = true;
	};

	struct FCompiledSeamProviderSpan
	{
		FLayoutId ProviderId;
		FLayoutId ModuleSnapshotId;
		FIntVector PlacementRootCell = FIntVector::ZeroValue;
		int32 YawRotationSteps = 0;
		FGameplayTag InterfaceFamily;
		ELayoutSeamJunctionUsage JunctionUsage = ELayoutSeamJunctionUsage::Both;
		FIntVector Cell = FIntVector::ZeroValue;
		ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;
		bool bCanOwnSeam = true;
		bool bCanAcceptSeam = false;
		int32 ThicknessCells = 1;
		bool bSealsBoundary = true;
	};

	void BuildPlannedCellIntentMap(
		const TArray<FLayoutPlannedCell>& PlannedCells,
		TMap<FIntVector, ELayoutCellIntent>& OutPlannedCellIntents)
	{
		OutPlannedCellIntents.Reset();
		for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
		{
			OutPlannedCellIntents.Add(PlannedCell.Cell, PlannedCell.Intent);
		}
	}

	void BuildClosureProviderSpansFromContext(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		TArray<FCompiledClosureProviderSpan>& OutProviderSpans)
	{
		OutProviderSpans.Reset();
		for (const TPair<FIntVector, LayoutProfileSolverInternal::FSolveContext::FSolvePlacement>& PlacementPair : Context.Placements)
		{
			const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& Placement = PlacementPair.Value;
			if (!LayoutProfileSolverInternal::FSolveContext::IsOccupiedPlacement(Placement)
				|| !Placement.bBundleRoot
				|| !Context.Variants.IsValidIndex(Placement.VariantIndex))
			{
				continue;
			}

			const LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant = Context.Variants[Placement.VariantIndex];
			if (!Context.ModuleSnapshots.IsValidIndex(Variant.ModuleSnapshotIndex))
			{
				continue;
			}

			const FLayoutModuleSolveSnapshot& ModuleSnapshot = Context.ModuleSnapshots[Variant.ModuleSnapshotIndex];
			const auto ProjectSpanCell = [&Placement, &ModuleSnapshot](const FLayoutDerivedSpanOffer& SpanOffer)
			{
				return LayoutPlacementOccupancy::ProjectLocalCellToWorld(
					Placement.BundleRootCell,
					SpanOffer.LocalCell,
					ModuleSnapshot.BoundsCells,
					Placement.YawRotationSteps);
			};
			if (Variant.ClosureProviderIntents.IsEmpty())
			{
				for (const FLayoutDerivedSpanOffer& SpanOffer : Variant.WorldSpanOffers)
				{
					FCompiledClosureProviderSpan& ProviderSpan = OutProviderSpans.AddDefaulted_GetRef();
					ProviderSpan.SpanOfferId = SpanOffer.SpanOfferId;
					ProviderSpan.ClosureId = SpanOffer.ClosureId;
					ProviderSpan.Cell = ProjectSpanCell(SpanOffer);
					ProviderSpan.FaceDirection = SpanOffer.FaceDirection;
					ProviderSpan.ThicknessCells = SpanOffer.ThicknessCells;
					ProviderSpan.bSealsBoundary = SpanOffer.bSealsBoundary;
				}
				continue;
			}

			for (const FLayoutClosureProviderIntent& ProviderIntent : Variant.ClosureProviderIntents)
			{
				if (ProviderIntent.Zone != ELayoutPlacementZone::Perimeter)
				{
					continue;
				}

				for (const FLayoutDerivedSpanOffer& SpanOffer : Variant.WorldSpanOffers)
				{
					if (!SpanOffer.ClosureId.IsNone()
						&& !ProviderIntent.ClosureId.IsNone()
						&& SpanOffer.ClosureId != ProviderIntent.ClosureId)
					{
						continue;
					}

					FCompiledClosureProviderSpan& ProviderSpan = OutProviderSpans.AddDefaulted_GetRef();
					ProviderSpan.SpanOfferId = FLayoutId(*FString::Printf(
						TEXT("%s.%s"),
						*ProviderIntent.ProviderIntentId.ToString(),
						*SpanOffer.SpanOfferId.ToString()));
					ProviderSpan.ClosureId = !SpanOffer.ClosureId.IsNone() ? SpanOffer.ClosureId : ProviderIntent.ClosureId;
					ProviderSpan.Cell = ProjectSpanCell(SpanOffer);
					ProviderSpan.FaceDirection = SpanOffer.FaceDirection;
					ProviderSpan.ThicknessCells = SpanOffer.ThicknessCells;
					ProviderSpan.bSealsBoundary = SpanOffer.bSealsBoundary;
				}
			}
		}
	}

	bool TryCoverClosureSegmentWithProviders(
		const TArray<FCompiledClosureProviderSpan>& ProviderSpans,
		FLayoutClosureCoverageSegmentRecord& Segment)
	{
		for (const FCompiledClosureProviderSpan& ProviderSpan : ProviderSpans)
		{
			if (!ProviderSpan.bSealsBoundary || ProviderSpan.FaceDirection != Segment.FaceDirection)
			{
				continue;
			}

			if (!ProviderSpan.ClosureId.IsNone() && ProviderSpan.ClosureId != Segment.ClosureId)
			{
				continue;
			}

			if (ProviderSpan.Cell != Segment.Cell)
			{
				continue;
			}

			if (ProviderSpan.ThicknessCells < Segment.MinThicknessCells)
			{
				continue;
			}

			Segment.bCovered = true;
			Segment.ProviderId = ProviderSpan.SpanOfferId;
			return true;
		}

		return false;
	}

	int32 GetClosureSegmentPrimaryAxis(const FLayoutClosureCoverageSegmentRecord& Segment)
	{
		switch (Segment.FaceDirection)
		{
		case ELayoutFaceDirection::NegY:
		case ELayoutFaceDirection::PosY:
			return Segment.Cell.X;
		case ELayoutFaceDirection::NegX:
		case ELayoutFaceDirection::PosX:
			return Segment.Cell.Y;
		default:
			return 0;
		}
	}

	int32 GetClosureSegmentFixedAxis(const FLayoutClosureCoverageSegmentRecord& Segment)
	{
		switch (Segment.FaceDirection)
		{
		case ELayoutFaceDirection::NegY:
		case ELayoutFaceDirection::PosY:
			return Segment.Cell.Y;
		case ELayoutFaceDirection::NegX:
		case ELayoutFaceDirection::PosX:
			return Segment.Cell.X;
		default:
			return 0;
		}
	}

	bool AreClosureSegmentsContiguousInRun(
		const FLayoutClosureCoverageSegmentRecord& PreviousSegment,
		const FLayoutClosureCoverageSegmentRecord& NextSegment)
	{
		return PreviousSegment.ClosureId == NextSegment.ClosureId
			&& PreviousSegment.BoundaryZone == NextSegment.BoundaryZone
			&& PreviousSegment.FaceDirection == NextSegment.FaceDirection
			&& PreviousSegment.Cell.Z == NextSegment.Cell.Z
			&& PreviousSegment.MinThicknessCells == NextSegment.MinThicknessCells
			&& PreviousSegment.bCovered == NextSegment.bCovered
			&& PreviousSegment.ProviderId == NextSegment.ProviderId
			&& GetClosureSegmentFixedAxis(PreviousSegment) == GetClosureSegmentFixedAxis(NextSegment)
			&& GetClosureSegmentPrimaryAxis(NextSegment) == GetClosureSegmentPrimaryAxis(PreviousSegment) + 1;
	}

	void BuildClosureRunsFromSegments(
		const TArray<FLayoutClosureCoverageSegmentRecord>& Segments,
		TArray<FLayoutClosureRunRecord>& OutRuns)
	{
		OutRuns.Reset();
		if (Segments.IsEmpty())
		{
			return;
		}

		TArray<const FLayoutClosureCoverageSegmentRecord*> SortedSegments;
		SortedSegments.Reserve(Segments.Num());
		for (const FLayoutClosureCoverageSegmentRecord& Segment : Segments)
		{
			SortedSegments.Add(&Segment);
		}

		SortedSegments.Sort([](const FLayoutClosureCoverageSegmentRecord& Left, const FLayoutClosureCoverageSegmentRecord& Right)
		{
			if (Left.ClosureId != Right.ClosureId)
			{
				return Left.ClosureId.LexicalLess(Right.ClosureId);
			}

			if (Left.BoundaryZone != Right.BoundaryZone)
			{
				return static_cast<int32>(Left.BoundaryZone) < static_cast<int32>(Right.BoundaryZone);
			}

			if (Left.Cell.Z != Right.Cell.Z)
			{
				return Left.Cell.Z < Right.Cell.Z;
			}

			if (Left.FaceDirection != Right.FaceDirection)
			{
				return static_cast<int32>(Left.FaceDirection) < static_cast<int32>(Right.FaceDirection);
			}

			const int32 LeftFixedAxis = GetClosureSegmentFixedAxis(Left);
			const int32 RightFixedAxis = GetClosureSegmentFixedAxis(Right);
			if (LeftFixedAxis != RightFixedAxis)
			{
				return LeftFixedAxis < RightFixedAxis;
			}

			const int32 LeftPrimaryAxis = GetClosureSegmentPrimaryAxis(Left);
			const int32 RightPrimaryAxis = GetClosureSegmentPrimaryAxis(Right);
			if (LeftPrimaryAxis != RightPrimaryAxis)
			{
				return LeftPrimaryAxis < RightPrimaryAxis;
			}

			if (Left.bCovered != Right.bCovered)
			{
				return Left.bCovered && !Right.bCovered;
			}

			return Left.ProviderId.LexicalLess(Right.ProviderId);
		});

		const FLayoutClosureCoverageSegmentRecord* CurrentSegment = SortedSegments[0];
		FLayoutClosureRunRecord* CurrentRun = &OutRuns.AddDefaulted_GetRef();
		CurrentRun->ClosureId = CurrentSegment->ClosureId;
		CurrentRun->BoundaryZone = CurrentSegment->BoundaryZone;
		CurrentRun->FaceDirection = CurrentSegment->FaceDirection;
		CurrentRun->StartCell = CurrentSegment->Cell;
		CurrentRun->EndCell = CurrentSegment->Cell;
		CurrentRun->MinThicknessCells = CurrentSegment->MinThicknessCells;
		CurrentRun->bCovered = CurrentSegment->bCovered;
		CurrentRun->ProviderId = CurrentSegment->ProviderId;
		CurrentRun->SegmentCount = 1;

		for (int32 SegmentIndex = 1; SegmentIndex < SortedSegments.Num(); ++SegmentIndex)
		{
			const FLayoutClosureCoverageSegmentRecord* NextSegment = SortedSegments[SegmentIndex];
			if (AreClosureSegmentsContiguousInRun(*CurrentSegment, *NextSegment))
			{
				CurrentRun->EndCell = NextSegment->Cell;
				++CurrentRun->SegmentCount;
			}
			else
			{
				CurrentRun = &OutRuns.AddDefaulted_GetRef();
				CurrentRun->ClosureId = NextSegment->ClosureId;
				CurrentRun->BoundaryZone = NextSegment->BoundaryZone;
				CurrentRun->FaceDirection = NextSegment->FaceDirection;
				CurrentRun->StartCell = NextSegment->Cell;
				CurrentRun->EndCell = NextSegment->Cell;
				CurrentRun->MinThicknessCells = NextSegment->MinThicknessCells;
				CurrentRun->bCovered = NextSegment->bCovered;
				CurrentRun->ProviderId = NextSegment->ProviderId;
				CurrentRun->SegmentCount = 1;
			}

			CurrentSegment = NextSegment;
		}
	}

	bool EvaluateClosureCoverage(
		const TArray<FLayoutClosureRequirement>& ClosureRequirements,
		const FLayoutProfileSolveSnapshot& ProfileSnapshot,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const TMap<FIntVector, ELayoutCellIntent>& PlannedCellIntents,
		const TArray<FCompiledClosureProviderSpan>& ProviderSpans,
		TArray<FLayoutClosureCoverageRecord>& OutCoverage,
		TArray<FLayoutClosureCoverageSegmentRecord>& OutSegments,
		TArray<FLayoutClosureRunRecord>& OutRuns,
		TArray<FLayoutValidationMessage>& OutMessages,
		FString& OutFailureReason)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_ClosureEvaluation, STAT_PorismLayout_Closure);
		OutCoverage.Reset();
		OutSegments.Reset();
		OutRuns.Reset();

		bool bAllClosuresSatisfied = true;
		for (const FLayoutClosureRequirement& ClosureRequirement : ClosureRequirements)
		{
			FLayoutClosureCoverageRecord& CoverageRecord = OutCoverage.AddDefaulted_GetRef();
			CoverageRecord.ClosureId = ClosureRequirement.ClosureId;
			CoverageRecord.BoundaryZone = ClosureRequirement.Zone;
			FString UnsupportedReason;
			if (!IsCurrentBatchSupportedClosureRequirement(ClosureRequirement, ProfileSnapshot, UnsupportedReason))
			{
				FLayoutValidationMessage& Message = OutMessages.AddDefaulted_GetRef();
				Message.Severity = ELayoutValidationSeverity::Error;
				Message.Message = UnsupportedReason;
				bAllClosuresSatisfied = false;
				if (OutFailureReason.IsEmpty())
				{
					OutFailureReason = UnsupportedReason;
				}
				continue;
			}

			const int32 SegmentStartIndex = OutSegments.Num();
			BuildSolvedFootprintClosureSegments(ClosureRequirement, PlannedCells, PlannedCellIntents, OutSegments);
			CoverageRecord.RequiredSegmentCount = OutSegments.Num() - SegmentStartIndex;
			for (int32 SegmentIndex = SegmentStartIndex; SegmentIndex < OutSegments.Num(); ++SegmentIndex)
			{
				FLayoutClosureCoverageSegmentRecord& Segment = OutSegments[SegmentIndex];
				if (TryCoverClosureSegmentWithProviders(ProviderSpans, Segment))
				{
					++CoverageRecord.CoveredSegmentCount;
				}
			}

			CoverageRecord.bSatisfied = CoverageRecord.RequiredSegmentCount > 0
				&& CoverageRecord.CoveredSegmentCount == CoverageRecord.RequiredSegmentCount;
			if (!CoverageRecord.bSatisfied)
			{
				const FString MessageText = FString::Printf(
					TEXT("Closure '%s' is not fully covered. Covered %d of %d required boundary segments."),
					*ClosureRequirement.ClosureId.ToString(),
					CoverageRecord.CoveredSegmentCount,
					CoverageRecord.RequiredSegmentCount);
				FLayoutValidationMessage& Message = OutMessages.AddDefaulted_GetRef();
				Message.Severity = ELayoutValidationSeverity::Error;
				Message.Message = MessageText;
				bAllClosuresSatisfied = false;
				if (OutFailureReason.IsEmpty())
				{
					OutFailureReason = MessageText;
				}
			}
		}

		BuildClosureRunsFromSegments(OutSegments, OutRuns);
		return bAllClosuresSatisfied;
	}

	bool BuildSeamProviderSpansFromSolveResult(
		const FLayoutSolveResult& SolveResult,
		const FLayoutModuleCatalog& ModuleCatalog,
		TArray<FCompiledSeamProviderSpan>& OutProviderSpans,
		FString& OutFailureReason)
	{
		FLayoutRegionSolveRequest SyntheticRequest;
		SyntheticRequest.ModuleCatalog = ModuleCatalog;

		for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
		{
			if (!LayoutProfileSolverInternal::IsOccupiedPlacedModule(Placement))
			{
				continue;
			}

			const FLayoutModuleSolveSnapshot* ModuleSnapshot = LayoutProfileSolverInternal::FindModuleSnapshotForPlacedModule(SyntheticRequest, Placement);
			if (ModuleSnapshot == nullptr)
			{
				const FString PlacementSourceName = Placement.Module != nullptr
					? GetNameSafe(Placement.Module.Get())
					: GetNameSafe(Placement.CompositeModule.Get());
				OutFailureReason = FString::Printf(
					TEXT("Scheduled seam audit could not find a snapshot contract for placed bundle '%s'."),
					*PlacementSourceName);
				return false;
			}

			const TArray<FLayoutDerivedSpanOffer> WorldSpanOffers = BuildWorldSpanOffersForYaw(ModuleSnapshot->DerivedSpanOffers, Placement.YawRotationSteps);
			const auto ProjectSpanCell = [&Placement, ModuleSnapshot](const FLayoutDerivedSpanOffer& SpanOffer)
			{
				return LayoutPlacementOccupancy::ProjectLocalCellToWorld(
					Placement.Cell,
					SpanOffer.LocalCell,
					ModuleSnapshot->BoundsCells,
					Placement.YawRotationSteps);
			};
			for (const FLayoutSeamProviderIntent& SeamIntent : ModuleSnapshot->SeamProviderIntents)
			{
				if (!SeamIntent.InterfaceFamily.IsValid())
				{
					continue;
				}

				for (const FLayoutDerivedSpanOffer& SpanOffer : WorldSpanOffers)
				{
					FCompiledSeamProviderSpan& ProviderSpan = OutProviderSpans.AddDefaulted_GetRef();
					ProviderSpan.ProviderId = FLayoutId(*FString::Printf(
						TEXT("%s.%s"),
						*SeamIntent.SeamIntentId.ToString(),
						*SpanOffer.SpanOfferId.ToString()));
					ProviderSpan.ModuleSnapshotId = ModuleSnapshot->SnapshotId;
					ProviderSpan.PlacementRootCell = Placement.Cell;
					ProviderSpan.YawRotationSteps = Placement.YawRotationSteps;
					ProviderSpan.InterfaceFamily = SeamIntent.InterfaceFamily;
					ProviderSpan.JunctionUsage = SeamIntent.JunctionUsage;
					ProviderSpan.Cell = ProjectSpanCell(SpanOffer);
					ProviderSpan.FaceDirection = SpanOffer.FaceDirection;
					ProviderSpan.bCanOwnSeam = SeamIntent.bCanOwnSeam;
					ProviderSpan.bCanAcceptSeam = SeamIntent.bCanAcceptSeam;
					ProviderSpan.ThicknessCells = SpanOffer.ThicknessCells;
					ProviderSpan.bSealsBoundary = SpanOffer.bSealsBoundary;
				}
			}
		}

		return true;
	}

	bool BuildClosureProviderSpansFromSolveResult(
		const FLayoutSolveResult& SolveResult,
		const FLayoutModuleCatalog& ModuleCatalog,
		const FLayoutProfileSolveSnapshot& ProfileSnapshot,
		const bool bRequireAuthoredProviderIntent,
		TArray<FCompiledClosureProviderSpan>& OutProviderSpans,
		FString& OutFailureReason)
	{
		FLayoutRegionSolveRequest SyntheticRequest;
		SyntheticRequest.ModuleCatalog = ModuleCatalog;

		for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
		{
			if (!LayoutProfileSolverInternal::IsOccupiedPlacedModule(Placement))
			{
				continue;
			}

			const FLayoutModuleSolveSnapshot* ModuleSnapshot = LayoutProfileSolverInternal::FindModuleSnapshotForPlacedModule(SyntheticRequest, Placement);
			if (ModuleSnapshot == nullptr)
			{
				const FString PlacementSourceName = Placement.Module != nullptr
					? GetNameSafe(Placement.Module.Get())
					: GetNameSafe(Placement.CompositeModule.Get());
				OutFailureReason = FString::Printf(
					TEXT("Scheduled closure audit could not find a snapshot contract for placed bundle '%s'."),
					*PlacementSourceName);
				return false;
			}

			const TArray<FLayoutDerivedSpanOffer> WorldSpanOffers = BuildWorldSpanOffersForYaw(ModuleSnapshot->DerivedSpanOffers, Placement.YawRotationSteps);
			const auto ProjectSpanCell = [&Placement, ModuleSnapshot](const FLayoutDerivedSpanOffer& SpanOffer)
			{
				return LayoutPlacementOccupancy::ProjectLocalCellToWorld(
					Placement.Cell,
					SpanOffer.LocalCell,
					ModuleSnapshot->BoundsCells,
					Placement.YawRotationSteps);
			};
			if (!bRequireAuthoredProviderIntent)
			{
				for (const FLayoutDerivedSpanOffer& SpanOffer : WorldSpanOffers)
				{
					FCompiledClosureProviderSpan& ProviderSpan = OutProviderSpans.AddDefaulted_GetRef();
					ProviderSpan.SpanOfferId = SpanOffer.SpanOfferId;
					ProviderSpan.ClosureId = SpanOffer.ClosureId;
					ProviderSpan.Cell = ProjectSpanCell(SpanOffer);
					ProviderSpan.FaceDirection = SpanOffer.FaceDirection;
					ProviderSpan.ThicknessCells = SpanOffer.ThicknessCells;
					ProviderSpan.bSealsBoundary = SpanOffer.bSealsBoundary;
				}
				continue;
			}

			TArray<const FLayoutClosureProviderIntent*> EffectiveProviderIntents;
			GatherEffectiveProviderIntents(*ModuleSnapshot, EffectiveProviderIntents);
			for (const FLayoutClosureProviderIntent* ProviderIntentPtr : EffectiveProviderIntents)
			{
				const FLayoutClosureProviderIntent& ProviderIntent = *ProviderIntentPtr;
				if (ProviderIntent.Zone != ELayoutPlacementZone::Perimeter)
				{
					continue;
				}

				for (const FLayoutDerivedSpanOffer& SpanOffer : WorldSpanOffers)
				{
					if (!SpanOffer.ClosureId.IsNone()
						&& !ProviderIntent.ClosureId.IsNone()
						&& SpanOffer.ClosureId != ProviderIntent.ClosureId)
					{
						continue;
					}

					FCompiledClosureProviderSpan& ProviderSpan = OutProviderSpans.AddDefaulted_GetRef();
					ProviderSpan.SpanOfferId = FLayoutId(*FString::Printf(
						TEXT("%s.%s"),
						*ProviderIntent.ProviderIntentId.ToString(),
						*SpanOffer.SpanOfferId.ToString()));
					ProviderSpan.ClosureId = !SpanOffer.ClosureId.IsNone() ? SpanOffer.ClosureId : ProviderIntent.ClosureId;
					ProviderSpan.Cell = ProjectSpanCell(SpanOffer);
					ProviderSpan.FaceDirection = SpanOffer.FaceDirection;
					ProviderSpan.ThicknessCells = SpanOffer.ThicknessCells;
					ProviderSpan.bSealsBoundary = SpanOffer.bSealsBoundary;
				}
			}
		}

		return true;
	}

	bool AppendPassiveSeamClosureProviderSpans(
		const FString& RegionPath,
		const TArray<FLayoutPartitionSeamRecord>& PartitionSeams,
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		const TMap<FString, int32>& RegionResultIndexByPath,
		const FLayoutRegionSolveScheduleResult& ScheduleResult,
		TArray<FCompiledClosureProviderSpan>& OutProviderSpans,
		FString& OutFailureReason)
	{
		TMap<FString, TArray<FCompiledSeamProviderSpan>> SeamProviderSpansByOwner;
		for (const FLayoutPartitionSeamRecord& SeamRecord : PartitionSeams)
		{
			if (!SeamRecord.bCountsTowardClosure || SeamRecord.PassiveRegionDebugPath != RegionPath)
			{
				continue;
			}

			TArray<FCompiledSeamProviderSpan>* ExistingProviderSpans = SeamProviderSpansByOwner.Find(SeamRecord.OwnerRegionDebugPath);
			if (ExistingProviderSpans == nullptr)
			{
				const FLayoutRegionSolveRequest* const* OwnerRequestPtr = RequestsByPath.Find(SeamRecord.OwnerRegionDebugPath);
				const int32* OwnerResultIndex = RegionResultIndexByPath.Find(SeamRecord.OwnerRegionDebugPath);
				if (OwnerRequestPtr == nullptr || *OwnerRequestPtr == nullptr || OwnerResultIndex == nullptr || !ScheduleResult.RegionResults.IsValidIndex(*OwnerResultIndex))
				{
					OutFailureReason = FString::Printf(
						TEXT("Scheduled seam audit could not find owner region '%s' for seam '%s'."),
						*SeamRecord.OwnerRegionDebugPath,
						*SeamRecord.SeamId.ToString());
					return false;
				}

				TArray<FCompiledSeamProviderSpan>& OwnerProviderSpans = SeamProviderSpansByOwner.Add(SeamRecord.OwnerRegionDebugPath);
				if (!BuildSeamProviderSpansFromSolveResult(
					ScheduleResult.RegionResults[*OwnerResultIndex].SolveResult,
					(*OwnerRequestPtr)->ModuleCatalog,
					OwnerProviderSpans,
					OutFailureReason))
				{
					return false;
				}
				ExistingProviderSpans = &OwnerProviderSpans;
			}

			const int32 StepCount = FMath::Max(1, SeamRecord.SegmentCount);
			const FIntVector OwnerDelta = StepCount > 1
				? FIntVector(
					FMath::Clamp(SeamRecord.OwnerEndCell.X - SeamRecord.OwnerStartCell.X, -1, 1),
					FMath::Clamp(SeamRecord.OwnerEndCell.Y - SeamRecord.OwnerStartCell.Y, -1, 1),
					FMath::Clamp(SeamRecord.OwnerEndCell.Z - SeamRecord.OwnerStartCell.Z, -1, 1))
				: FIntVector::ZeroValue;
			const FIntVector PassiveDelta = StepCount > 1
				? FIntVector(
					FMath::Clamp(SeamRecord.PassiveEndCell.X - SeamRecord.PassiveStartCell.X, -1, 1),
					FMath::Clamp(SeamRecord.PassiveEndCell.Y - SeamRecord.PassiveStartCell.Y, -1, 1),
					FMath::Clamp(SeamRecord.PassiveEndCell.Z - SeamRecord.PassiveStartCell.Z, -1, 1))
				: FIntVector::ZeroValue;

			for (int32 SegmentIndex = 0; SegmentIndex < StepCount; ++SegmentIndex)
			{
				const FIntVector OwnerCell = SeamRecord.OwnerStartCell + OwnerDelta * SegmentIndex;
				const FIntVector PassiveCell = SeamRecord.PassiveStartCell + PassiveDelta * SegmentIndex;

				const FCompiledSeamProviderSpan* MatchingProvider = ExistingProviderSpans->FindByPredicate(
					[&SeamRecord, &OwnerCell](const FCompiledSeamProviderSpan& ProviderSpan)
					{
						return ProviderSpan.bCanOwnSeam
							&& ProviderSpan.bSealsBoundary
							&& ProviderSpan.InterfaceFamily == SeamRecord.InterfaceFamily
							&& ProviderSpan.Cell == OwnerCell
							&& ProviderSpan.FaceDirection == SeamRecord.OwnerFaceDirection;
					});
				if (MatchingProvider == nullptr)
				{
					continue;
				}

				FCompiledClosureProviderSpan& ProviderSpan = OutProviderSpans.AddDefaulted_GetRef();
				ProviderSpan.SpanOfferId = MatchingProvider->ProviderId;
				ProviderSpan.ClosureId = NAME_None;
				ProviderSpan.Cell = PassiveCell;
				ProviderSpan.FaceDirection = SeamRecord.PassiveFaceDirection;
				ProviderSpan.ThicknessCells = MatchingProvider->ThicknessCells;
				ProviderSpan.bSealsBoundary = MatchingProvider->bSealsBoundary;
			}
		}

		return true;
	}

	void SortPlannedCellsDeterministically(TArray<FLayoutPlannedCell>& PlannedCells)
	{
		PlannedCells.Sort([](const FLayoutPlannedCell& Left, const FLayoutPlannedCell& Right)
		{
			if (Left.Cell.Z != Right.Cell.Z)
			{
				return Left.Cell.Z < Right.Cell.Z;
			}
			if (Left.Cell.Y != Right.Cell.Y)
			{
				return Left.Cell.Y < Right.Cell.Y;
			}
			return Left.Cell.X < Right.Cell.X;
		});
	}

	bool TryGetSharedOverlapFaceDirections(
		const FIntVector& RegionALocalCell,
		const FIntPoint& RegionAFootprintSize,
		const FIntVector& RegionBLocalCell,
		const FIntPoint& RegionBFootprintSize,
		const bool bAllowSameFaceOverlap,
		ELayoutFaceDirection& OutRegionAFaceDirection,
		ELayoutFaceDirection& OutRegionBFaceDirection)
	{
		if (bAllowSameFaceOverlap)
		{
			if (RegionALocalCell.X == RegionAFootprintSize.X - 1 && RegionBLocalCell.X == RegionBFootprintSize.X - 1)
			{
				OutRegionAFaceDirection = ELayoutFaceDirection::PosX;
				OutRegionBFaceDirection = ELayoutFaceDirection::PosX;
				return true;
			}

			if (RegionALocalCell.X == 0 && RegionBLocalCell.X == 0)
			{
				OutRegionAFaceDirection = ELayoutFaceDirection::NegX;
				OutRegionBFaceDirection = ELayoutFaceDirection::NegX;
				return true;
			}

			if (RegionALocalCell.Y == RegionAFootprintSize.Y - 1 && RegionBLocalCell.Y == RegionBFootprintSize.Y - 1)
			{
				OutRegionAFaceDirection = ELayoutFaceDirection::PosY;
				OutRegionBFaceDirection = ELayoutFaceDirection::PosY;
				return true;
			}

			if (RegionALocalCell.Y == 0 && RegionBLocalCell.Y == 0)
			{
				OutRegionAFaceDirection = ELayoutFaceDirection::NegY;
				OutRegionBFaceDirection = ELayoutFaceDirection::NegY;
				return true;
			}
		}

		if (RegionALocalCell.Y == RegionBLocalCell.Y)
		{
			if (RegionALocalCell.X == RegionAFootprintSize.X - 1 && RegionBLocalCell.X == 0)
			{
				OutRegionAFaceDirection = ELayoutFaceDirection::PosX;
				OutRegionBFaceDirection = ELayoutFaceDirection::NegX;
				return true;
			}

			if (RegionALocalCell.X == 0 && RegionBLocalCell.X == RegionBFootprintSize.X - 1)
			{
				OutRegionAFaceDirection = ELayoutFaceDirection::NegX;
				OutRegionBFaceDirection = ELayoutFaceDirection::PosX;
				return true;
			}
		}

		if (RegionALocalCell.X == RegionBLocalCell.X)
		{
			if (RegionALocalCell.Y == RegionAFootprintSize.Y - 1 && RegionBLocalCell.Y == 0)
			{
				OutRegionAFaceDirection = ELayoutFaceDirection::PosY;
				OutRegionBFaceDirection = ELayoutFaceDirection::NegY;
				return true;
			}

			if (RegionALocalCell.Y == 0 && RegionBLocalCell.Y == RegionBFootprintSize.Y - 1)
			{
				OutRegionAFaceDirection = ELayoutFaceDirection::NegY;
				OutRegionBFaceDirection = ELayoutFaceDirection::PosY;
				return true;
			}
		}

		return false;
	}

	void GatherDeferredClosureAuditRegionPaths(
		const FString& RootRegionPath,
		const TMap<FString, TArray<FString>>& ChildrenByParent,
		TArray<FString>& OutRegionPaths)
	{
		OutRegionPaths.Reset();
		OutRegionPaths.Add(RootRegionPath);

		if (const TArray<FString>* DirectChildren = ChildrenByParent.Find(RootRegionPath))
		{
			TArray<FString> SortedChildren = *DirectChildren;
			SortedChildren.Sort();
			OutRegionPaths.Append(SortedChildren);
		}
	}
}

void LayoutProfileSolverInternal::CompileHardClosureSegmentsForContext(
	const FSolveContext& Context,
	TArray<FLayoutClosureCoverageSegmentRecord>& OutSegments)
{
	OutSegments.Reset();
	for (const FLayoutClosureRequirement& ClosureRequirement : Context.ProfileSnapshot.ClosureRequirements)
	{
		FString UnsupportedReason;
		if (!LayoutClosureAndSeamSolverPrivate::IsCurrentBatchSupportedClosureRequirement(
			ClosureRequirement,
			Context.ProfileSnapshot,
			UnsupportedReason))
		{
			continue;
		}

		LayoutClosureAndSeamSolverPrivate::BuildSolvedFootprintClosureSegments(
			ClosureRequirement,
			Context.Result.PlannedCells,
			Context.PlannedCellIntents,
			OutSegments);
	}
}

bool LayoutProfileSolverInternal::DoesProjectedSpanOfferCoverClosureSegment(
	const FLayoutDerivedSpanOffer& WorldSpanOffer,
	const FIntVector& BundleRootCell,
	const FIntVector& BundleBoundsCells,
	const int32 YawRotationSteps,
	const FLayoutClosureCoverageSegmentRecord& Segment)
{
	return WorldSpanOffer.bSealsBoundary
		&& WorldSpanOffer.FaceDirection == Segment.FaceDirection
		&& (WorldSpanOffer.ClosureId.IsNone() || WorldSpanOffer.ClosureId == Segment.ClosureId)
		&& WorldSpanOffer.ThicknessCells >= Segment.MinThicknessCells
		&& LayoutPlacementOccupancy::ProjectLocalCellToWorld(
			BundleRootCell,
			WorldSpanOffer.LocalCell,
			BundleBoundsCells,
			YawRotationSteps) == Segment.Cell;
}

bool LayoutProfileSolverInternal::ValidateClosureCoverageForContext(FSolveContext& Context)
{
	TArray<LayoutClosureAndSeamSolverPrivate::FCompiledClosureProviderSpan> ProviderSpans;
	LayoutClosureAndSeamSolverPrivate::BuildClosureProviderSpansFromContext(Context, ProviderSpans);
	return LayoutClosureAndSeamSolverPrivate::EvaluateClosureCoverage(
		Context.ProfileSnapshot.ClosureRequirements,
		Context.ProfileSnapshot,
		Context.Result.PlannedCells,
		Context.PlannedCellIntents,
		ProviderSpans,
		Context.Result.ClosureCoverage,
		Context.Result.ClosureSegments,
		Context.Result.ClosureRuns,
		Context.Result.Messages,
		Context.Result.FailureReason);
}

void LayoutProfileSolverInternal::PopulateResidualUnoccupiedCells(FLayoutSolveResult& SolveResult)
{
	TSet<FIntVector> OccupiedCells;
	for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
	{
		TArray<FIntVector> OccupiedLocalCells = LayoutPlacementOccupancy::ResolveOccupiedLocalCells(Placement, nullptr);
		if (OccupiedLocalCells.IsEmpty())
		{
			OccupiedLocalCells.Add(FIntVector::ZeroValue);
		}
		const FIntVector Bounds = LayoutPlacementOccupancy::BuildOccupiedLocalCellBounds(OccupiedLocalCells);
		for (const FIntVector& LocalCell : OccupiedLocalCells)
		{
			OccupiedCells.Add(LayoutPlacementOccupancy::ProjectOccupiedLocalCellToWorld(Placement, LocalCell, Bounds));
		}
	}

	TMap<FIntVector, FLayoutResidualCellRecord> PreservedResidualByCell;
	for (const FLayoutResidualCellRecord& ExistingResidual : SolveResult.ResidualUnoccupiedCells)
	{
		if (!OccupiedCells.Contains(ExistingResidual.Cell)
			&& (ExistingResidual.Source != ELayoutResidualCellSource::UnoccupiedPlannedCell
				|| !ExistingResidual.SourceRegionDebugPath.IsEmpty()
				|| ExistingResidual.SourceContentEntryId != NAME_None
				|| ExistingResidual.RelatedDropDecisionId != NAME_None
				|| ExistingResidual.SourceSparsePlacementRuleId != NAME_None))
		{
			PreservedResidualByCell.FindOrAdd(ExistingResidual.Cell) = ExistingResidual;
		}
	}

	SolveResult.ResidualUnoccupiedCells.Reset();
	for (const FLayoutPlannedCell& PlannedCell : SolveResult.PlannedCells)
	{
		if (OccupiedCells.Contains(PlannedCell.Cell))
		{
			continue;
		}

		if (FLayoutResidualCellRecord* PreservedResidual = PreservedResidualByCell.Find(PlannedCell.Cell))
		{
			SolveResult.ResidualUnoccupiedCells.Add(*PreservedResidual);
			PreservedResidualByCell.Remove(PlannedCell.Cell);
			continue;
		}

		FLayoutResidualCellRecord& ResidualCell = SolveResult.ResidualUnoccupiedCells.AddDefaulted_GetRef();
		ResidualCell.Cell = PlannedCell.Cell;
		ResidualCell.Intent = PlannedCell.Intent;
		ResidualCell.ModuleLevelIndex = PlannedCell.ModuleLevelIndex;
		ResidualCell.PlacementZone = PlannedCell.PlacementZone;
	}

	for (const TPair<FIntVector, FLayoutResidualCellRecord>& PreservedPair : PreservedResidualByCell)
	{
		SolveResult.ResidualUnoccupiedCells.Add(PreservedPair.Value);
	}
	SolveResult.ResidualUnoccupiedCells.Sort([](const FLayoutResidualCellRecord& Left, const FLayoutResidualCellRecord& Right)
	{
		return Left.Cell.Z != Right.Cell.Z ? Left.Cell.Z < Right.Cell.Z
			: Left.Cell.Y != Right.Cell.Y ? Left.Cell.Y < Right.Cell.Y
			: Left.Cell.X < Right.Cell.X;
	});
}

bool LayoutProfileSolverInternal::BuildSchedulePartitionSeams(
	const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
	const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
	const TMap<FString, TArray<FString>>& ChildrenByParent,
	TArray<FLayoutPartitionSeamRecord>& OutSeams,
	TSet<FString>& OutPassiveRegionPaths,
	const bool bPlannedSeamsAreAuthoritative,
	FString& OutFailureReason)
{
	return BuildSchedulePartitionSeamsFromSharedParentPolicy(
		RequestsByPath,
		CapabilityEnvelopeByRegion,
		ChildrenByParent,
		OutSeams,
		OutPassiveRegionPaths,
		bPlannedSeamsAreAuthoritative,
		OutFailureReason);
}

bool LayoutProfileSolverInternal::BuildSchedulePartitionSeamsFromSharedParentPolicy(
	const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
	const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
	const TMap<FString, TArray<FString>>& ChildrenByParent,
	TArray<FLayoutPartitionSeamRecord>& OutSeams,
	TSet<FString>& OutPassiveRegionPaths,
	const bool bPlannedSeamsAreAuthoritative,
	FString& OutFailureReason)
{
	for (const FLayoutPartitionSeamRecord& ExistingSeam : OutSeams)
	{
		if (!ExistingSeam.PassiveRegionDebugPath.IsEmpty())
		{
			OutPassiveRegionPaths.Add(ExistingSeam.PassiveRegionDebugPath);
		}
	}

	TArray<FString> ParentPaths;
	ChildrenByParent.GetKeys(ParentPaths);
	ParentPaths.Sort();

	for (const FString& ParentPath : ParentPaths)
	{
		const TArray<FString>* Children = ChildrenByParent.Find(ParentPath);
		if (!AppendSchedulePartitionSeamsForParentFromSharedPairPolicy(
				ParentPath,
				RequestsByPath,
				CapabilityEnvelopeByRegion,
				Children != nullptr ? *Children : TArray<FString>(),
				OutSeams,
				OutPassiveRegionPaths,
				bPlannedSeamsAreAuthoritative,
				OutFailureReason))
		{
			return false;
		}
	}

	SortSchedulePartitionSeams(OutSeams);
	return true;
}

bool LayoutProfileSolverInternal::AppendSchedulePartitionSeamsForParentFromSharedPairPolicy(
	const FString& ParentPath,
	const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
	const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
	const TArray<FString>& Children,
	TArray<FLayoutPartitionSeamRecord>& InOutSeams,
	TSet<FString>& OutPassiveRegionPaths,
	const bool bPlannedSeamsAreAuthoritative,
	FString& OutFailureReason)
{
	TArray<FString> RelatedRegions = {ParentPath};
	TArray<FString> SortedChildren = Children;
	SortedChildren.Sort();
	RelatedRegions.Append(SortedChildren);

	for (int32 LeftIndex = 0; LeftIndex < RelatedRegions.Num(); ++LeftIndex)
	{
		for (int32 RightIndex = LeftIndex + 1; RightIndex < RelatedRegions.Num(); ++RightIndex)
		{
			if (!AppendSchedulePartitionSeamsForPair(
					ParentPath,
					RelatedRegions[LeftIndex],
					RelatedRegions[RightIndex],
					RequestsByPath,
					CapabilityEnvelopeByRegion,
					InOutSeams,
					OutPassiveRegionPaths,
					bPlannedSeamsAreAuthoritative,
					OutFailureReason))
			{
				return false;
			}
		}
	}

	return true;
}

bool LayoutProfileSolverInternal::AppendSchedulePartitionSeamsForParent(
	const FString& ParentPath,
	const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
	const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
	const TArray<FString>& Children,
	TArray<FLayoutPartitionSeamRecord>& InOutSeams,
	TSet<FString>& OutPassiveRegionPaths,
	const bool bPlannedSeamsAreAuthoritative,
	FString& OutFailureReason)
{
	return AppendSchedulePartitionSeamsForParentFromSharedPairPolicy(
		ParentPath,
		RequestsByPath,
		CapabilityEnvelopeByRegion,
		Children,
		InOutSeams,
		OutPassiveRegionPaths,
		bPlannedSeamsAreAuthoritative,
		OutFailureReason);
}

bool LayoutProfileSolverInternal::AppendSchedulePartitionSeamsForPair(
	const FString& ParentPath,
	const FString& LeftPath,
	const FString& RightPath,
	const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
	const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
	TArray<FLayoutPartitionSeamRecord>& InOutSeams,
	TSet<FString>& OutPassiveRegionPaths,
	const bool bPlannedSeamsAreAuthoritative,
	FString& OutFailureReason)
{
	return AppendSchedulePartitionSeamsForPairFromLeafStages(
		ParentPath,
		LeftPath,
		RightPath,
		RequestsByPath,
		CapabilityEnvelopeByRegion,
		InOutSeams,
		OutPassiveRegionPaths,
		bPlannedSeamsAreAuthoritative,
		OutFailureReason);
}

void LayoutProfileSolverInternal::SortSchedulePartitionSeams(
	TArray<FLayoutPartitionSeamRecord>& InOutSeams)
{
	InOutSeams.Sort([](const FLayoutPartitionSeamRecord& Left, const FLayoutPartitionSeamRecord& Right)
	{
		if (Left.ParentRegionDebugPath != Right.ParentRegionDebugPath)
		{
			return Left.ParentRegionDebugPath < Right.ParentRegionDebugPath;
		}
		if (Left.OwnerRegionDebugPath != Right.OwnerRegionDebugPath)
		{
			return Left.OwnerRegionDebugPath < Right.OwnerRegionDebugPath;
		}
		if (Left.PassiveRegionDebugPath != Right.PassiveRegionDebugPath)
		{
			return Left.PassiveRegionDebugPath < Right.PassiveRegionDebugPath;
		}
		if (Left.InterfaceFamily != Right.InterfaceFamily)
		{
			return Left.InterfaceFamily.ToString() < Right.InterfaceFamily.ToString();
		}
		return Left.SegmentCount < Right.SegmentCount;
	});
}

bool LayoutProfileSolverInternal::ValidateOwnedSeamJunctionRequirements(
	const TArray<FLayoutOwnedSeamJunctionRequirement>& JunctionRequirements,
	const TArray<FLayoutPartitionSeamRecord>& PartitionSeams,
	const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
	const TMap<FString, int32>& RegionResultIndexByPath,
	const FLayoutRegionSolveScheduleResult& ScheduleResult,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	TMap<FString, TArray<LayoutClosureAndSeamSolverPrivate::FCompiledSeamProviderSpan>> ProviderSpansByOwner;
	for (const FLayoutOwnedSeamJunctionRequirement& Requirement : JunctionRequirements)
	{
		const FLayoutRegionSolveRequest* const* OwnerRequestPtr = RequestsByPath.Find(Requirement.OwnerRegionDebugPath);
		const int32* OwnerResultIndex = RegionResultIndexByPath.Find(Requirement.OwnerRegionDebugPath);
		if (OwnerRequestPtr == nullptr || *OwnerRequestPtr == nullptr
			|| OwnerResultIndex == nullptr || !ScheduleResult.RegionResults.IsValidIndex(*OwnerResultIndex))
		{
			OutFailureReason = FString::Printf(
				TEXT("Owned seam junction '%s' could not resolve owner region '%s'."),
				*Requirement.JunctionRequirementId.ToString(),
				*Requirement.OwnerRegionDebugPath);
			return false;
		}

		const FLayoutPartitionSeamRecord* ContinuingSeam = PartitionSeams.FindByPredicate(
			[&Requirement](const FLayoutPartitionSeamRecord& Seam)
			{
				return Seam.SeamId == Requirement.ContinuingSeamId;
			});
		const FLayoutPartitionSeamRecord* BranchSeam = PartitionSeams.FindByPredicate(
			[&Requirement](const FLayoutPartitionSeamRecord& Seam)
			{
				return Seam.SeamId == Requirement.BranchSeamId;
			});
		if (ContinuingSeam == nullptr
			|| (!Requirement.BranchSeamId.IsNone() && BranchSeam == nullptr))
		{
			OutFailureReason = FString::Printf(
				TEXT("Owned seam junction '%s' references missing committed seam ids '%s' and '%s'."),
				*Requirement.JunctionRequirementId.ToString(),
				*Requirement.ContinuingSeamId.ToString(),
				*Requirement.BranchSeamId.ToString());
			return false;
		}

		TArray<LayoutClosureAndSeamSolverPrivate::FCompiledSeamProviderSpan>* ProviderSpans =
			ProviderSpansByOwner.Find(Requirement.OwnerRegionDebugPath);
		if (ProviderSpans == nullptr)
		{
			TArray<LayoutClosureAndSeamSolverPrivate::FCompiledSeamProviderSpan>& AddedSpans =
				ProviderSpansByOwner.Add(Requirement.OwnerRegionDebugPath);
			if (!LayoutClosureAndSeamSolverPrivate::BuildSeamProviderSpansFromSolveResult(
					ScheduleResult.RegionResults[*OwnerResultIndex].SolveResult,
					(*OwnerRequestPtr)->ModuleCatalog,
					AddedSpans,
					OutFailureReason))
			{
				return false;
			}
			ProviderSpans = &AddedSpans;
		}

		const FIntVector LocalJunctionCell = Requirement.JunctionCell
			- ScheduleResult.RegionResults[*OwnerResultIndex].RegionCellOffset;
		bool bRealizedByOnePlacement = false;
		for (const LayoutClosureAndSeamSolverPrivate::FCompiledSeamProviderSpan& ContinuingProvider : *ProviderSpans)
		{
			if (!ContinuingProvider.bCanOwnSeam
				|| !ContinuingProvider.bSealsBoundary
				|| ContinuingProvider.JunctionUsage == ELayoutSeamJunctionUsage::NonJunctionOnly
				|| ContinuingProvider.InterfaceFamily != Requirement.InterfaceFamily
				|| ContinuingProvider.Cell != LocalJunctionCell
				|| ContinuingProvider.FaceDirection != Requirement.ContinuingOwnerFaceDirection)
			{
				continue;
			}
			bRealizedByOnePlacement = ProviderSpans->ContainsByPredicate(
				[&](const LayoutClosureAndSeamSolverPrivate::FCompiledSeamProviderSpan& BranchProvider)
				{
					return BranchProvider.bCanOwnSeam
						&& BranchProvider.bSealsBoundary
						&& BranchProvider.JunctionUsage != ELayoutSeamJunctionUsage::NonJunctionOnly
						&& BranchProvider.InterfaceFamily == Requirement.InterfaceFamily
						&& BranchProvider.Cell == LocalJunctionCell
						&& BranchProvider.FaceDirection == Requirement.BranchOwnerFaceDirection
						&& BranchProvider.ModuleSnapshotId == ContinuingProvider.ModuleSnapshotId
						&& BranchProvider.PlacementRootCell == ContinuingProvider.PlacementRootCell
						&& BranchProvider.YawRotationSteps == ContinuingProvider.YawRotationSteps;
				});
			if (bRealizedByOnePlacement)
			{
				break;
			}
		}
		if (!bRealizedByOnePlacement)
		{
			OutFailureReason = FString::Printf(
				TEXT("Owned seam junction '%s' at %s was not realized by one owner placement supporting both %d and %d faces for interface '%s'."),
				*Requirement.JunctionRequirementId.ToString(),
				*Requirement.JunctionCell.ToString(),
				static_cast<int32>(Requirement.ContinuingOwnerFaceDirection),
				static_cast<int32>(Requirement.BranchOwnerFaceDirection),
				Requirement.InterfaceFamily.IsValid() ? *Requirement.InterfaceFamily.ToString() : TEXT("None"));
			return false;
		}
	}
	return true;
}

bool LayoutProfileSolverInternal::ReevaluateDeferredClosureCoverageForRegion(
	const FString& RegionPath,
	const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
	const TMap<FString, int32>& RegionResultIndexByPath,
	const TMap<FString, TArray<FString>>& ChildrenByParent,
	FLayoutRegionSolveScheduleResult& ScheduleResult,
	FString& OutFailureReason)
{
	const FLayoutRegionSolveRequest* const* ParentRequestPtr = RequestsByPath.Find(RegionPath);
	const int32* ParentResultIndex = RegionResultIndexByPath.Find(RegionPath);
	if (ParentRequestPtr == nullptr || *ParentRequestPtr == nullptr || ParentResultIndex == nullptr || !ScheduleResult.RegionResults.IsValidIndex(*ParentResultIndex))
	{
		OutFailureReason = FString::Printf(TEXT("Scheduled closure audit could not find parent region '%s'."), *RegionPath);
		return false;
	}

	const FLayoutRegionSolveRequest& ParentRequest = **ParentRequestPtr;
	if (ParentRequest.ProfileSnapshot.ClosureRequirements.IsEmpty())
	{
		return true;
	}

	TArray<FString> AuditRegionPaths;
	LayoutClosureAndSeamSolverPrivate::GatherDeferredClosureAuditRegionPaths(RegionPath, ChildrenByParent, AuditRegionPaths);

	TMap<FIntVector, ELayoutCellIntent> CombinedPlannedCellIntents;
	TArray<FLayoutPlannedCell> CombinedPlannedCells;
	TArray<LayoutClosureAndSeamSolverPrivate::FCompiledClosureProviderSpan> ProviderSpans;
	for (const FString& AuditRegionPath : AuditRegionPaths)
	{
		const FLayoutRegionSolveRequest* const* RequestPtr = RequestsByPath.Find(AuditRegionPath);
		const int32* ResultIndex = RegionResultIndexByPath.Find(AuditRegionPath);
		if (RequestPtr == nullptr || *RequestPtr == nullptr || ResultIndex == nullptr || !ScheduleResult.RegionResults.IsValidIndex(*ResultIndex))
		{
			OutFailureReason = FString::Printf(
				TEXT("Scheduled closure audit for '%s' could not find committed region '%s'."),
				*RegionPath,
				*AuditRegionPath);
			return false;
		}

		const FLayoutRegionSolveRequest& AuditRequest = **RequestPtr;
		const FLayoutSolveResult& AuditSolveResult = ScheduleResult.RegionResults[*ResultIndex].SolveResult;
		for (const FLayoutPlannedCell& PlannedCell : AuditSolveResult.PlannedCells)
		{
			if (!CombinedPlannedCellIntents.Contains(PlannedCell.Cell))
			{
				CombinedPlannedCellIntents.Add(PlannedCell.Cell, PlannedCell.Intent);
				CombinedPlannedCells.Add(PlannedCell);
			}
		}

		if (!LayoutClosureAndSeamSolverPrivate::BuildClosureProviderSpansFromSolveResult(
			AuditSolveResult,
			AuditRequest.ModuleCatalog,
			AuditRequest.ProfileSnapshot,
			AuditRegionPath != RegionPath,
			ProviderSpans,
			OutFailureReason))
		{
			return false;
		}
	}

	LayoutClosureAndSeamSolverPrivate::SortPlannedCellsDeterministically(CombinedPlannedCells);
	TArray<FLayoutClosureCoverageRecord> Coverage;
	TArray<FLayoutClosureCoverageSegmentRecord> Segments;
	TArray<FLayoutClosureRunRecord> Runs;
	TArray<FLayoutValidationMessage> Messages = ScheduleResult.RegionResults[*ParentResultIndex].SolveResult.Messages;
	FString FailureReason;
	if (!LayoutClosureAndSeamSolverPrivate::EvaluateClosureCoverage(
		ParentRequest.ProfileSnapshot.ClosureRequirements,
		ParentRequest.ProfileSnapshot,
		CombinedPlannedCells,
		CombinedPlannedCellIntents,
		ProviderSpans,
		Coverage,
		Segments,
		Runs,
		Messages,
		FailureReason))
	{
		OutFailureReason = FailureReason;
	}

	FLayoutSolveResult& ParentSolveResult = ScheduleResult.RegionResults[*ParentResultIndex].SolveResult;
	ParentSolveResult.ClosureCoverage = MoveTemp(Coverage);
	ParentSolveResult.ClosureSegments = MoveTemp(Segments);
	ParentSolveResult.ClosureRuns = MoveTemp(Runs);
	ParentSolveResult.Messages = MoveTemp(Messages);
	ParentSolveResult.FailureReason = FailureReason;
	return OutFailureReason.IsEmpty();
}

bool LayoutProfileSolverInternal::ReevaluatePassiveSeamClosureCoverageForRegion(
	const FString& PassiveRegionPath,
	const TArray<FLayoutPartitionSeamRecord>& PartitionSeams,
	const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
	const TMap<FString, int32>& RegionResultIndexByPath,
	FLayoutRegionSolveScheduleResult& ScheduleResult,
	FString& OutFailureReason)
{
	const FLayoutRegionSolveRequest* const* PassiveRequestPtr = RequestsByPath.Find(PassiveRegionPath);
	const int32* PassiveResultIndex = RegionResultIndexByPath.Find(PassiveRegionPath);
	if (PassiveRequestPtr == nullptr || *PassiveRequestPtr == nullptr || PassiveResultIndex == nullptr || !ScheduleResult.RegionResults.IsValidIndex(*PassiveResultIndex))
	{
		OutFailureReason = FString::Printf(TEXT("Scheduled seam audit could not find passive region '%s'."), *PassiveRegionPath);
		return false;
	}

	const FLayoutRegionSolveRequest& PassiveRequest = **PassiveRequestPtr;
	if (PassiveRequest.ProfileSnapshot.ClosureRequirements.IsEmpty())
	{
		return true;
	}

	TMap<FIntVector, ELayoutCellIntent> PassivePlannedCellIntents;
	const TArray<FLayoutPlannedCell>& PassivePlannedCells = ScheduleResult.RegionResults[*PassiveResultIndex].SolveResult.PlannedCells;
	LayoutClosureAndSeamSolverPrivate::BuildPlannedCellIntentMap(PassivePlannedCells, PassivePlannedCellIntents);

	TArray<LayoutClosureAndSeamSolverPrivate::FCompiledClosureProviderSpan> PassiveProviderSpans;
	if (!LayoutClosureAndSeamSolverPrivate::BuildClosureProviderSpansFromSolveResult(
		ScheduleResult.RegionResults[*PassiveResultIndex].SolveResult,
		PassiveRequest.ModuleCatalog,
		PassiveRequest.ProfileSnapshot,
		false,
		PassiveProviderSpans,
		OutFailureReason)
		|| !LayoutClosureAndSeamSolverPrivate::AppendPassiveSeamClosureProviderSpans(
			PassiveRegionPath,
			PartitionSeams,
			RequestsByPath,
			RegionResultIndexByPath,
			ScheduleResult,
			PassiveProviderSpans,
			OutFailureReason))
	{
		return false;
	}

	TArray<FLayoutClosureCoverageRecord> Coverage;
	TArray<FLayoutClosureCoverageSegmentRecord> Segments;
	TArray<FLayoutClosureRunRecord> Runs;
	TArray<FLayoutValidationMessage> Messages = ScheduleResult.RegionResults[*PassiveResultIndex].SolveResult.Messages;
	FString FailureReason;
	if (!LayoutClosureAndSeamSolverPrivate::EvaluateClosureCoverage(
		PassiveRequest.ProfileSnapshot.ClosureRequirements,
		PassiveRequest.ProfileSnapshot,
		PassivePlannedCells,
		PassivePlannedCellIntents,
		PassiveProviderSpans,
		Coverage,
		Segments,
		Runs,
		Messages,
		FailureReason))
	{
		OutFailureReason = FailureReason;
	}

	FLayoutSolveResult& PassiveSolveResult = ScheduleResult.RegionResults[*PassiveResultIndex].SolveResult;
	PassiveSolveResult.ClosureCoverage = MoveTemp(Coverage);
	PassiveSolveResult.ClosureSegments = MoveTemp(Segments);
	PassiveSolveResult.ClosureRuns = MoveTemp(Runs);
	PassiveSolveResult.Messages = MoveTemp(Messages);
	PassiveSolveResult.FailureReason = FailureReason;
	return OutFailureReason.IsEmpty();
}

#if WITH_AUTOMATION_TESTS
bool FLayoutProfileSolver::DebugTryGetClosureSharedOverlapFaceDirections(
	const FIntVector& FirstLocalCell,
	const FIntPoint& FirstFootprintSize,
	const FIntVector& SecondLocalCell,
	const FIntPoint& SecondFootprintSize,
	ELayoutFaceDirection& OutFirstFaceDirection,
	ELayoutFaceDirection& OutSecondFaceDirection)
{
	return LayoutClosureAndSeamSolverPrivate::TryGetSharedOverlapFaceDirections(
		FirstLocalCell,
		FirstFootprintSize,
		SecondLocalCell,
		SecondFootprintSize,
		false,
		OutFirstFaceDirection,
		OutSecondFaceDirection);
}
#endif
