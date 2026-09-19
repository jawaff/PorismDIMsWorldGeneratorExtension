// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutModuleDerivedContractBuilder.h"

#include "Layout/Solver/LayoutSolveSnapshotValidation.h"

namespace LayoutModuleDerivedContractBuilder
{
	namespace
	{
		FLayoutProofRecord MakeSnapshotProofRecord(
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

		bool CanDeriveEndpointOfferFromFaceRule(const FLayoutFaceRule& FaceRule)
		{
			if (!FaceRule.GetEffectiveConnectionTag().IsValid() || FaceRule.ConnectedTraversalChannels.IsEmpty())
			{
				return false;
			}

			// Endpoint offers are now context-filtered by the consumer. Root-only validation is handled at
			// profile/request time, so child-only filled-neighbor interfaces still need to compile into the
			// immutable snapshot for child negotiation, vertical-access pairing, and later seam/closure work.
			return true;
		}

		bool CanDeriveSpanOfferFromFaceRule(const FLayoutFaceRule& FaceRule)
		{
			if (!FaceRule.GetEffectiveConnectionTag().IsValid())
			{
				return false;
			}

			// Span offers likewise need to preserve filled-neighbor child-facing boundary contracts.
			// The derived bSealsBoundary flag still distinguishes empty-capable root sealing from
			// child-only attached interfaces.
			return true;
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

		void DeriveModuleVerticalAccessContracts(FLayoutModuleSolveSnapshot& Snapshot)
		{
			Snapshot.DerivedVerticalAccessContracts.Reset();

			if (!Snapshot.SupportsIntent(ELayoutCellIntent::VerticalAccess))
			{
				return;
			}

			for (const FLayoutLocalCellFaceRuleSnapshot& CellSnapshot : Snapshot.GeneratedLocalCellFaceRules)
			{
				const FLayoutFaceRule* TopFaceRule = CellSnapshot.ExposedFaceRules.FindByPredicate([](const FLayoutFaceRule& FaceRule)
				{
					return FaceRule.Direction == ELayoutFaceDirection::PosZ;
				});

				FGameplayTagContainer ExitTraversalChannels;
				bool bRequireMatchingYawAtExit = false;
				if (TopFaceRule != nullptr && !TopFaceRule->ConnectedTraversalChannels.IsEmpty())
				{
					ExitTraversalChannels = TopFaceRule->ConnectedTraversalChannels;
					bRequireMatchingYawAtExit = TopFaceRule->bRequireMatchingYawWithFilledNeighbor;
				}
				else if (Snapshot.SourceCompositeModule != nullptr)
				{
					const FIntVector UpperLocalCell = CellSnapshot.LocalCell + FIntVector(0, 0, 1);
					for (const FLayoutDerivedInternalTraversalLink& Link : Snapshot.DerivedInternalTraversalLinks)
					{
						const bool bForwardVerticalBridge =
							Link.FromLocalCell == CellSnapshot.LocalCell
							&& Link.ToLocalCell == UpperLocalCell
							&& Link.FromTraversalChannel.IsValid();
						const bool bReverseBidirectionalVerticalBridge =
							Link.bBidirectional
							&& Link.ToLocalCell == CellSnapshot.LocalCell
							&& Link.FromLocalCell == UpperLocalCell
							&& Link.ToTraversalChannel.IsValid();
						if (bForwardVerticalBridge)
						{
							ExitTraversalChannels.AddTag(Link.FromTraversalChannel);
						}
						if (bReverseBidirectionalVerticalBridge)
						{
							ExitTraversalChannels.AddTag(Link.ToTraversalChannel);
						}
					}
				}

				if (ExitTraversalChannels.IsEmpty())
				{
					continue;
				}

				FGameplayTagContainer SourceTraversalChannels;
				for (const FLayoutDerivedEndpointOffer& Offer : Snapshot.DerivedEndpointOffers)
				{
					if (Offer.FaceDirection == ELayoutFaceDirection::PosZ && Offer.LocalCell == CellSnapshot.LocalCell)
					{
						continue;
					}

					for (const FGameplayTag& TraversalChannel : Offer.TraversalChannels)
					{
						if (TraversalChannel.IsValid() && ExitTraversalChannels.HasTagExact(TraversalChannel))
						{
							SourceTraversalChannels.AddTag(TraversalChannel);
						}
					}
				}

				for (const FLayoutInternalAccessLink& Link : Snapshot.InternalAccessLinks)
				{
					if (!Link.FromTraversalChannel.IsValid() || !Link.ToTraversalChannel.IsValid())
					{
						continue;
					}

					if (ExitTraversalChannels.HasTagExact(Link.ToTraversalChannel))
					{
						SourceTraversalChannels.AddTag(Link.FromTraversalChannel);
						ExitTraversalChannels.AddTag(Link.ToTraversalChannel);
					}

					if (Link.bBidirectional && ExitTraversalChannels.HasTagExact(Link.FromTraversalChannel))
					{
						SourceTraversalChannels.AddTag(Link.ToTraversalChannel);
						ExitTraversalChannels.AddTag(Link.FromTraversalChannel);
					}
				}

				for (const FLayoutDerivedInternalTraversalLink& Link : Snapshot.DerivedInternalTraversalLinks)
				{
					if (Link.ToLocalCell == CellSnapshot.LocalCell
						&& Link.FromTraversalChannel.IsValid()
						&& ExitTraversalChannels.HasTagExact(Link.ToTraversalChannel))
					{
						SourceTraversalChannels.AddTag(Link.FromTraversalChannel);
					}

					if (Link.bBidirectional
						&& Link.FromLocalCell == CellSnapshot.LocalCell
						&& Link.ToTraversalChannel.IsValid()
						&& ExitTraversalChannels.HasTagExact(Link.FromTraversalChannel))
					{
						SourceTraversalChannels.AddTag(Link.ToTraversalChannel);
					}
				}

				if (SourceTraversalChannels.IsEmpty() || ExitTraversalChannels.IsEmpty())
				{
					continue;
				}

				FLayoutDerivedVerticalAccessContract& Contract = Snapshot.DerivedVerticalAccessContracts.AddDefaulted_GetRef();
				Contract.ContractId = FLayoutId(*FString::Printf(
					TEXT("%s.VerticalAccess.%s.PosZ"),
					*Snapshot.SnapshotId.ToString(),
					*CellSnapshot.LocalCell.ToString()));
				Contract.LocalCell = CellSnapshot.LocalCell;
				Contract.SourceTraversalChannels = SourceTraversalChannels;
				Contract.ExitTraversalChannels = ExitTraversalChannels;
				Contract.ExitFaceDirection = ELayoutFaceDirection::PosZ;
				Contract.bRequireMatchingYawAtExit = bRequireMatchingYawAtExit;
				Snapshot.ProofRecords.Add(MakeSnapshotProofRecord(
					FLayoutId(*FString::Printf(TEXT("%s.Proof"), *Contract.ContractId.ToString())),
					ELayoutProofKind::DerivedVerticalAccess,
					Contract.ContractId,
					{Snapshot.SnapshotId},
					TopFaceRule != nullptr
						? TEXT("Derived vertical-access contract from canonical local PosZ face traversal on the compiled module shape.")
						: TEXT("Derived vertical-access contract from a composite's localized vertical traversal bridge after the leaf PosZ face became an internal glued boundary.")));
			}
		}
	}

	void BuildModuleLocalShapeContracts(FLayoutModuleSolveSnapshot& Snapshot)
	{
		Snapshot.GeneratedLocalCellFaceRules.Reset();
		Snapshot.DerivedInternalTraversalLinks.Reset();

		TSet<FIntVector> OccupiedCellSet;
		for (const FIntVector& Cell : Snapshot.OccupiedLocalCells)
		{
			OccupiedCellSet.Add(Cell);
		}

		const TArray<FLayoutFaceRule> CanonicalFaceRules = Snapshot.EffectiveFaceRules.ToArray();
		for (const FIntVector& OccupiedCell : Snapshot.OccupiedLocalCells)
		{
			FLayoutLocalCellFaceRuleSnapshot& CellSnapshot = Snapshot.GeneratedLocalCellFaceRules.AddDefaulted_GetRef();
			CellSnapshot.LocalCell = OccupiedCell;
			CellSnapshot.TemplatePath = Snapshot.Template.ToSoftObjectPath();
			CellSnapshot.RelativeYawRotationSteps = 0;
			CellSnapshot.Roles = Snapshot.Roles;
			CellSnapshot.SupportedCellIntents = Snapshot.SupportedCellIntents;

			for (const FLayoutFaceRule& FaceRule : CanonicalFaceRules)
			{
				const FIntVector NeighborCell = OccupiedCell + FLayoutDirectionUtils::ToCellDelta(FaceRule.Direction);
				if (OccupiedCellSet.Contains(NeighborCell)
					&& !IsCellOnOuterBoundsFace(OccupiedCell, Snapshot.BoundsCells, FaceRule.Direction))
				{
					continue;
				}

				CellSnapshot.ExposedFaceRules.Add(FaceRule);
			}
		}

		for (const FIntVector& OccupiedCell : Snapshot.OccupiedLocalCells)
		{
			for (const ELayoutFaceDirection Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::PosY, ELayoutFaceDirection::PosZ})
			{
				const FIntVector NeighborCell = OccupiedCell + FLayoutDirectionUtils::ToCellDelta(Direction);
				if (!OccupiedCellSet.Contains(NeighborCell))
				{
					continue;
				}

				for (const FGameplayTag& TraversalChannel : Snapshot.TraversalChannels)
				{
					if (!TraversalChannel.IsValid())
					{
						continue;
					}

					FLayoutDerivedInternalTraversalLink& Link = Snapshot.DerivedInternalTraversalLinks.AddDefaulted_GetRef();
					Link.LinkId = FLayoutId(*FString::Printf(
						TEXT("%s.Internal.%s.%s.%s"),
						*Snapshot.SnapshotId.ToString(),
						*OccupiedCell.ToString(),
						*NeighborCell.ToString(),
						*TraversalChannel.ToString()));
					Link.FromLocalCell = OccupiedCell;
					Link.FromTraversalChannel = TraversalChannel;
					Link.ToLocalCell = NeighborCell;
					Link.ToTraversalChannel = TraversalChannel;
					Link.bBidirectional = true;

					Snapshot.ProofRecords.Add(MakeSnapshotProofRecord(
						FLayoutId(*FString::Printf(TEXT("%s.Proof"), *Link.LinkId.ToString())),
						ELayoutProofKind::DerivedContract,
						Link.LinkId,
						{Snapshot.SnapshotId},
						TEXT("Derived internal occupied-cell traversal bridge from adjacent canonical module cells that share the module traversal contract.")));
				}
			}
		}

		LayoutSolveSnapshotValidation::ValidateModuleLocalShapeContracts(Snapshot);
	}

	void DeriveModuleInterfaceContracts(FLayoutModuleSolveSnapshot& Snapshot)
	{
		Snapshot.DerivedEndpointOffers.Reset();
		Snapshot.DerivedSpanOffers.Reset();

		for (const FLayoutLocalCellFaceRuleSnapshot& CellSnapshot : Snapshot.GeneratedLocalCellFaceRules)
		{
			for (const FLayoutFaceRule& FaceRule : CellSnapshot.ExposedFaceRules)
			{
				if (CanDeriveEndpointOfferFromFaceRule(FaceRule))
				{
					FLayoutDerivedEndpointOffer& Offer = Snapshot.DerivedEndpointOffers.AddDefaulted_GetRef();
					Offer.OfferId = FLayoutId(*FString::Printf(
						TEXT("%s.Endpoint.%s.%s"),
						*Snapshot.SnapshotId.ToString(),
						*CellSnapshot.LocalCell.ToString(),
						*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(FaceRule.Direction))));
					Offer.LocalCell = CellSnapshot.LocalCell;
					Offer.FaceDirection = FaceRule.Direction;
					Offer.ConnectionTag = FaceRule.GetEffectiveConnectionTag();
					Offer.AllowedConnectionTags = FaceRule.GetEffectiveAllowedConnectionTags();
					Offer.TraversalChannels = FaceRule.ConnectedTraversalChannels;
					Offer.Roles = Snapshot.Roles;
					Offer.OccupancyPolicy = FaceRule.OccupancyPolicy;
					Snapshot.ProofRecords.Add(MakeSnapshotProofRecord(
						FLayoutId(*FString::Printf(TEXT("%s.Proof"), *Offer.OfferId.ToString())),
						ELayoutProofKind::DerivedOffer,
						Offer.OfferId,
						{Snapshot.SnapshotId},
						FString::Printf(TEXT("Derived endpoint offer from canonical local face rule %s on cell %s."),
							*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(FaceRule.Direction)),
							*CellSnapshot.LocalCell.ToString())));
				}

				const bool bBoundaryCapableRole = Snapshot.Roles.Contains(ELayoutModuleRole::Boundary)
					|| Snapshot.Roles.Contains(ELayoutModuleRole::Entry)
					|| Snapshot.Roles.Contains(ELayoutModuleRole::VerticalAccess);
				if (bBoundaryCapableRole && CanDeriveSpanOfferFromFaceRule(FaceRule))
				{
					FLayoutDerivedSpanOffer& SpanOffer = Snapshot.DerivedSpanOffers.AddDefaulted_GetRef();
					SpanOffer.SpanOfferId = FLayoutId(*FString::Printf(
						TEXT("%s.Span.%s.%s"),
						*Snapshot.SnapshotId.ToString(),
						*CellSnapshot.LocalCell.ToString(),
						*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(FaceRule.Direction))));
					SpanOffer.ClosureId = NAME_None;
					SpanOffer.LocalCell = CellSnapshot.LocalCell;
					SpanOffer.FaceDirection = FaceRule.Direction;
					SpanOffer.ConnectionTag = FaceRule.GetEffectiveConnectionTag();
					SpanOffer.AllowedConnectionTags = FaceRule.GetEffectiveAllowedConnectionTags();
					SpanOffer.Roles = Snapshot.Roles;
					SpanOffer.ThicknessCells = 1;
					SpanOffer.bSealsBoundary = FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor
						|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor
						|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor
						|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor;
					Snapshot.ProofRecords.Add(MakeSnapshotProofRecord(
						FLayoutId(*FString::Printf(TEXT("%s.Proof"), *SpanOffer.SpanOfferId.ToString())),
						ELayoutProofKind::DerivedSpan,
						SpanOffer.SpanOfferId,
						{Snapshot.SnapshotId},
						FString::Printf(TEXT("Derived boundary span offer from canonical local face rule %s on cell %s."),
							*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(FaceRule.Direction)),
							*CellSnapshot.LocalCell.ToString())));
				}
			}
		}

		const bool bRequiresEndpointOffer = Snapshot.SupportsIntent(ELayoutCellIntent::Entry)
			|| Snapshot.SupportsIntent(ELayoutCellIntent::VerticalAccess);
		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ModuleSnapshot.DerivedEndpointOfferContract"),
			ELayoutValidationAssertionKind::DerivedOfferContractValid,
			!bRequiresEndpointOffer || Snapshot.DerivedEndpointOffers.Num() > 0,
			{Snapshot.SnapshotId},
			!bRequiresEndpointOffer || Snapshot.DerivedEndpointOffers.Num() > 0
				? FString()
				: FString::Printf(TEXT("Module '%s' supports Entry or VerticalAccess but does not derive any endpoint offers from canonical face rules."), *Snapshot.DebugName.ToString())));

		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ModuleSnapshot.DerivedSpanOfferContract"),
			ELayoutValidationAssertionKind::DerivedSpanContractValid,
			!Snapshot.SupportsIntent(ELayoutCellIntent::Boundary) || Snapshot.DerivedSpanOffers.Num() > 0,
			{Snapshot.SnapshotId},
			!Snapshot.SupportsIntent(ELayoutCellIntent::Boundary) || Snapshot.DerivedSpanOffers.Num() > 0
				? FString()
				: FString::Printf(TEXT("Module '%s' supports Boundary but does not derive any boundary span offers from canonical face rules."), *Snapshot.DebugName.ToString())));

		DeriveModuleVerticalAccessContracts(Snapshot);
		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ModuleSnapshot.DerivedVerticalAccessContract"),
			ELayoutValidationAssertionKind::DerivedVerticalAccessContractValid,
			!Snapshot.SupportsIntent(ELayoutCellIntent::VerticalAccess) || Snapshot.DerivedVerticalAccessContracts.Num() > 0,
			{Snapshot.SnapshotId},
			!Snapshot.SupportsIntent(ELayoutCellIntent::VerticalAccess) || Snapshot.DerivedVerticalAccessContracts.Num() > 0
				? FString()
				: FString::Printf(TEXT("Module '%s' supports VerticalAccess but does not derive any vertical-access contracts from canonical face rules and internal access links."), *Snapshot.DebugName.ToString())));
	}
}
