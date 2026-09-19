// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutCompositeModuleAsset.h"

#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Diagnostics/LayoutEditorMessageLog.h"
#include "Layout/Types/LayoutEntryRootUtilities.h"

namespace
{
	struct FResolvedCompositeCell
	{
		const FLayoutCompositeModuleCell* SourceCell = nullptr;
		ULayoutModuleAsset* Module = nullptr;
		FLayoutModuleFaceRules WorldFaceRules;
		FGameplayTagContainer TraversalChannels;
		TArray<FLayoutFaceRule> ExposedFaceRules;
		bool bTraversable = false;
	};

	int32 NormalizeCompositeYawRotationSteps(const int32 YawRotationSteps)
	{
		int32 NormalizedSteps = YawRotationSteps % 4;
		if (NormalizedSteps < 0)
		{
			NormalizedSteps += 4;
		}

		return NormalizedSteps;
	}

	void AppendCompositeValidationResult(
		FDataValidationContext& Context,
		const FLayoutValidationResult& ValidationResult)
	{
		for (const FLayoutValidationMessage& Message : ValidationResult.Messages)
		{
			if (Message.Severity == ELayoutValidationSeverity::Error)
			{
				Context.AddError(FText::FromString(Message.Message));
			}
			else
			{
				Context.AddWarning(FText::FromString(Message.Message));
			}
		}
	}

	FString BuildCompositeValidationMessage(
		const ULayoutCompositeModuleAsset* Composite,
		const FString& Summary,
		const TArray<FString>& DetailLines)
	{
		FString Message = Summary;
		Message += FString::Printf(TEXT("\nComposite: %s"), Composite != nullptr ? *Composite->GetName() : TEXT("<none>"));
		for (const FString& DetailLine : DetailLines)
		{
			if (!DetailLine.IsEmpty())
			{
				Message += TEXT("\n");
				Message += DetailLine;
			}
		}

		return Message;
	}

	bool CanFaceConnectToFilledNeighbor(const FLayoutFaceRule& FaceRule)
	{
		return FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor;
	}

	bool IsCompositeSharedCellSizeSquareXY(const FIntVector& SharedCellSizeInBlocks)
	{
		return SharedCellSizeInBlocks.X == SharedCellSizeInBlocks.Y;
	}

	bool IsVerticalFaceDirection(const ELayoutFaceDirection Direction)
	{
		return Direction == ELayoutFaceDirection::PosZ
			|| Direction == ELayoutFaceDirection::NegZ;
	}

	bool AreCompositeFaceRulesTagCompatible(
		const FLayoutFaceRule& SourceFaceRule,
		const FLayoutFaceRule& TargetFaceRule)
	{
		return SourceFaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor
			|| SourceFaceRule.GetEffectiveAllowedConnectionTags().HasAnyExact(TargetFaceRule.GetEffectiveConnectionTags());
	}

	bool DoCompositeFaceRulesRequireMatchingYaw(
		const FLayoutFaceRule& SourceFaceRule,
		const FLayoutFaceRule& TargetFaceRule)
	{
		return IsVerticalFaceDirection(SourceFaceRule.Direction)
			&& IsVerticalFaceDirection(TargetFaceRule.Direction)
			&& (SourceFaceRule.bRequireMatchingYawWithFilledNeighbor
				|| TargetFaceRule.bRequireMatchingYawWithFilledNeighbor);
	}

	bool DoCompositeFacesShareTraversal(
		const FLayoutFaceRule& SourceFaceRule,
		const FLayoutFaceRule& TargetFaceRule)
	{
		for (const FGameplayTag& TraversalChannel : SourceFaceRule.ConnectedTraversalChannels)
		{
			if (TraversalChannel.IsValid() && TargetFaceRule.ConnectedTraversalChannels.HasTagExact(TraversalChannel))
			{
				return true;
			}
		}

		return false;
	}

	FLayoutModuleFaceRules BuildCompositeWorldFaceRulesForYaw(const ULayoutModuleAsset* Module, const int32 YawRotationSteps)
	{
		FLayoutModuleFaceRules WorldFaceRules;
		if (Module == nullptr)
		{
			return WorldFaceRules;
		}

		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection WorldDirection = static_cast<ELayoutFaceDirection>(DirectionIndex);
			const ELayoutFaceDirection AuthoredDirection = FLayoutDirectionUtils::RotateYaw(WorldDirection, -YawRotationSteps);
			FLayoutFaceRule Rule;
			if (Module->GetEffectiveFaceRule(AuthoredDirection, Rule))
			{
				Rule.Direction = WorldDirection;
				WorldFaceRules.SetRule(Rule);
			}
		}

		WorldFaceRules.NormalizeDirections();
		return WorldFaceRules;
	}

	bool IsLeafModuleEligibleForCompositeUse(const ULayoutModuleAsset* Module)
	{
		if (Module == nullptr)
		{
			return false;
		}

		const TArray<FIntVector> OccupiedLocalCells = Module->GetOccupiedLocalCells();
		return OccupiedLocalCells.Num() == 1
			&& OccupiedLocalCells[0] == FIntVector::ZeroValue;
	}

	FLayoutValidationResult ValidateCompositeModuleInternal(
		const ULayoutCompositeModuleAsset* Composite,
		const FIntVector& SharedCellSizeInBlocks)
	{
		FLayoutValidationResult Result;

		if (Composite == nullptr)
		{
			Result.AddError(TEXT("Layout composite module validation requires a valid composite asset."));
			return Result;
		}

		if (Composite->Cells.IsEmpty())
		{
			Result.AddError(BuildCompositeValidationMessage(
				Composite,
				TEXT("Layout composite module must contain at least one occupied cell."),
				{
					TEXT("Problem: No leaf modules are referenced by this composite."),
					TEXT("Fix: Add one or more occupied composite cells that reference reusable single-cell leaf modules.")
				}));
			return Result;
		}

		if (SharedCellSizeInBlocks != FIntVector::ZeroValue
			&& !IsCompositeSharedCellSizeSquareXY(SharedCellSizeInBlocks))
		{
			Result.AddError(BuildCompositeValidationMessage(
				Composite,
				TEXT("Layout composite module received a non-square established shared cell size contract."),
				{
					FString::Printf(TEXT("Established shared cell size: %s"), *SharedCellSizeInBlocks.ToString()),
					TEXT("Problem: The active leaf-module yaw contract now assumes one-cell X/Y metrics are square so composite leaf rotations remain freely available."),
					TEXT("Fix: Use a shared cell size whose X and Y match before composing this asset, or keep non-square metrics only in legacy fixture/import-export compatibility surfaces.")
				}));
		}

		TMap<FIntVector, int32> CellIndexByLocalCell;
		TArray<FResolvedCompositeCell> ResolvedCells;
		ResolvedCells.Reserve(Composite->Cells.Num());

		FIntVector InternalSharedCellSize = FIntVector::ZeroValue;
		bool bInternalSharedCellSizeInitialized = false;

		for (int32 CellIndex = 0; CellIndex < Composite->Cells.Num(); ++CellIndex)
		{
			const FLayoutCompositeModuleCell& Cell = Composite->Cells[CellIndex];
			if (Cell.LocalCell.X < 0 || Cell.LocalCell.Y < 0 || Cell.LocalCell.Z < 0)
			{
				Result.AddError(BuildCompositeValidationMessage(
					Composite,
					FString::Printf(TEXT("Composite cell %d uses a negative local cell."), CellIndex),
					{
						FString::Printf(TEXT("LocalCell: %s"), *Cell.LocalCell.ToString()),
						TEXT("Problem: Composite occupied cells must use non-negative local coordinates."),
						TEXT("Fix: Move this leaf module reference into a non-negative local cell.")
					}));
			}

			if (CellIndexByLocalCell.Contains(Cell.LocalCell))
			{
				Result.AddError(BuildCompositeValidationMessage(
					Composite,
					FString::Printf(TEXT("Composite cell %d duplicates another occupied local cell."), CellIndex),
					{
						FString::Printf(TEXT("LocalCell: %s"), *Cell.LocalCell.ToString()),
						TEXT("Problem: Each occupied local cell may only be authored once."),
						TEXT("Fix: Remove the duplicate occupied local cell or move one of the leaf references.")
					}));
			}
			else
			{
				CellIndexByLocalCell.Add(Cell.LocalCell, CellIndex);
			}

			if (Cell.RelativeYawRotationSteps < 0 || Cell.RelativeYawRotationSteps > 3)
			{
				Result.AddError(BuildCompositeValidationMessage(
					Composite,
					FString::Printf(TEXT("Composite cell %d uses an invalid relative yaw step."), CellIndex),
					{
						FString::Printf(TEXT("RelativeYawRotationSteps: %d"), Cell.RelativeYawRotationSteps),
						TEXT("Problem: Composite relative yaw steps must stay in the inclusive range [0,3]."),
						TEXT("Fix: Use 0, 1, 2, or 3 to describe authored quarter-turn rotation inside the composite.")
					}));
			}

			ULayoutModuleAsset* Module = Cell.Module;
			if (Module == nullptr)
			{
				Result.AddError(BuildCompositeValidationMessage(
					Composite,
					FString::Printf(TEXT("Composite cell %d is missing its referenced leaf module."), CellIndex),
					{
						FString::Printf(TEXT("LocalCell: %s"), *Cell.LocalCell.ToString()),
						TEXT("Problem: Every composite occupied cell must reference one reusable single-cell module."),
						TEXT("Fix: Assign the leaf module used at this occupied local cell.")
					}));
				continue;
			}

			const FLayoutValidationResult LeafValidation = Module->ValidateModule();
			Result.Messages.Append(LeafValidation.Messages);

			if (!IsLeafModuleEligibleForCompositeUse(Module))
			{
				Result.AddError(BuildCompositeValidationMessage(
					Composite,
					FString::Printf(TEXT("Composite cell %d references a non-leaf module."), CellIndex),
					{
						FString::Printf(TEXT("Leaf module: %s"), *Module->GetName()),
						FString::Printf(TEXT("OccupiedBoundsCells: %s"), *Module->GetOccupiedBoundsCells().ToString()),
						TEXT("Problem: First-pass composite modules may only reuse single-cell leaf modules with one occupied local cell at (0,0,0)."),
						TEXT("Fix: Reference a reusable 1x1x1 module asset here, or split the larger authored module before composing it.")
					}));
			}

			if (!bInternalSharedCellSizeInitialized)
			{
				InternalSharedCellSize = Module->GetEffectiveCellSizeInBlocks();
				bInternalSharedCellSizeInitialized = true;
				if (!IsCompositeSharedCellSizeSquareXY(InternalSharedCellSize))
				{
					Result.AddError(BuildCompositeValidationMessage(
						Composite,
						FString::Printf(TEXT("Composite cell %d introduces a non-square shared leaf cell size."), CellIndex),
						{
							FString::Printf(TEXT("Leaf module: %s"), *Module->GetName()),
							FString::Printf(TEXT("Leaf cell size: %s"), *InternalSharedCellSize.ToString()),
							TEXT("Problem: The active leaf-module yaw contract now assumes one-cell X/Y metrics are square so composite leaf rotations remain freely available."),
							TEXT("Fix: Rebuild the leaf module with square X/Y cell metrics, or keep non-square metrics only in legacy fixture/import-export compatibility surfaces.")
						}));
				}
			}
			else if (InternalSharedCellSize != Module->GetEffectiveCellSizeInBlocks())
			{
				Result.AddError(BuildCompositeValidationMessage(
					Composite,
					FString::Printf(TEXT("Composite cell %d uses a mismatched leaf cell size."), CellIndex),
					{
						FString::Printf(TEXT("Leaf module: %s"), *Module->GetName()),
						FString::Printf(TEXT("Leaf cell size: %s"), *Module->GetEffectiveCellSizeInBlocks().ToString()),
						FString::Printf(TEXT("Expected shared cell size: %s"), *InternalSharedCellSize.ToString()),
						TEXT("Problem: Every leaf module inside one composite must use the same shared cell size."),
						TEXT("Fix: Replace the mismatched leaf module or rebuild it to use the composite's shared cell size.")
					}));
			}

			if (SharedCellSizeInBlocks != FIntVector::ZeroValue
				&& SharedCellSizeInBlocks != Module->GetEffectiveCellSizeInBlocks())
			{
				Result.AddError(BuildCompositeValidationMessage(
					Composite,
					FString::Printf(TEXT("Composite cell %d violates the established shared cell size contract."), CellIndex),
					{
						FString::Printf(TEXT("Leaf module: %s"), *Module->GetName()),
						FString::Printf(TEXT("Leaf cell size: %s"), *Module->GetEffectiveCellSizeInBlocks().ToString()),
						FString::Printf(TEXT("Established shared cell size: %s"), *SharedCellSizeInBlocks.ToString()),
						TEXT("Problem: Composite leaf alignment is now checked against the caller-owned shared planning cell metrics once they are established."),
						TEXT("Fix: Use leaf modules that match the content set or request shared cell size, or change the broader shared cell-size contract before composing this asset.")
					}));
			}

			const int32 NormalizedYawRotationSteps = NormalizeCompositeYawRotationSteps(Cell.RelativeYawRotationSteps);

			FResolvedCompositeCell& ResolvedCell = ResolvedCells.AddDefaulted_GetRef();
			ResolvedCell.SourceCell = &Cell;
			ResolvedCell.Module = Module;
			ResolvedCell.WorldFaceRules = BuildCompositeWorldFaceRulesForYaw(Module, NormalizedYawRotationSteps);
			ResolvedCell.TraversalChannels = Module->GetEffectiveTraversalChannels();
			ResolvedCell.bTraversable = !ResolvedCell.TraversalChannels.IsEmpty();
		}

		TMap<int32, TArray<int32>> TraversableAdjacency;
		TArray<int32> TraversableCellIndices;
		for (int32 CellIndex = 0; CellIndex < ResolvedCells.Num(); ++CellIndex)
		{
			if (ResolvedCells[CellIndex].bTraversable)
			{
				TraversableCellIndices.Add(CellIndex);
			}
		}

		for (int32 CellIndex = 0; CellIndex < ResolvedCells.Num(); ++CellIndex)
		{
			FResolvedCompositeCell& ResolvedCell = ResolvedCells[CellIndex];
			const FLayoutCompositeModuleCell& SourceCell = *ResolvedCell.SourceCell;
			const int32 NormalizedYawRotationSteps = NormalizeCompositeYawRotationSteps(SourceCell.RelativeYawRotationSteps);

			for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
			{
				const ELayoutFaceDirection FaceDirection = static_cast<ELayoutFaceDirection>(DirectionIndex);
				const FIntVector NeighborLocalCell = SourceCell.LocalCell + FLayoutDirectionUtils::ToCellDelta(FaceDirection);
				const int32* NeighborIndex = CellIndexByLocalCell.Find(NeighborLocalCell);
				const FLayoutFaceRule* SourceFaceRule = ResolvedCell.WorldFaceRules.FindRule(FaceDirection);
				if (SourceFaceRule == nullptr)
				{
					continue;
				}

				if (NeighborIndex == nullptr)
				{
					ResolvedCell.ExposedFaceRules.Add(*SourceFaceRule);
					continue;
				}

				if (*NeighborIndex <= CellIndex)
				{
					continue;
				}

				const FResolvedCompositeCell& NeighborResolvedCell = ResolvedCells[*NeighborIndex];
				const FLayoutCompositeModuleCell& NeighborSourceCell = *NeighborResolvedCell.SourceCell;
				const int32 NeighborNormalizedYawRotationSteps = NormalizeCompositeYawRotationSteps(NeighborSourceCell.RelativeYawRotationSteps);
				const FLayoutFaceRule* NeighborFaceRule = NeighborResolvedCell.WorldFaceRules.FindRule(FLayoutDirectionUtils::GetOpposite(FaceDirection));
				if (NeighborFaceRule == nullptr)
				{
					continue;
				}

				const bool bGlueTagCompatible = AreCompositeFaceRulesTagCompatible(*SourceFaceRule, *NeighborFaceRule)
					&& AreCompositeFaceRulesTagCompatible(*NeighborFaceRule, *SourceFaceRule);
				const bool bGlueOccupancyCompatible = CanFaceConnectToFilledNeighbor(*SourceFaceRule)
					&& CanFaceConnectToFilledNeighbor(*NeighborFaceRule);
				const bool bGlueYawCompatible = !DoCompositeFaceRulesRequireMatchingYaw(*SourceFaceRule, *NeighborFaceRule)
					|| NormalizedYawRotationSteps == NeighborNormalizedYawRotationSteps;

				if (!bGlueTagCompatible || !bGlueOccupancyCompatible || !bGlueYawCompatible)
				{
					Result.AddError(BuildCompositeValidationMessage(
						Composite,
						TEXT("Composite glued neighbors are not structurally compatible."),
						{
							FString::Printf(TEXT("First cell: %s (%s)"), *SourceCell.LocalCell.ToString(), *GetNameSafe(ResolvedCell.Module)),
							FString::Printf(TEXT("Second cell: %s (%s)"), *NeighborSourceCell.LocalCell.ToString(), *GetNameSafe(NeighborResolvedCell.Module)),
							FString::Printf(TEXT("Shared face direction: %s"), *StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(FaceDirection))),
							TEXT("Problem: Opposing glued faces must both accept a filled neighbor, accept each other's connection tags, and honor any matching-yaw requirement."),
							TEXT("Fix: Change the referenced leaf modules, relative yaw, or face authoring so the glued internal boundary is structurally compatible.")
						}));
					continue;
				}

				if (ResolvedCell.bTraversable
					&& NeighborResolvedCell.bTraversable
					&& DoCompositeFacesShareTraversal(*SourceFaceRule, *NeighborFaceRule))
				{
					TraversableAdjacency.FindOrAdd(CellIndex).Add(*NeighborIndex);
					TraversableAdjacency.FindOrAdd(*NeighborIndex).Add(CellIndex);
				}
			}

			// Boundary intent is per-cell placement eligibility; explicit face-boundary requirements remain optional solver constraints.
			const bool bEntryCapable = ResolvedCell.Module != nullptr && ResolvedCell.Module->SupportsIntent(ELayoutCellIntent::Entry);
			const bool bVerticalAccessCapable = ResolvedCell.Module != nullptr && ResolvedCell.Module->SupportsIntent(ELayoutCellIntent::VerticalAccess);

			const bool bHasExplicitEntryExposedFace = ResolvedCell.ExposedFaceRules.ContainsByPredicate([](const FLayoutFaceRule& FaceRule)
			{
				return LayoutEntryRootUtilities::FaceProvidesExplicitEntry(FaceRule);
			});
			const bool bHasTraversableExposedFace = ResolvedCell.ExposedFaceRules.ContainsByPredicate([](const FLayoutFaceRule& FaceRule)
			{
				return FaceRule.IsTraversable();
			});

			if (bEntryCapable && !bHasExplicitEntryExposedFace)
			{
				Result.AddError(BuildCompositeValidationMessage(
					Composite,
					TEXT("Composite entry-capable leaf loses all exposed explicit entry faces."),
					{
						FString::Printf(TEXT("LocalCell: %s"), *SourceCell.LocalCell.ToString()),
						FString::Printf(TEXT("Leaf module: %s"), *GetNameSafe(ResolvedCell.Module)),
						TEXT("Problem: First-pass composites require each entry-capable leaf cell to retain at least one exposed explicit entry face after glue suppression."),
						TEXT("Fix: Change the local arrangement, relative yaw, or leaf selection so an explicit entry face remains exposed.")
					}));
			}

			if (bVerticalAccessCapable && !bHasTraversableExposedFace)
			{
				Result.AddError(BuildCompositeValidationMessage(
					Composite,
					TEXT("Composite vertical-access leaf loses all exposed traversable faces."),
					{
						FString::Printf(TEXT("LocalCell: %s"), *SourceCell.LocalCell.ToString()),
						FString::Printf(TEXT("Leaf module: %s"), *GetNameSafe(ResolvedCell.Module)),
						TEXT("Problem: First-pass composites require each vertical-access leaf cell to retain at least one exposed traversable face after glue suppression."),
						TEXT("Fix: Change the local arrangement, relative yaw, or leaf selection so a traversable face remains exposed.")
					}));
			}
		}

		if (TraversableCellIndices.Num() > 1)
		{
			TSet<int32> VisitedIndices;
			TArray<int32> PendingIndices;
			PendingIndices.Add(TraversableCellIndices[0]);

			while (!PendingIndices.IsEmpty())
			{
				const int32 CurrentIndex = PendingIndices.Pop(EAllowShrinking::No);
				if (VisitedIndices.Contains(CurrentIndex))
				{
					continue;
				}

				VisitedIndices.Add(CurrentIndex);
				for (const int32 NeighborIndex : TraversableAdjacency.FindOrAdd(CurrentIndex))
				{
					if (!VisitedIndices.Contains(NeighborIndex))
					{
						PendingIndices.Add(NeighborIndex);
					}
				}
			}

			if (VisitedIndices.Num() != TraversableCellIndices.Num())
			{
				Result.AddError(BuildCompositeValidationMessage(
					Composite,
					TEXT("Composite traversable leaf cells do not form one connected traversable subgraph."),
					{
						FString::Printf(TEXT("Traversable cell count: %d"), TraversableCellIndices.Num()),
						FString::Printf(TEXT("Connected traversable cell count: %d"), VisitedIndices.Num()),
						TEXT("Problem: Occupied wall or shell cells may remain non-traversable, but every traversal-capable leaf cell in one composite must connect through compatible glued traversal faces."),
						TEXT("Fix: Add compatible glued traversal between the disconnected traversal-capable cells, or change the leaf selection so only the intended connected subset exposes traversal.")
					}));
			}
		}

		return Result;
	}
}

FIntVector ULayoutCompositeModuleAsset::GetSharedCellSizeInBlocks() const
{
	for (const FLayoutCompositeModuleCell& Cell : Cells)
	{
		if (Cell.Module != nullptr)
		{
			return Cell.Module->GetEffectiveCellSizeInBlocks();
		}
	}

	return FIntVector::ZeroValue;
}

TArray<FIntVector> ULayoutCompositeModuleAsset::GetOccupiedLocalCells() const
{
	TArray<FIntVector> Result;
	Result.Reserve(Cells.Num());
	for (const FLayoutCompositeModuleCell& Cell : Cells)
	{
		Result.Add(Cell.LocalCell);
	}

	return Result;
}

FIntVector ULayoutCompositeModuleAsset::GetBoundsCells() const
{
	FIntVector Result = FIntVector::ZeroValue;
	for (const FLayoutCompositeModuleCell& Cell : Cells)
	{
		Result.X = FMath::Max(Result.X, Cell.LocalCell.X + 1);
		Result.Y = FMath::Max(Result.Y, Cell.LocalCell.Y + 1);
		Result.Z = FMath::Max(Result.Z, Cell.LocalCell.Z + 1);
	}

	return Result;
}

bool ULayoutCompositeModuleAsset::SupportsIntent(const ELayoutCellIntent Intent) const
{
	for (const FLayoutCompositeModuleCell& Cell : Cells)
	{
		if (Cell.Module != nullptr && Cell.Module->SupportsIntent(Intent))
		{
			return true;
		}
	}

	return false;
}

FGameplayTagContainer ULayoutCompositeModuleAsset::GetEffectiveTraversalChannels() const
{
	FGameplayTagContainer Result;
	for (const FLayoutCompositeModuleCell& Cell : Cells)
	{
		if (Cell.Module != nullptr)
		{
			Result.AppendTags(Cell.Module->GetEffectiveTraversalChannels());
		}
	}

	return Result;
}

FLayoutValidationResult ULayoutCompositeModuleAsset::ValidateCompositeModule() const
{
	return ValidateCompositeModuleInternal(this, FIntVector::ZeroValue);
}

FLayoutValidationResult ULayoutCompositeModuleAsset::ValidateCompositeModuleAgainstSharedCellSize(const FIntVector& SharedCellSizeInBlocks) const
{
	return ValidateCompositeModuleInternal(this, SharedCellSizeInBlocks);
}

#if WITH_EDITOR
void ULayoutCompositeModuleAsset::ValidateLayoutCompositeModuleInEditor() const
{
	PorismLayoutEditorMessageLog::ReportValidationResult(
		this,
		ValidateCompositeModule(),
		TEXT("Layout composite module validation passed."));
}

EDataValidationResult ULayoutCompositeModuleAsset::IsDataValid(FDataValidationContext& Context) const
{
	const FLayoutValidationResult ValidationResult = ValidateCompositeModule();
	AppendCompositeValidationResult(Context, ValidationResult);
	return ValidationResult.IsValid() ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
