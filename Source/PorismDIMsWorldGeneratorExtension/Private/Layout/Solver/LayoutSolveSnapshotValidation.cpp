// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutSolveSnapshotValidation.h"

namespace LayoutSolveSnapshotValidation
{
	namespace
	{
		FLayoutValidationAssertionRecord MakeSnapshotAssertionRecord(
			const FLayoutId AssertionId,
			const ELayoutValidationAssertionKind AssertionKind,
			const bool bPassed,
			const TArray<FLayoutId>& RelatedIds,
			const FString& FailureReason = FString())
		{
			FLayoutValidationAssertionRecord Assertion;
			Assertion.AssertionId = AssertionId;
			Assertion.AssertionKind = AssertionKind;
			Assertion.bPassed = bPassed;
			Assertion.RelatedIds = RelatedIds;
			Assertion.FailureReason = FailureReason;
			return Assertion;
		}

		bool AreEquivalentTagContainers(
			const FGameplayTagContainer& Left,
			const FGameplayTagContainer& Right)
		{
			return Left.HasAllExact(Right) && Right.HasAllExact(Left);
		}

		bool AreEquivalentFaceRules(
			const FLayoutFaceRule& Left,
			const FLayoutFaceRule& Right)
		{
			return Left.Direction == Right.Direction
				&& Left.ConnectionTag == Right.ConnectionTag
				&& AreEquivalentTagContainers(Left.AllowedConnectionTags, Right.AllowedConnectionTags)
				&& Left.OccupancyPolicy == Right.OccupancyPolicy
				&& AreEquivalentTagContainers(Left.ConnectedTraversalChannels, Right.ConnectedTraversalChannels)
				&& Left.BoundaryRequirement == Right.BoundaryRequirement
				&& Left.bRequireMatchingYawWithFilledNeighbor == Right.bRequireMatchingYawWithFilledNeighbor;
		}

		bool IsCellInsideBounds(
			const FIntVector& Cell,
			const FIntVector& BoundsCells)
		{
			return Cell.X >= 0
				&& Cell.Y >= 0
				&& Cell.Z >= 0
				&& Cell.X < BoundsCells.X
				&& Cell.Y < BoundsCells.Y
				&& Cell.Z < BoundsCells.Z;
		}

		bool IsCellOnOuterBoundsFace(
			const FIntVector& Cell,
			const FIntVector& BoundsCells,
			const ELayoutFaceDirection Direction)
		{
			switch (Direction)
			{
			case ELayoutFaceDirection::PosX:
				return Cell.X == BoundsCells.X - 1;
			case ELayoutFaceDirection::NegX:
				return Cell.X == 0;
			case ELayoutFaceDirection::PosY:
				return Cell.Y == BoundsCells.Y - 1;
			case ELayoutFaceDirection::NegY:
				return Cell.Y == 0;
			case ELayoutFaceDirection::PosZ:
				return Cell.Z == BoundsCells.Z - 1;
			case ELayoutFaceDirection::NegZ:
				return Cell.Z == 0;
			default:
				return false;
			}
		}
	}

	void ValidateModuleLocalShapeContracts(FLayoutModuleSolveSnapshot& Snapshot)
	{
		bool bHasAtLeastOneOccupiedCell = !Snapshot.OccupiedLocalCells.IsEmpty();
		bool bOccupiedCellSetIsUniqueAndInBounds = true;
		TSet<FIntVector> UniqueOccupiedCells;
		for (const FIntVector& OccupiedCell : Snapshot.OccupiedLocalCells)
		{
			if (!IsCellInsideBounds(OccupiedCell, Snapshot.BoundsCells)
				|| UniqueOccupiedCells.Contains(OccupiedCell))
			{
				bOccupiedCellSetIsUniqueAndInBounds = false;
			}
			UniqueOccupiedCells.Add(OccupiedCell);
		}

		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ModuleSnapshot.OccupiedLocalCellsPresent"),
			ELayoutValidationAssertionKind::SnapshotContractInitialized,
			bHasAtLeastOneOccupiedCell,
			{Snapshot.SnapshotId},
			bHasAtLeastOneOccupiedCell
				? FString()
				: FString::Printf(TEXT("Module '%s' does not provide any occupied local cells for placement-bundle compilation."), *Snapshot.DebugName.ToString())));

		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ModuleSnapshot.OccupiedLocalCellContract"),
			ELayoutValidationAssertionKind::SnapshotContractInitialized,
			bHasAtLeastOneOccupiedCell && bOccupiedCellSetIsUniqueAndInBounds,
			{Snapshot.SnapshotId},
			bHasAtLeastOneOccupiedCell && bOccupiedCellSetIsUniqueAndInBounds
				? FString()
				: FString::Printf(TEXT("Module '%s' must provide unique occupied local cells that all lie inside BoundsCells %s before placement-bundle compilation."), *Snapshot.DebugName.ToString(), *Snapshot.BoundsCells.ToString())));

		bool bFaceGridMatchesOccupiedCellSet =
			Snapshot.GeneratedLocalCellFaceRules.Num() == Snapshot.OccupiedLocalCells.Num();
		TSet<FIntVector> FaceSnapshotCells;
		for (const FLayoutLocalCellFaceRuleSnapshot& CellSnapshot : Snapshot.GeneratedLocalCellFaceRules)
		{
			if (!UniqueOccupiedCells.Contains(CellSnapshot.LocalCell)
				|| FaceSnapshotCells.Contains(CellSnapshot.LocalCell))
			{
				bFaceGridMatchesOccupiedCellSet = false;
			}
			FaceSnapshotCells.Add(CellSnapshot.LocalCell);
		}
		if (FaceSnapshotCells.Num() != UniqueOccupiedCells.Num())
		{
			bFaceGridMatchesOccupiedCellSet = false;
		}

		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ModuleSnapshot.LocalCellFaceGrid"),
			ELayoutValidationAssertionKind::SnapshotContractInitialized,
			bFaceGridMatchesOccupiedCellSet,
			{Snapshot.SnapshotId},
			bFaceGridMatchesOccupiedCellSet
				? FString()
				: FString::Printf(TEXT("Module '%s' must generate one unique local face snapshot for every occupied local cell."), *Snapshot.DebugName.ToString())));

		bool bExposedFaceContractsAreValid = true;
		const TArray<FLayoutFaceRule> CanonicalFaceRules = Snapshot.EffectiveFaceRules.ToArray();
		const bool bRequiresCanonicalFaceParity = Snapshot.SourceCompositeModule == nullptr;
		for (const FLayoutLocalCellFaceRuleSnapshot& CellSnapshot : Snapshot.GeneratedLocalCellFaceRules)
		{
			TSet<ELayoutFaceDirection> SeenDirections;
			for (const FLayoutFaceRule& FaceRule : CellSnapshot.ExposedFaceRules)
			{
				if (SeenDirections.Contains(FaceRule.Direction))
				{
					bExposedFaceContractsAreValid = false;
					continue;
				}
				SeenDirections.Add(FaceRule.Direction);
			}

			if (!bRequiresCanonicalFaceParity)
			{
				continue;
			}

			for (const FLayoutFaceRule& CanonicalFaceRule : CanonicalFaceRules)
			{
				const FIntVector NeighborCell =
					CellSnapshot.LocalCell + FLayoutDirectionUtils::ToCellDelta(CanonicalFaceRule.Direction);
				const bool bShouldExposeFace =
					!UniqueOccupiedCells.Contains(NeighborCell)
					|| IsCellOnOuterBoundsFace(CellSnapshot.LocalCell, Snapshot.BoundsCells, CanonicalFaceRule.Direction);
				const FLayoutFaceRule* ActualFaceRule = CellSnapshot.ExposedFaceRules.FindByPredicate(
					[Direction = CanonicalFaceRule.Direction](const FLayoutFaceRule& FaceRule)
					{
						return FaceRule.Direction == Direction;
					});
				if (bShouldExposeFace)
				{
					if (ActualFaceRule == nullptr || !AreEquivalentFaceRules(*ActualFaceRule, CanonicalFaceRule))
					{
						bExposedFaceContractsAreValid = false;
					}
				}
				else if (ActualFaceRule != nullptr)
				{
					bExposedFaceContractsAreValid = false;
				}
			}
		}

		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ModuleSnapshot.ExposedLocalFaceContract"),
			ELayoutValidationAssertionKind::SnapshotContractInitialized,
			bExposedFaceContractsAreValid,
			{Snapshot.SnapshotId},
			bExposedFaceContractsAreValid
				? FString()
				: FString::Printf(TEXT("Module '%s' generated exposed local-cell face rules that do not match the occupied-cell cutout shape."), *Snapshot.DebugName.ToString())));

	}
}
