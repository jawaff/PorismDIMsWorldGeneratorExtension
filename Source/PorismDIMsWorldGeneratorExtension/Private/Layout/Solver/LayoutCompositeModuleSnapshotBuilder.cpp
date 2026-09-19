// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutCompositeModuleSnapshotBuilder.h"

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Solver/LayoutSolveSnapshotValidation.h"

namespace LayoutCompositeModuleSnapshotBuilder
{
	namespace
	{
		FLayoutProofRecord MakeCompositeSnapshotProofRecord(
			const FLayoutId ProofId,
			const ELayoutProofKind ProofKind,
			const FLayoutId TargetId,
			const TArray<FLayoutId>& SourceIds,
			const FString& ProofSummary)
		{
			FLayoutProofRecord Proof;
			Proof.ProofId = ProofId;
			Proof.ProofKind = ProofKind;
			Proof.TargetId = TargetId;
			Proof.SourceIds = SourceIds;
			Proof.ProofSummary = ProofSummary;
			return Proof;
		}

		bool CanFaceConnectToFilledNeighbor(const FLayoutFaceRule& FaceRule)
		{
			return FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor
				|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor
				|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor
				|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor
				|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor;
		}

		bool AreFaceRulesTagCompatible(
			const FLayoutFaceRule& SourceFaceRule,
			const FLayoutFaceRule& TargetFaceRule)
		{
			return SourceFaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor
				|| SourceFaceRule.GetEffectiveAllowedConnectionTags().HasAnyExact(TargetFaceRule.GetEffectiveConnectionTags());
		}

		bool DoFaceRulesRequireMatchingYaw(
			const FLayoutFaceRule& SourceFaceRule,
			const FLayoutFaceRule& TargetFaceRule)
		{
			const bool bVerticalPair =
				(SourceFaceRule.Direction == ELayoutFaceDirection::PosZ || SourceFaceRule.Direction == ELayoutFaceDirection::NegZ)
				&& (TargetFaceRule.Direction == ELayoutFaceDirection::PosZ || TargetFaceRule.Direction == ELayoutFaceDirection::NegZ);
			return bVerticalPair
				&& (SourceFaceRule.bRequireMatchingYawWithFilledNeighbor
					|| TargetFaceRule.bRequireMatchingYawWithFilledNeighbor);
		}

		bool DoFaceRulesShareTraversal(
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

		int32 NormalizeCompositeYawRotationSteps(const int32 YawRotationSteps)
		{
			int32 NormalizedSteps = YawRotationSteps % 4;
			if (NormalizedSteps < 0)
			{
				NormalizedSteps += 4;
			}

			return NormalizedSteps;
		}

		FLayoutModuleFaceRules BuildWorldFaceRulesForYaw(const ULayoutModuleAsset* Module, const int32 YawRotationSteps)
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

		bool IsLexicographicallyEarlierDerivedInternalTraversalLink(
			const FLayoutDerivedInternalTraversalLink& Left,
			const FLayoutDerivedInternalTraversalLink& Right)
		{
			if (Left.FromLocalCell != Right.FromLocalCell)
			{
				return Left.FromLocalCell.Z != Right.FromLocalCell.Z ? Left.FromLocalCell.Z < Right.FromLocalCell.Z
					: (Left.FromLocalCell.Y != Right.FromLocalCell.Y ? Left.FromLocalCell.Y < Right.FromLocalCell.Y : Left.FromLocalCell.X < Right.FromLocalCell.X);
			}
			if (Left.ToLocalCell != Right.ToLocalCell)
			{
				return Left.ToLocalCell.Z != Right.ToLocalCell.Z ? Left.ToLocalCell.Z < Right.ToLocalCell.Z
					: (Left.ToLocalCell.Y != Right.ToLocalCell.Y ? Left.ToLocalCell.Y < Right.ToLocalCell.Y : Left.ToLocalCell.X < Right.ToLocalCell.X);
			}
			if (Left.FromTraversalChannel != Right.FromTraversalChannel)
			{
				return Left.FromTraversalChannel.ToString() < Right.FromTraversalChannel.ToString();
			}
			if (Left.ToTraversalChannel != Right.ToTraversalChannel)
			{
				return Left.ToTraversalChannel.ToString() < Right.ToTraversalChannel.ToString();
			}
			if (Left.bBidirectional != Right.bBidirectional)
			{
				return Left.bBidirectional && !Right.bBidirectional;
			}
			return Left.LinkId.LexicalLess(Right.LinkId);
		}

		template<typename EnumType>
		void SortEnumsCanonically(TArray<EnumType>& Values)
		{
			Values.Sort([](const EnumType Left, const EnumType Right)
			{
				return static_cast<int32>(Left) < static_cast<int32>(Right);
			});
		}

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

		void AppendFailedAssertionsToValidation(
			const TArray<FLayoutValidationAssertionRecord>& Assertions,
			FLayoutValidationResult& Validation)
		{
			for (const FLayoutValidationAssertionRecord& Assertion : Assertions)
			{
				if (!Assertion.bPassed)
				{
					Validation.AddError(
						Assertion.FailureReason.IsEmpty()
							? FString::Printf(TEXT("Snapshot assertion '%s' failed."), *Assertion.AssertionId.ToString())
							: Assertion.FailureReason);
				}
			}
		}
	}

	bool IsLexicographicallyEarlierCompositeCell(const FIntVector& Left, const FIntVector& Right)
	{
		return Left.Z != Right.Z ? Left.Z < Right.Z
			: (Left.Y != Right.Y ? Left.Y < Right.Y : Left.X < Right.X);
	}

	void BuildCompositeLocalShapeContracts(
		FLayoutModuleSolveSnapshot& Snapshot,
		const ULayoutCompositeModuleAsset* Composite)
	{
		Snapshot.GeneratedLocalCellFaceRules.Reset();
		Snapshot.Roles.Reset();
		Snapshot.SupportedCellIntents.Reset();
		Snapshot.TraversalChannels.Reset();
		Snapshot.InternalAccessLinks.Reset();
		Snapshot.DerivedInternalTraversalLinks.Reset();

		if (Composite == nullptr)
		{
			LayoutSolveSnapshotValidation::ValidateModuleLocalShapeContracts(Snapshot);
			return;
		}

		TMap<FIntVector, const FLayoutCompositeModuleCell*> CompositeCellsByLocalCell;
		TMap<FIntVector, FLayoutModuleFaceRules> WorldFaceRulesByLocalCell;

		for (const FLayoutCompositeModuleCell& CompositeCell : Composite->Cells)
		{
			CompositeCellsByLocalCell.Add(CompositeCell.LocalCell, &CompositeCell);
			WorldFaceRulesByLocalCell.Add(
				CompositeCell.LocalCell,
				BuildWorldFaceRulesForYaw(
					CompositeCell.Module,
					NormalizeCompositeYawRotationSteps(CompositeCell.RelativeYawRotationSteps)));

			if (CompositeCell.Module == nullptr)
			{
				continue;
			}

			for (const ELayoutModuleRole Role : CompositeCell.Module->GetEffectiveRoles())
			{
				Snapshot.Roles.AddUnique(Role);
			}

			for (const ELayoutCellIntent Intent : CompositeCell.Module->GetEffectiveSupportedCellIntents())
			{
				Snapshot.SupportedCellIntents.AddUnique(Intent);
			}

			for (const FGameplayTag& TraversalChannel : CompositeCell.Module->GetEffectiveTraversalChannels())
			{
				if (TraversalChannel.IsValid())
				{
					Snapshot.TraversalChannels.AddTag(TraversalChannel);
				}
			}

			for (const FLayoutInternalAccessLink& Link : CompositeCell.Module->GetEffectiveInternalAccessLinks())
			{
				if (!Link.FromTraversalChannel.IsValid() || !Link.ToTraversalChannel.IsValid())
				{
					continue;
				}

				FLayoutDerivedInternalTraversalLink& DerivedLink =
					Snapshot.DerivedInternalTraversalLinks.AddDefaulted_GetRef();
				DerivedLink.LinkId = FLayoutId(*FString::Printf(
					TEXT("%s.CompositeInternal.%s.%s.%s"),
					*Snapshot.SnapshotId.ToString(),
					*CompositeCell.LocalCell.ToString(),
					*Link.FromTraversalChannel.ToString(),
					*Link.ToTraversalChannel.ToString()));
				DerivedLink.FromLocalCell = CompositeCell.LocalCell;
				DerivedLink.FromTraversalChannel = Link.FromTraversalChannel;
				DerivedLink.ToLocalCell = CompositeCell.LocalCell;
				DerivedLink.ToTraversalChannel = Link.ToTraversalChannel;
				DerivedLink.bBidirectional = Link.bBidirectional;
			}
		}

		if (const FLayoutCompositeModuleCell* const* RootCompositeCell = CompositeCellsByLocalCell.Find(FIntVector::ZeroValue))
		{
			if (RootCompositeCell != nullptr && *RootCompositeCell != nullptr && (*RootCompositeCell)->Module != nullptr)
			{
				Snapshot.RootSupportedCellIntents = (*RootCompositeCell)->Module->GetEffectiveSupportedCellIntents();
			}
		}
		if (Snapshot.RootSupportedCellIntents.IsEmpty())
		{
			Snapshot.RootSupportedCellIntents = Snapshot.SupportedCellIntents;
		}

		SortEnumsCanonically(Snapshot.Roles);
		SortEnumsCanonically(Snapshot.SupportedCellIntents);
		SortEnumsCanonically(Snapshot.RootSupportedCellIntents);

		for (const FLayoutCompositeModuleCell& CompositeCell : Composite->Cells)
		{
			FLayoutLocalCellFaceRuleSnapshot& CellSnapshot = Snapshot.GeneratedLocalCellFaceRules.AddDefaulted_GetRef();
			CellSnapshot.LocalCell = CompositeCell.LocalCell;
			CellSnapshot.TemplatePath = CompositeCell.Module != nullptr
				? CompositeCell.Module->Template.ToSoftObjectPath()
				: FSoftObjectPath();
			CellSnapshot.RelativeYawRotationSteps =
				NormalizeCompositeYawRotationSteps(CompositeCell.RelativeYawRotationSteps);
			if (CompositeCell.Module != nullptr)
			{
				CellSnapshot.Roles = CompositeCell.Module->GetEffectiveRoles();
				CellSnapshot.SupportedCellIntents = CompositeCell.Module->GetEffectiveSupportedCellIntents();
				SortEnumsCanonically(CellSnapshot.Roles);
				SortEnumsCanonically(CellSnapshot.SupportedCellIntents);
			}

			const FLayoutModuleFaceRules* LocalWorldFaceRules = WorldFaceRulesByLocalCell.Find(CompositeCell.LocalCell);
			if (LocalWorldFaceRules == nullptr)
			{
				continue;
			}

			for (const FLayoutFaceRule& FaceRule : LocalWorldFaceRules->ToArray())
			{
				const FIntVector NeighborLocalCell = CompositeCell.LocalCell + FLayoutDirectionUtils::ToCellDelta(FaceRule.Direction);
				const FLayoutCompositeModuleCell* const* NeighborCompositeCell = CompositeCellsByLocalCell.Find(NeighborLocalCell);
				if (NeighborCompositeCell == nullptr || *NeighborCompositeCell == nullptr)
				{
					CellSnapshot.ExposedFaceRules.Add(FaceRule);
					continue;
				}

				const FLayoutModuleFaceRules* NeighborWorldFaceRules = WorldFaceRulesByLocalCell.Find(NeighborLocalCell);
				if (NeighborWorldFaceRules == nullptr)
				{
					continue;
				}

				const FLayoutFaceRule* NeighborFaceRule =
					NeighborWorldFaceRules->FindRule(FLayoutDirectionUtils::GetOpposite(FaceRule.Direction));
				if (NeighborFaceRule == nullptr)
				{
					continue;
				}

				const bool bGlueCompatible =
					CanFaceConnectToFilledNeighbor(FaceRule)
					&& CanFaceConnectToFilledNeighbor(*NeighborFaceRule)
					&& AreFaceRulesTagCompatible(FaceRule, *NeighborFaceRule)
					&& AreFaceRulesTagCompatible(*NeighborFaceRule, FaceRule)
					&& (!DoFaceRulesRequireMatchingYaw(FaceRule, *NeighborFaceRule)
						|| (NormalizeCompositeYawRotationSteps(CompositeCell.RelativeYawRotationSteps)
							== NormalizeCompositeYawRotationSteps((*NeighborCompositeCell)->RelativeYawRotationSteps)));
				if (!bGlueCompatible || !DoFaceRulesShareTraversal(FaceRule, *NeighborFaceRule))
				{
					continue;
				}

				if (!IsLexicographicallyEarlierCompositeCell(CompositeCell.LocalCell, NeighborLocalCell))
				{
					continue;
				}

				for (const FGameplayTag& TraversalChannel : FaceRule.ConnectedTraversalChannels)
				{
					if (!TraversalChannel.IsValid() || !NeighborFaceRule->ConnectedTraversalChannels.HasTagExact(TraversalChannel))
					{
						continue;
					}

					FLayoutDerivedInternalTraversalLink& DerivedLink =
						Snapshot.DerivedInternalTraversalLinks.AddDefaulted_GetRef();
					DerivedLink.LinkId = FLayoutId(*FString::Printf(
						TEXT("%s.CompositeGlue.%s.%s.%s"),
						*Snapshot.SnapshotId.ToString(),
						*CompositeCell.LocalCell.ToString(),
						*NeighborLocalCell.ToString(),
						*TraversalChannel.ToString()));
					DerivedLink.FromLocalCell = CompositeCell.LocalCell;
					DerivedLink.FromTraversalChannel = TraversalChannel;
					DerivedLink.ToLocalCell = NeighborLocalCell;
					DerivedLink.ToTraversalChannel = TraversalChannel;
					DerivedLink.bBidirectional = true;
				}
			}

			CellSnapshot.ExposedFaceRules.Sort([](const FLayoutFaceRule& Left, const FLayoutFaceRule& Right)
			{
				return static_cast<int32>(Left.Direction) < static_cast<int32>(Right.Direction);
			});
		}

		Snapshot.GeneratedLocalCellFaceRules.Sort([](
			const FLayoutLocalCellFaceRuleSnapshot& Left,
			const FLayoutLocalCellFaceRuleSnapshot& Right)
		{
			return IsLexicographicallyEarlierCompositeCell(Left.LocalCell, Right.LocalCell);
		});
		Snapshot.DerivedInternalTraversalLinks.Sort([](
			const FLayoutDerivedInternalTraversalLink& Left,
			const FLayoutDerivedInternalTraversalLink& Right)
		{
			return IsLexicographicallyEarlierDerivedInternalTraversalLink(Left, Right);
		});

		for (const FLayoutDerivedInternalTraversalLink& Link : Snapshot.DerivedInternalTraversalLinks)
		{
			Snapshot.ProofRecords.Add(MakeCompositeSnapshotProofRecord(
				FLayoutId(*FString::Printf(TEXT("%s.Proof"), *Link.LinkId.ToString())),
				ELayoutProofKind::DerivedContract,
				Link.LinkId,
				{Snapshot.SnapshotId},
				TEXT("Derived composite traversal bridge from glued leaf-module traversal contracts.")));
		}

		LayoutSolveSnapshotValidation::ValidateModuleLocalShapeContracts(Snapshot);
	}

	FLayoutModuleSolveSnapshot BuildCompositeModuleSnapshotFromAsset(
		const int32 SnapshotSchemaVersion,
		const ULayoutCompositeModuleAsset* Composite,
		const int32 Weight,
		const ELayoutPlacementZone PlacementZone,
		const ELayoutLevelPlacementPolicy LevelPlacementPolicy,
		const int32 SpecificLevel,
		const bool bOptional,
		const FIntVector& EstablishedSharedCellSizeInBlocks)
	{
		FLayoutModuleSolveSnapshot Snapshot;
		Snapshot.SnapshotSchemaVersion = SnapshotSchemaVersion;
		Snapshot.PlacementZone = PlacementZone;
		Snapshot.LevelPlacementPolicy = LevelPlacementPolicy;
		Snapshot.SpecificLevel = FMath::Max(0, SpecificLevel);
		Snapshot.bOptional = bOptional;
		if (Composite == nullptr)
		{
			Snapshot.Validation.AddError(TEXT("Layout composite-module snapshot requires a composite asset."));
			Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
				TEXT("ModuleSnapshot.CompositeSourcePresent"),
				ELayoutValidationAssertionKind::SnapshotSourcePresent,
				false,
				{},
				TEXT("Layout composite-module snapshot requires a composite asset.")));
			AppendFailedAssertionsToValidation(Snapshot.ValidationAssertions, Snapshot.Validation);
			return Snapshot;
		}

		Snapshot.SnapshotId = Composite->GetFName();
		Snapshot.SourceCompositeModule = const_cast<ULayoutCompositeModuleAsset*>(Composite);
		Snapshot.DebugName = Composite->GetFName();
		Snapshot.CellSizeInBlocks = Composite->GetSharedCellSizeInBlocks();
		Snapshot.BoundsCells = Composite->GetBoundsCells();
		Snapshot.TemplateDimensionsBlocks = FIntVector(
			Snapshot.BoundsCells.X * Snapshot.CellSizeInBlocks.X,
			Snapshot.BoundsCells.Y * Snapshot.CellSizeInBlocks.Y,
			Snapshot.BoundsCells.Z * Snapshot.CellSizeInBlocks.Z);
		Snapshot.OccupiedLocalCells = Composite->GetOccupiedLocalCells();
		Snapshot.OccupiedLocalCells.Sort([](const FIntVector& Left, const FIntVector& Right)
		{
			return IsLexicographicallyEarlierCompositeCell(Left, Right);
		});
		Snapshot.AllowedYawRotationSteps =
			Snapshot.CellSizeInBlocks.X == Snapshot.CellSizeInBlocks.Y
				? TArray<int32>{0, 1, 2, 3}
				: TArray<int32>{0};
		Snapshot.Weight = FMath::Max(1, Weight);
		Snapshot.Validation = EstablishedSharedCellSizeInBlocks != FIntVector::ZeroValue
			? Composite->ValidateCompositeModuleAgainstSharedCellSize(EstablishedSharedCellSizeInBlocks)
			: Composite->ValidateCompositeModule();
		Snapshot.ProofRecords.Add(MakeCompositeSnapshotProofRecord(
			TEXT("CompositeModuleSnapshot.Copy"),
			ELayoutProofKind::SnapshotCopy,
			Snapshot.SnapshotId,
			{Composite->GetFName()},
			TEXT("Copied composite-module contract and glued leaf-module arrangement into immutable solver snapshot.")));
		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ModuleSnapshot.CompositeSourcePresent"),
			ELayoutValidationAssertionKind::SnapshotSourcePresent,
			true,
			{Composite->GetFName()}));
		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ModuleSnapshot.CompositeAssetValidationPassed"),
			ELayoutValidationAssertionKind::AssetValidationPassed,
			Snapshot.Validation.IsValid(),
			{Composite->GetFName()},
			Snapshot.Validation.IsValid()
				? FString()
				: FString::Printf(
					TEXT("Composite module '%s' failed asset validation before snapshot solve setup."),
					*Composite->GetName())));
		BuildCompositeLocalShapeContracts(Snapshot, Composite);
		return Snapshot;
	}
}
