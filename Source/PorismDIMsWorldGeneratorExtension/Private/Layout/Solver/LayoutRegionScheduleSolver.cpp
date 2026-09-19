// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutProfileSolverInternal.h"
#include "LayoutSolveExecutionBudget.h"
#include "Layout/Solver/LayoutRegionScheduleSolverFacade.h"

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "LayoutRegionRequestSnapshotBuilder.h"
#include "LayoutRegionSchedulePlacementBridge.h"
#include "LayoutRegionSchedulePlacementBridgeTypes.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"

/**
 * Rewrite coordinator for the staged recursive scheduler replacement.
 *
 * This file owns the coordinator/bootstrap path and routes work into the
 * rewrite-owned pipeline stages declared in `LayoutRegionScheduleSolverFacade.h`.
 * The remaining placement-bridge compatibility body now lives in
 * `LayoutRegionSchedulePlacementBridge.cpp` instead of an included `.inl`, so
 * this file stays focused on stage ordering and root-request orchestration.
 *
 * Target stage homes:
 * - context/bootstrap
 * - child capability compilation
 * - residual parent analysis
 * - responsibility negotiation
 * - seam planning and optional-child decisions
 * - proof-contract construction and thread-ready proof jobs
 * - proof execution that can later move off the compatibility adapter
 * - final proof, audit, and post-structural handoff
 */

namespace LayoutRegionScheduleSolverPrivate
{
	FCommittedVerticalAccessOwnership DetermineCommittedVerticalAccessOwnership(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TSet<FIntVector>& ReservedParentCells,
		const TArray<FAutomaticChildPlacement>& Placements);
	bool TrySelectNegotiatedHostVerticalAccessAnchors(
		const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
		const TArray<FLayoutPlannedCell>& ChildPlannedCells,
		const TArray<FIntVector>& ChildVerticalAccessLocalCells,
		FLayoutCommittedEndpointAnchor& OutIngressAnchor,
		FLayoutCommittedEndpointAnchor& OutEgressAnchor,
		TArray<FIntVector>& OutRouteCells,
		FString& OutFailureReason);
	bool ValidateNegotiatedChildResponsibilityContract(
		const FLayoutNegotiatedChildResponsibilityContract& Contract,
		FString& OutFailureReason);
	void NormalizeSuccessfulChildPlacementCandidatesForTestsInternal(
		TArray<LayoutProfileSolverInternal::FNegotiatedCandidateNormalizationInputForTests>& InOutCandidates,
		const bool bPartialPlacementStep,
		const int32 PlacementDemandCount,
		int32& OutCollapsedCount,
		int32& OutBeamTrimmedCount);
	bool TryGetSharedOverlapFaceDirections(
		const FIntVector& FirstLocalCell,
		const FIntPoint& FirstFootprintSize,
		const FIntVector& SecondLocalCell,
		const FIntPoint& SecondFootprintSize,
		ELayoutFaceDirection& OutFirstFaceDirection,
		ELayoutFaceDirection& OutSecondFaceDirection);
}

namespace LayoutRegionScheduleSolverFacade
{
	namespace
	{
		static int32 BuildStageSeed(const int32 BaseSeed, const TCHAR* StageName)
		{
			return static_cast<int32>(HashCombineFast(
				static_cast<uint32>(BaseSeed),
				GetTypeHash(FName(StageName))));
		}

		static bool IsProtectedParentTraversalIntent(const ELayoutCellIntent Intent)
		{
			switch (Intent)
			{
			case ELayoutCellIntent::Entry:
			case ELayoutCellIntent::Connector:
			case ELayoutCellIntent::Core:
			case ELayoutCellIntent::Interior:
			case ELayoutCellIntent::VerticalAccess:
				return true;
			default:
				return false;
			}
		}

		static void AppendForcedPlacementBundleInsertionIfMissingOnPlacementBridge(
			TArray<FLayoutForcedPlacementBundleInsertion>& InOutInsertions,
			const FLayoutForcedPlacementBundleInsertion& Insertion)
		{
			if (!InOutInsertions.ContainsByPredicate(
				[&Insertion](const FLayoutForcedPlacementBundleInsertion& Existing)
				{
					return Existing.BundleId == Insertion.BundleId
						&& Existing.AnchorCell == Insertion.AnchorCell
						&& Existing.ProvingCell == Insertion.ProvingCell;
				}))
			{
				InOutInsertions.Add(Insertion);
			}
		}

		static void AppendRequiredRouteConstraintIfMissingOnPlacementBridge(
			TArray<FLayoutRouteConstraintRecord>& InOutRouteConstraints,
			const FLayoutRouteConstraintRecord& RouteConstraint)
		{
			FLayoutRouteConstraintRecord* ExistingConstraint =
				InOutRouteConstraints.FindByPredicate(
					[&RouteConstraint](const FLayoutRouteConstraintRecord& Existing)
					{
						return Existing.Cell == RouteConstraint.Cell;
					});
			if (ExistingConstraint == nullptr)
			{
				InOutRouteConstraints.Add(RouteConstraint);
				return;
			}

			if (ExistingConstraint->ConstraintId == NAME_None)
			{
				ExistingConstraint->ConstraintId = RouteConstraint.ConstraintId;
			}
			ExistingConstraint->bScoreAsMainRoute =
				ExistingConstraint->bScoreAsMainRoute || RouteConstraint.bScoreAsMainRoute;
			if (ExistingConstraint->Intent != RouteConstraint.Intent)
			{
				InOutRouteConstraints.Add(RouteConstraint);
				return;
			}
			for (const FLayoutRouteFaceRequirement& Requirement : RouteConstraint.FaceRequirements)
			{
				FLayoutRouteFaceRequirement* ExistingRequirement =
					ExistingConstraint->FaceRequirements.FindByPredicate(
						[&Requirement](const FLayoutRouteFaceRequirement& Existing)
						{
							return Existing.FaceDirection == Requirement.FaceDirection;
						});
				if (ExistingRequirement != nullptr)
				{
					if (ExistingRequirement->TraversalChannel != Requirement.TraversalChannel)
					{
						ExistingConstraint->FaceRequirements.Add(Requirement);
					}
				}
				else
				{
					ExistingConstraint->FaceRequirements.Add(Requirement);
				}
			}
		}

		static ELayoutSteppedTerrainTransitionSupportStatus TranslateSteppedTransitionSupportStatus(
			const ETerrainSteppedTransitionRootSupportStatus Status)
		{
			switch (Status)
			{
			case ETerrainSteppedTransitionRootSupportStatus::MissingAdjacentVerticalAccessIntent:
				return ELayoutSteppedTerrainTransitionSupportStatus::MissingAdjacentVerticalAccessIntent;
			case ETerrainSteppedTransitionRootSupportStatus::MissingRootVerticalAccessCandidate:
				return ELayoutSteppedTerrainTransitionSupportStatus::MissingRootVerticalAccessCandidate;
			case ETerrainSteppedTransitionRootSupportStatus::SupportedByRootVerticalAccessCandidate:
				return ELayoutSteppedTerrainTransitionSupportStatus::SupportedByRootVerticalAccessCandidate;
			default:
				return ELayoutSteppedTerrainTransitionSupportStatus::MissingAdjacentVerticalAccessIntent;
			}
		}

		static FLayoutSteppedTerrainTransitionDiagnostic BuildPublicSteppedTerrainTransitionDiagnostic(
			const FTerrainSteppedTransitionRequirement& Requirement)
		{
			FLayoutSteppedTerrainTransitionDiagnostic Diagnostic;
			Diagnostic.FromCell = Requirement.FromCell;
			Diagnostic.ToCell = Requirement.ToCell;
			Diagnostic.StepHeightBlocks = Requirement.StepHeightBlocks;
			Diagnostic.SupportStatus = TranslateSteppedTransitionSupportStatus(
				Requirement.RootSupportStatus);
			return Diagnostic;
		}

		static void ApplySteppedTerrainTransitionDiagnosticsToRegionResult(
			FLayoutRegionSolveResult& InOutRootResult,
			const FCompiledStructuralInputs& StructuralInputs)
		{
			InOutRootResult.SteppedTerrainTransitionDiagnostics.Reset();
			for (const FTerrainSteppedTransitionRequirement& Requirement :
				StructuralInputs.RootTerrainSteppedTransitionRequirements)
			{
				InOutRootResult.SteppedTerrainTransitionDiagnostics.Add(
					BuildPublicSteppedTerrainTransitionDiagnostic(Requirement));
			}

			InOutRootResult.UnsupportedSteppedTerrainTransitionDiagnostics.Reset();
			for (const FTerrainSteppedTransitionRequirement& Requirement :
				StructuralInputs.RootUnsupportedTerrainSteppedTransitionRequirements)
			{
				InOutRootResult.UnsupportedSteppedTerrainTransitionDiagnostics.Add(
					BuildPublicSteppedTerrainTransitionDiagnostic(Requirement));
			}
			InOutRootResult.bCanSatisfySteppedTerrainTransitions =
				StructuralInputs.bRootCanSatisfyTerrainSteppedTransitions;
		}

		static void AppendPreparedSteppedCarriersToRequest(
			FLayoutRegionSolveRequest& InOutRequest,
			const FTerrainSteppedPreparedSolveContract& PreparedContract)
		{
			for (const FTerrainSteppedInsertionPlan& InsertionPlan :
				PreparedContract.InsertionPlans)
			{
				FLayoutForcedPlacementBundleInsertion Insertion;
				Insertion.BundleId =
					InsertionPlan.SelectedRootPlacementBundle.BundleId;
				Insertion.AnchorCell = InsertionPlan.BundleAnchorCell;
				Insertion.ProvingCell =
					InsertionPlan.SupportPlan.SelectedSupportingRootVerticalAccessCell;
				AppendForcedPlacementBundleInsertionIfMissingOnPlacementBridge(
					InOutRequest.ForcedPlacementBundleInsertions,
					Insertion);
			}

			for (const FLayoutRouteConstraintRecord& RouteConstraint :
				PreparedContract.RouteConstraints)
			{
				AppendRequiredRouteConstraintIfMissingOnPlacementBridge(
					InOutRequest.RequiredRouteConstraints,
					RouteConstraint);
			}
		}

		static void ApplySteppedTerrainTransitionDiagnosticsToScheduleResult(
			FLayoutRegionSolveScheduleResult& InOutScheduleResult,
			const FString& RootRegionDebugPath,
			const FCompiledStructuralInputs& StructuralInputs)
		{
			FLayoutRegionSolveResult* RootResult =
				InOutScheduleResult.RegionResults.FindByPredicate(
					[&RootRegionDebugPath](const FLayoutRegionSolveResult& Result)
					{
						return Result.RegionDebugPath == RootRegionDebugPath;
					});
			if (RootResult == nullptr)
			{
				return;
			}

			ApplySteppedTerrainTransitionDiagnosticsToRegionResult(
				*RootResult,
				StructuralInputs);
		}

		static void ApplyRootSteppedDiagnosticsToMergedSolveResult(
			FLayoutRegionSolveScheduleResult& InOutScheduleResult,
			const FString& RootRegionDebugPath)
		{
			const FLayoutRegionSolveResult* RootResult =
				InOutScheduleResult.RegionResults.FindByPredicate(
					[&RootRegionDebugPath](const FLayoutRegionSolveResult& Result)
					{
						return Result.RegionDebugPath == RootRegionDebugPath;
					});
			if (RootResult == nullptr)
			{
				return;
			}

			InOutScheduleResult.MergedSolveResult.SteppedTerrainSupportMap =
				RootResult->SteppedTerrainSupportMap;
			InOutScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions =
				RootResult->ForcedPlacementBundleInsertions;
			InOutScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints =
				RootResult->RequiredRouteConstraints;
			InOutScheduleResult.MergedSolveResult.SteppedTerrainTransitionDiagnostics =
				RootResult->SteppedTerrainTransitionDiagnostics;
			InOutScheduleResult.MergedSolveResult.UnsupportedSteppedTerrainTransitionDiagnostics =
				RootResult->UnsupportedSteppedTerrainTransitionDiagnostics;
			InOutScheduleResult.MergedSolveResult.FutureTerraceProofClosureSegments =
				RootResult->FutureTerraceProofClosureSegments;
			InOutScheduleResult.MergedSolveResult.TerrainStageDiagnostics =
				RootResult->TerrainStageDiagnostics;
			InOutScheduleResult.MergedSolveResult.TerrainFrontierOwnershipDiagnostics =
				RootResult->TerrainFrontierOwnershipDiagnostics;
			InOutScheduleResult.MergedSolveResult.bCanSatisfySteppedTerrainTransitions =
				RootResult->bCanSatisfySteppedTerrainTransitions;
		}

		static void ApplyRequestOwnedSolveSurfaceToMergedSolveResult(
			FLayoutRegionSolveScheduleResult& InOutScheduleResult,
			const FLayoutRegionSolveRequest& Request)
		{
			const FIntVector ContentSetSharedCellSize =
				Request.ContentSetSnapshot.SharedCellSizeInBlocks;
			const FIntVector ResolvedCatalogSharedCellSize =
				Request.ModuleCatalog.SharedCellSizeInBlocks;
			const FIntVector RequestSharedCellSize =
				ContentSetSharedCellSize != FIntVector::ZeroValue
					? ContentSetSharedCellSize
					: ResolvedCatalogSharedCellSize;

			if (RequestSharedCellSize != FIntVector::ZeroValue)
			{
				InOutScheduleResult.MergedSolveResult.SharedCellSizeInBlocks =
					RequestSharedCellSize;
			}

			InOutScheduleResult.MergedSolveResult.TemplatePlacementZOffsetBlocks =
				Request.TemplatePlacementZOffsetBlocks;
			InOutScheduleResult.MergedSolveResult.RootPlacementKind =
				Request.bHasSelectedModePlan
					? Request.SelectedModePlan.PlacementKind
					: Request.RootPlacementKind;
			if (const FLayoutRegionSolveResult* RootResult =
				InOutScheduleResult.RegionResults.FindByPredicate(
					[](const FLayoutRegionSolveResult& Result)
					{
						return Result.SourceContentEntryId.IsNone();
					}))
			{
				InOutScheduleResult.MergedSolveResult.ResolvedTerrainAlignmentLevel =
					RootResult->SolveResult.ResolvedTerrainAlignmentLevel;
			}
			InOutScheduleResult.MergedSolveResult.WorldBindingPlacementPolicy =
				Request.bHasSelectedModePlan
					? Request.SelectedModePlan.PlacementPolicy
					: Request.WorldBindingPlacementPolicy;
			// Caller-facing merged results must keep request-contract assertions because
			// some active seams only retain the merged solve payload after failure.
			InOutScheduleResult.MergedSolveResult.ValidationAssertions =
				Request.ValidationAssertions;
			InOutScheduleResult.MergedSolveResult.SteppedTerrainSupportMap =
				Request.SteppedTerrainSupportMap;
			// Capability-backed and connector-rooted solves can materialize stepped carriers
			// on the merged result even when the request-owned surface stayed thinner. Keep
			// those solved carriers and only backfill from the request when the merged
			// surface did not already publish them.
			if (InOutScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions.IsEmpty())
			{
				InOutScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions =
					Request.ForcedPlacementBundleInsertions;
			}
			if (InOutScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints.IsEmpty())
			{
				InOutScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints =
					Request.RequiredRouteConstraints;
			}
			if (InOutScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions.IsEmpty())
			{
				for (const FLayoutPlacedModule& Placement : InOutScheduleResult.MergedSolveResult.Placements)
				{
					if (Placement.Intent != ELayoutCellIntent::VerticalAccess
						|| Placement.ModuleSnapshotId.IsNone()
						|| Placement.ModuleSnapshotIndex == INDEX_NONE
						|| Request.EffectiveSnapshotId.IsNone())
					{
						continue;
					}

					FLayoutForcedPlacementBundleInsertion& Insertion =
						InOutScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
					Insertion.BundleId = FLayoutId(*FString::Printf(
						TEXT("%s.Bundle.%d.%s"),
						*Request.EffectiveSnapshotId.ToString(),
						Placement.ModuleSnapshotIndex,
						*Placement.ModuleSnapshotId.ToString()));
					Insertion.AnchorCell = Placement.Cell;
					Insertion.ProvingCell = Placement.Cell;
				}
			}
		}

		static void AppendProtectedStructuralCellIfMissing(
			TArray<FIntVector>& InOutCells,
			const FIntVector& Cell)
		{
			if (!InOutCells.Contains(Cell))
			{
				InOutCells.Add(Cell);
			}
		}

		static void AppendTerrainSteppedRequiredTraversalCellsToProtectedContext(
			FRecursiveScheduleSolveContext& InOutSolveContext,
			const FCompiledStructuralInputs& StructuralInputs)
		{
			for (const FTerrainSteppedTransitionRequirement& Requirement :
				StructuralInputs.RootTerrainSteppedTransitionRequirements)
			{
				for (const FIntVector& RequiredCell : Requirement.RequiredTraversalCells)
				{
					AppendProtectedStructuralCellIfMissing(
						InOutSolveContext.RootRequest.ProtectedStructuralCells,
						RequiredCell);
					InOutSolveContext.ProtectedParentTraversalCells.Add(RequiredCell);
				}
			}

			InOutSolveContext.RootRequest.ProtectedStructuralCells.Sort([](
				const FIntVector& Left,
				const FIntVector& Right)
			{
				if (Left.Z != Right.Z)
				{
					return Left.Z < Right.Z;
				}
				if (Left.Y != Right.Y)
				{
					return Left.Y < Right.Y;
				}
				return Left.X < Right.X;
			});
		}

		static FLayoutValidationAssertionRecord MakeRequestValidationAssertion(
			const FLayoutId AssertionId,
			const bool bPassed,
			const TArray<FLayoutId>& RelatedIds,
			const FString& FailureReason = FString())
		{
			FLayoutValidationAssertionRecord Assertion;
			Assertion.AssertionId = AssertionId;
			Assertion.AssertionKind = ELayoutValidationAssertionKind::RequestContractValid;
			Assertion.bPassed = bPassed;
			Assertion.RelatedIds = RelatedIds;
			Assertion.FailureReason = FailureReason;
			return Assertion;
		}

		static FString BuildRootRequestBreadcrumb(const FLayoutRegionSolveRequest& Request)
		{
			return FString::Printf(
				TEXT(" Request '%s' (%s)."),
				*Request.RegionDebugPath,
				*Request.EffectiveSnapshotId.ToString());
		}

		static FString BuildProfileSnapshotBreadcrumb(const FLayoutProfileSolveSnapshot& ProfileSnapshot)
		{
			const FString LiveProfileBreadcrumb =
				ProfileSnapshot.SourceProfile != nullptr
					? FString::Printf(TEXT(" Profile: %s."), *ProfileSnapshot.SourceProfile->GetPathName())
					: FString();
			return FString::Printf(
				TEXT(" Frozen profile snapshot %s (%s).%s"),
				*ProfileSnapshot.DebugName.ToString(),
				*ProfileSnapshot.SnapshotId.ToString(),
				*LiveProfileBreadcrumb);
		}

		static FString BuildContentSetSnapshotBreadcrumb(const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot)
		{
			const FString LiveContentSetBreadcrumb =
				ContentSetSnapshot.SourceContentSet != nullptr
					? FString::Printf(TEXT(" ContentSet: %s."), *ContentSetSnapshot.SourceContentSet->GetPathName())
					: FString();
			return FString::Printf(
				TEXT(" Content-set snapshot %s (%s).%s"),
				*ContentSetSnapshot.DebugName.ToString(),
				*ContentSetSnapshot.SnapshotId.ToString(),
				*LiveContentSetBreadcrumb);
		}

		static FString BuildModuleCatalogBreadcrumb(const FLayoutModuleCatalog& ModuleCatalog)
		{
			return FString::Printf(
				TEXT(" Module-set snapshot %s (%s).%s"),
				*ModuleCatalog.DebugName.ToString(),
				*ModuleCatalog.SnapshotId.ToString(),
				*FString());
		}

		static void UpsertRequestValidationAssertion(
			FLayoutRegionSolveRequest& InOutRootRequest,
			const FLayoutValidationAssertionRecord& Assertion)
		{
			FLayoutValidationAssertionRecord* ExistingAssertion =
				InOutRootRequest.ValidationAssertions.FindByPredicate(
					[&Assertion](const FLayoutValidationAssertionRecord& Candidate)
					{
						return Candidate.AssertionId == Assertion.AssertionId;
					});
			if (ExistingAssertion != nullptr)
			{
				*ExistingAssertion = Assertion;
				return;
			}

			InOutRootRequest.ValidationAssertions.Add(Assertion);
		}

		static const FLayoutValidationAssertionRecord* FindFailedBlockingRequestContractAssertion(
			const FLayoutRegionSolveRequest& Request)
		{
			static const FLayoutId WorldPlacementLatticeAssertionId =
				TEXT("RegionRequest.WorldPlacementLatticeContractValid");
			static const FLayoutId ForcedInsertionAssertionId =
				TEXT("RegionRequest.ForcedPlacementBundleInsertionContractValid");
			static const FLayoutId RequiredRouteAssertionId =
				TEXT("RegionRequest.RequiredRouteConstraintContractValid");
			static const FLayoutId SteppedEnrichmentAssertionId =
				TEXT("RegionRequest.SteppedTerrainEnrichmentContractValid");

			for (const FLayoutId AssertionId : {
				WorldPlacementLatticeAssertionId,
				ForcedInsertionAssertionId,
				RequiredRouteAssertionId,
				SteppedEnrichmentAssertionId})
			{
				const FLayoutValidationAssertionRecord* FailedAssertion =
					Request.ValidationAssertions.FindByPredicate(
						[AssertionId](const FLayoutValidationAssertionRecord& Assertion)
						{
							return Assertion.AssertionId == AssertionId
								&& !Assertion.bPassed;
						});
				if (FailedAssertion != nullptr)
				{
					return FailedAssertion;
				}
			}

			return nullptr;
		}

		static FIntVector ResolveRequestSharedCellSizeInBlocks(FLayoutRegionSolveRequest& InOutRootRequest)
		{
			const FIntVector ContentSetSharedCellSize = InOutRootRequest.ContentSetSnapshot.SharedCellSizeInBlocks;
			const FIntVector ResolvedCatalogSharedCellSize = InOutRootRequest.ModuleCatalog.SharedCellSizeInBlocks;
			UpsertRequestValidationAssertion(InOutRootRequest, MakeRequestValidationAssertion(
				TEXT("RegionRequest.SharedCellSizeContractValid"),
				true,
				{
					InOutRootRequest.ContentSetSnapshot.SnapshotId,
					InOutRootRequest.ModuleCatalog.SnapshotId
				}));
			if (ContentSetSharedCellSize != FIntVector::ZeroValue)
			{
				return ContentSetSharedCellSize;
			}

			return ResolvedCatalogSharedCellSize;
		}

		static void ValidateRequestRootPublicationIdentity(FLayoutRegionSolveRequest& InOutRootRequest)
		{
			const bool bHasAnyPublicationIdentity =
				InOutRootRequest.RootPlacementPolicyId != NAME_None
				|| InOutRootRequest.RootCandidateId != NAME_None
				|| InOutRootRequest.RootSolveId != NAME_None;
			const bool bPassed =
				!bHasAnyPublicationIdentity
				|| (InOutRootRequest.RootCandidateId != NAME_None
					&& InOutRootRequest.RootSolveId != NAME_None);
			if (bPassed)
			{
				return;
			}

			TArray<FLayoutId> RelatedIds;
			if (InOutRootRequest.RootPlacementPolicyId != NAME_None)
			{
				RelatedIds.Add(InOutRootRequest.RootPlacementPolicyId);
			}
			if (InOutRootRequest.RootCandidateId != NAME_None)
			{
				RelatedIds.Add(InOutRootRequest.RootCandidateId);
			}
			if (InOutRootRequest.RootSolveId != NAME_None)
			{
				RelatedIds.Add(InOutRootRequest.RootSolveId);
			}

			UpsertRequestValidationAssertion(
				InOutRootRequest,
				MakeRequestValidationAssertion(
					TEXT("RegionRequest.RootPublicationIdentityContractValid"),
					false,
					RelatedIds,
					FString::Printf(
						TEXT("Region solve request carries incomplete root publication identity.%s Requests that specify root publication or world-binding identity must carry both RootCandidateId and RootSolveId before recursive scheduling begins."),
						*BuildRootRequestBreadcrumb(InOutRootRequest))));
		}

		static int32 NormalizeRequestTemplatePlacementOffsetBlocks(
			FLayoutRegionSolveRequest& InOutRootRequest,
			const FIntVector& SharedCellSizeInBlocks)
		{
			if (InOutRootRequest.TemplatePlacementZOffsetBlocks == 0)
			{
				return 0;
			}

			if (SharedCellSizeInBlocks != FIntVector::ZeroValue)
			{
				return InOutRootRequest.TemplatePlacementZOffsetBlocks;
			}

			UpsertRequestValidationAssertion(
				InOutRootRequest,
				MakeRequestValidationAssertion(
					TEXT("RegionRequest.TemplatePlacementOffsetContractValid"),
					false,
					{
						InOutRootRequest.ContentSetSnapshot.SnapshotId,
						InOutRootRequest.ModuleCatalog.SnapshotId
					},
					FString::Printf(
						TEXT("Region solve request carries TemplatePlacementZOffsetBlocks=%d without a usable shared cell-size contract.%s%s%s Request-owned placement offsets require one usable request shared-cell metric before recursive scheduling begins."),
						InOutRootRequest.TemplatePlacementZOffsetBlocks,
						*BuildRootRequestBreadcrumb(InOutRootRequest),
						*BuildContentSetSnapshotBreadcrumb(InOutRootRequest.ContentSetSnapshot),
						*BuildModuleCatalogBreadcrumb(InOutRootRequest.ModuleCatalog))));
			InOutRootRequest.TemplatePlacementZOffsetBlocks = 0;
			return 0;
		}

		/**
		 * Request validation owner for the first coordinator-side stepped-solve
		 * gate. The recursive scheduler may not continue into negotiation/proof
		 * once the frozen stepped root contract already proves that terrain-driven
		 * transitions cannot be satisfied by the compiled root-side
		 * VerticalAccess structure.
		 */
		static FTerrainSteppedPreparedSolveContract ValidateRootTerrainSteppedPreparedSolveContract(
			FLayoutRegionSolveRequest& InOutRootRequest,
			const FCompiledStructuralInputs& StructuralInputs)
		{
			FTerrainSteppedPreparedSolveContract PreparedContract;
			const bool bSteppedSolveExpected =
				InOutRootRequest.RootPlacementKind != ELayoutWorldBindingPlacementKind::None
				&& InOutRootRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve;
			if (!bSteppedSolveExpected)
			{
				return PreparedContract;
			}

			PreparedContract = BuildRootTerrainSteppedPreparedSolveContract(StructuralInputs);
			TArray<FLayoutId> RelatedIds;
			if (InOutRootRequest.ProfileSnapshot.SnapshotId != NAME_None)
			{
				RelatedIds.Add(InOutRootRequest.ProfileSnapshot.SnapshotId);
			}
			if (InOutRootRequest.ContentSetSnapshot.SnapshotId != NAME_None)
			{
				RelatedIds.Add(InOutRootRequest.ContentSetSnapshot.SnapshotId);
			}
			if (InOutRootRequest.ModuleCatalog.SnapshotId != NAME_None)
			{
				RelatedIds.Add(InOutRootRequest.ModuleCatalog.SnapshotId);
			}

			UpsertRequestValidationAssertion(
				InOutRootRequest,
				MakeRequestValidationAssertion(
					TEXT("RegionRequest.RootTerrainSteppedPreparedSolveContractValid"),
					PreparedContract.bRootCanSatisfyTerrainSteppedTransitions,
					RelatedIds,
					PreparedContract.bRootCanSatisfyTerrainSteppedTransitions
						? FString()
						: [&InOutRootRequest, &PreparedContract]()
						{
							const FString SnapshotBreadcrumbs =
								BuildContentSetSnapshotBreadcrumb(InOutRootRequest.ContentSetSnapshot)
								+ BuildModuleCatalogBreadcrumb(InOutRootRequest.ModuleCatalog);
							return FString::Printf(
								TEXT("%s%s%s%s"),
								*PreparedContract.RejectionSummary.FailureReason,
								*BuildRootRequestBreadcrumb(InOutRootRequest),
								*BuildProfileSnapshotBreadcrumb(InOutRootRequest.ProfileSnapshot),
								*SnapshotBreadcrumbs);
						}()));
			return PreparedContract;
		}

		static TSet<ELayoutFaceDirection> CollectVerticalAccessIngressDirections(
			const FLayoutModuleCatalog& ModuleCatalog)
		{
			TSet<ELayoutFaceDirection> Result;
			for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : ModuleCatalog.Modules)
			{
				if (!ModuleSnapshot.SupportsIntent(ELayoutCellIntent::VerticalAccess))
				{
					continue;
				}

				for (const FLayoutDerivedVerticalAccessContract& Contract : ModuleSnapshot.DerivedVerticalAccessContracts)
				{
					for (const FLayoutDerivedEndpointOffer& Offer : ModuleSnapshot.DerivedEndpointOffers)
					{
						if (Offer.LocalCell != Contract.LocalCell
							|| Offer.FaceDirection == ELayoutFaceDirection::PosZ
							|| Offer.FaceDirection == ELayoutFaceDirection::NegZ)
						{
							continue;
						}

						for (const FGameplayTag& TraversalChannel : Offer.TraversalChannels)
						{
							if (TraversalChannel.IsValid()
								&& Contract.SourceTraversalChannels.HasTagExact(TraversalChannel))
							{
								Result.Add(Offer.FaceDirection);
								break;
							}
						}
					}
				}
			}

			return Result;
		}

		static void TryPopulateParentPlanFromIndexedSnapshot(
			const FLayoutRegionSolveRequest& RootRequest,
			FRecursiveScheduleSolveContext& InOutContext)
		{
			const FLayoutIndexedDomainSnapshot IndexedSnapshot =
				FLayoutProfileSolver::BuildIndexedDomainSnapshot(RootRequest);
			if (!IndexedSnapshot.bSucceeded || IndexedSnapshot.CellDomains.IsEmpty())
			{
				return;
			}

			int32 MaxCellX = 0;
			int32 MaxCellY = 0;
			InOutContext.ParentPlannedCells.Reserve(IndexedSnapshot.CellDomains.Num());
			for (const FLayoutIndexedCellDomain& CellDomain : IndexedSnapshot.CellDomains)
			{
				FLayoutPlannedCell& PlannedCell = InOutContext.ParentPlannedCells.AddDefaulted_GetRef();
				PlannedCell.Cell = CellDomain.Cell;
				PlannedCell.Intent = CellDomain.Intent;
				MaxCellX = FMath::Max(MaxCellX, CellDomain.Cell.X);
				MaxCellY = FMath::Max(MaxCellY, CellDomain.Cell.Y);
			}

			InOutContext.ParentFootprintSize = FIntPoint(MaxCellX + 1, MaxCellY + 1);
		}

		static void CollectProtectedParentTraversalCells(
			const TArray<FLayoutPlannedCell>& ParentPlannedCells,
			const TSet<ELayoutFaceDirection>& ParentVerticalAccessIngressDirections,
			TSet<FIntVector>& InOutProtectedTraversalCells)
		{
			TMap<FIntVector, ELayoutCellIntent> ParentPlannedCellIntents;
			for (const FLayoutPlannedCell& PlannedCell : ParentPlannedCells)
			{
				ParentPlannedCellIntents.Add(PlannedCell.Cell, PlannedCell.Intent);
			}

			static const ELayoutFaceDirection HorizontalDirections[] =
			{
				ELayoutFaceDirection::PosX,
				ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY,
				ELayoutFaceDirection::NegY
			};

			for (const FLayoutPlannedCell& PlannedCell : ParentPlannedCells)
			{
				if (PlannedCell.Intent != ELayoutCellIntent::Entry)
				{
					continue;
				}

				for (const ELayoutFaceDirection Direction : HorizontalDirections)
				{
					const FIntVector NeighborCell =
						PlannedCell.Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
					const ELayoutCellIntent* NeighborIntent = ParentPlannedCellIntents.Find(NeighborCell);
					if (NeighborIntent != nullptr && IsProtectedParentTraversalIntent(*NeighborIntent))
					{
						InOutProtectedTraversalCells.Add(NeighborCell);
					}
				}
			}

			for (const FLayoutPlannedCell& PlannedCell : ParentPlannedCells)
			{
				if (PlannedCell.Intent != ELayoutCellIntent::VerticalAccess)
				{
					continue;
				}

				for (const ELayoutFaceDirection Direction : ParentVerticalAccessIngressDirections)
				{
					if (Direction == ELayoutFaceDirection::PosZ
						|| Direction == ELayoutFaceDirection::NegZ)
					{
						continue;
					}

					const FIntVector NeighborCell =
						PlannedCell.Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
					const ELayoutCellIntent* NeighborIntent = ParentPlannedCellIntents.Find(NeighborCell);
					if (NeighborIntent != nullptr && IsProtectedParentTraversalIntent(*NeighborIntent))
					{
						InOutProtectedTraversalCells.Add(NeighborCell);
					}
				}
			}
		}

		static FLayoutRegionSolveScheduleResult BuildSingleRegionScheduleResult(
			const FLayoutRegionSolveRequest& RootRequest,
			const FLayoutRegionSolveResult& RootResult)
		{
			FLayoutRegionSolveScheduleResult ScheduleResult;
			ScheduleResult.bSucceeded = RootResult.SolveResult.bSucceeded;
			ScheduleResult.RegionResults.Add(RootResult);
			ScheduleResult.MergedSolveResult = RootResult.SolveResult;
			ScheduleResult.MergedSolveResult.RootPlacementKind = RootRequest.bHasSelectedModePlan
				? RootRequest.SelectedModePlan.PlacementKind
				: RootRequest.RootPlacementKind;
			ScheduleResult.MergedSolveResult.WorldBindingPlacementPolicy = RootRequest.bHasSelectedModePlan
				? RootRequest.SelectedModePlan.PlacementPolicy
				: RootRequest.WorldBindingPlacementPolicy;
			ScheduleResult.MergedSolveResult.ValidationAssertions =
				RootResult.ValidationAssertions;
			// Single-region schedules still need to publish the root region's stepped
			// request/diagnostic surface on the merged result; those carriers live on
			// the region result, not on the nested plain solve payload.
			ScheduleResult.MergedSolveResult.SteppedTerrainSupportMap =
				RootResult.SteppedTerrainSupportMap;
			ScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions =
				RootResult.ForcedPlacementBundleInsertions;
			ScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints =
				RootResult.RequiredRouteConstraints;
			ScheduleResult.MergedSolveResult.SteppedTerrainTransitionDiagnostics =
				RootResult.SteppedTerrainTransitionDiagnostics;
			ScheduleResult.MergedSolveResult.UnsupportedSteppedTerrainTransitionDiagnostics =
				RootResult.UnsupportedSteppedTerrainTransitionDiagnostics;
			ScheduleResult.MergedSolveResult.bCanSatisfySteppedTerrainTransitions =
				RootResult.bCanSatisfySteppedTerrainTransitions;
			ScheduleResult.FailureReason = RootResult.SolveResult.FailureReason;
			return ScheduleResult;
		}

		static void AppendCapabilityFailureMessage(
			FLayoutSolveResult& InOutSolveResult,
			const FString& FailureReason)
		{
			if (FailureReason.IsEmpty())
			{
				return;
			}

			// Coordinator-owned failure results already publish the canonical
			// payload through SolveResult.FailureReason. Avoid echoing the same
			// string again as a merged validation message when no extra detail
			// is being added.
			if (InOutSolveResult.FailureReason == FailureReason)
			{
				return;
			}

			const bool bAlreadyPresent = InOutSolveResult.Messages.ContainsByPredicate(
				[&FailureReason](const FLayoutValidationMessage& Message)
				{
					return Message.Message == FailureReason;
				});
			if (bAlreadyPresent)
			{
				return;
			}

			FLayoutValidationMessage& Message =
				InOutSolveResult.Messages.AddDefaulted_GetRef();
			Message.Severity = ELayoutValidationSeverity::Error;
			Message.Message = FailureReason;
		}

		static FLayoutRegionSolveScheduleResult BuildCapabilityBackedFailureScheduleResult(
			const FLayoutRegionSolveRequest& RootRequest,
			const FString& FailureReason)
		{
			FLayoutRegionSolveResult RootResult =
				LayoutProfileSolverInternal::BuildRequestBackedRegionPreparedTopology(
					RootRequest);
			// Coordinator failure owns topology diagnostics only. Running an
			// unconstrained root proof here would publish parent-only placements
			// as though they belonged to the rejected child-aware branch.
			RootResult.SolveResult.Placements.Reset();
			RootResult.SolveResult.ZoneFeatureProviderCommitments.Reset();
			RootResult.SolveResult.PartitionSeams.Reset();
			RootResult.SolveResult.RejectedPreviewStage =
				ELayoutRejectedPreviewStage::PreparedTopology;
			// Contract failures can occur before CSP creates a normal solve
			// surface. Preserve request-owned planned intents so editor previews
			// can still render the rejected grid.
			if (!RootRequest.PlannedCells.IsEmpty())
			{
				RootResult.SolveResult.PlannedCells = RootRequest.PlannedCells;
			}
			if (RootResult.SolveResult.FootprintSize == FIntPoint::ZeroValue)
			{
				RootResult.SolveResult.FootprintSize = RootRequest.FootprintSize;
			}
			if (RootResult.SolveResult.SharedCellSizeInBlocks == FIntVector::ZeroValue)
			{
				RootResult.SolveResult.SharedCellSizeInBlocks = RootRequest.ModuleCatalog.SharedCellSizeInBlocks;
			}
			RootResult.SolveResult.RootPlacementKind = RootRequest.bHasSelectedModePlan
				? RootRequest.SelectedModePlan.PlacementKind
				: RootRequest.RootPlacementKind;
			RootResult.SolveResult.WorldBindingPlacementPolicy = RootRequest.bHasSelectedModePlan
				? RootRequest.SelectedModePlan.PlacementPolicy
				: RootRequest.WorldBindingPlacementPolicy;
			RootResult.SolveResult.SteppedTerrainSupportMap = RootRequest.SteppedTerrainSupportMap;
			FLayoutRegionSolveScheduleResult ScheduleResult =
				BuildSingleRegionScheduleResult(RootRequest, RootResult);
			ScheduleResult.bSucceeded = false;
			ScheduleResult.FailureReason = FailureReason.IsEmpty()
				? RootResult.SolveResult.FailureReason
				: FailureReason;
			ScheduleResult.MergedSolveResult.bSucceeded = false;
			ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
			AppendCapabilityFailureMessage(
				ScheduleResult.MergedSolveResult,
				ScheduleResult.FailureReason);
			if (!ScheduleResult.RegionResults.IsEmpty())
			{
				ScheduleResult.RegionResults[0].SolveResult.bSucceeded = false;
				ScheduleResult.RegionResults[0].SolveResult.FailureReason =
					ScheduleResult.FailureReason;
				AppendCapabilityFailureMessage(
					ScheduleResult.RegionResults[0].SolveResult,
					ScheduleResult.FailureReason);
			}
			return ScheduleResult;
		}

		static bool IsDemandActiveForCurrentTerrainStage(
			const FNegotiationDemandPlan& DemandPlan)
		{
			return LayoutRegionScheduleSolverFacade::IsNegotiationDemandActiveForCurrentTerrainStage(DemandPlan);
		}

		static bool AreDemandsEligibleForRequiredPlacementBridge(
			const FCompiledStructuralInputs& StructuralInputs)
		{
			for (const FNegotiationDemandPlan& DemandPlan : StructuralInputs.Demands)
			{
				if (IsDemandActiveForCurrentTerrainStage(DemandPlan))
				{
					return true;
				}
			}
			return false;
		}

		static bool DoesRequiredPlacementBridgeMatchCompiledDemandSet(
			const FCompiledStructuralInputs& StructuralInputs,
			const TArray<FNegotiatedDemandResult>& BridgedDemandResults,
			FString& OutFailureReason)
		{
			OutFailureReason.Reset();
			// Local project fix: counted choice slots may settle through another
			// child source or residual parent modules, while required authored children may not disappear.
			TSet<FString> ActiveChildRegionPaths;
			TSet<FString> RequiredChildRegionPaths;
			for (const FNegotiationDemandPlan& DemandPlan : StructuralInputs.Demands)
			{
				if (!IsDemandActiveForCurrentTerrainStage(DemandPlan))
				{
					continue;
				}
				ActiveChildRegionPaths.Add(DemandPlan.ChildRegionDebugPath);
				if (!DemandPlan.FeatureFlags.bOptionalDemand
					&& !DemandPlan.FeatureFlags.bCountedProviderChoiceSlot)
				{
					RequiredChildRegionPaths.Add(DemandPlan.ChildRegionDebugPath);
				}
			}

			for (const FNegotiatedDemandResult& DemandResult : BridgedDemandResults)
			{
				if (!ActiveChildRegionPaths.Contains(DemandResult.ChildRegionDebugPath))
				{
					OutFailureReason = FString::Printf(
						TEXT("Coordinator required-demand placement bridge returned unexpected child region '%s'."),
						*DemandResult.ChildRegionDebugPath);
					return false;
				}
				RequiredChildRegionPaths.Remove(DemandResult.ChildRegionDebugPath);
			}

			if (!RequiredChildRegionPaths.IsEmpty())
			{
				const FString MissingChildRegionPath = *RequiredChildRegionPaths.CreateConstIterator();
				OutFailureReason = FString::Printf(
					TEXT("Coordinator required-demand placement bridge did not produce a required demand result for child region '%s'."),
					*MissingChildRegionPath);
				return false;
			}

			return true;
		}
	}

	FRecursiveScheduleSolveContext BuildSolveContext(const FLayoutRegionSolveRequest& RootRequest)
	{
		FRecursiveScheduleSolveContext Context;
		Context.RootRequest = RootRequest;
		Context.RootExternalEndpointCommitments = RootRequest.CommittedEndpointAnchors;
		Context.SeedStreams.ChildDemandSeed = BuildStageSeed(RootRequest.Seed, TEXT("ChildDemand"));
		Context.SeedStreams.ContactFamilySeed = BuildStageSeed(RootRequest.Seed, TEXT("ContactFamily"));
		Context.SeedStreams.ConfirmationSeed = BuildStageSeed(RootRequest.Seed, TEXT("Confirmation"));
		Context.SeedStreams.SeamAndOptionalSeed = BuildStageSeed(RootRequest.Seed, TEXT("SeamAndOptional"));
		Context.MaxConcurrentChildProofJobs = FMath::Max(1, RootRequest.ExecutionSettings.MaxConcurrentChildProofJobs);
		ValidateRequestRootPublicationIdentity(Context.RootRequest);
		Context.PublicationMetadata.RootSolveId =
			RootRequest.RootSolveId != NAME_None
				? RootRequest.RootSolveId
				: (RootRequest.EffectiveSnapshotId != NAME_None
					? RootRequest.EffectiveSnapshotId
					: FLayoutId(*RootRequest.RegionDebugPath));
		Context.PublicationMetadata.RootCandidateId =
			RootRequest.RootCandidateId != NAME_None
				? RootRequest.RootCandidateId
				: RootRequest.SourceContentEntryId;
		Context.PublicationMetadata.RootPlacementPolicyId = RootRequest.RootPlacementPolicyId;
		Context.PublicationMetadata.RootContinuationSelection = RootRequest.bHasSelectedModePlan
			? RootRequest.SelectedModePlan.ContinuationSelection
			: RootRequest.RootContinuationSelection;
		Context.SharedCellSizeInBlocks = ResolveRequestSharedCellSizeInBlocks(Context.RootRequest);
		Context.TemplatePlacementZOffsetBlocks =
			NormalizeRequestTemplatePlacementOffsetBlocks(
				Context.RootRequest,
				Context.SharedCellSizeInBlocks);
		Context.RootPlacementKind = RootRequest.bHasSelectedModePlan
			? RootRequest.SelectedModePlan.PlacementKind
			: RootRequest.RootPlacementKind;
		Context.WorldBindingPlacementPolicy = RootRequest.bHasSelectedModePlan
			? RootRequest.SelectedModePlan.PlacementPolicy
			: RootRequest.WorldBindingPlacementPolicy;
		Context.RootRequest.RootPlacementKind = Context.RootPlacementKind;
		Context.RootRequest.WorldBindingPlacementPolicy = Context.WorldBindingPlacementPolicy;
		Context.SteppedTerrainSupportMap = RootRequest.SteppedTerrainSupportMap;
		Context.ParentVerticalAccessIngressDirections =
			CollectVerticalAccessIngressDirections(RootRequest.ModuleCatalog);
		if (!RootRequest.PlannedCells.IsEmpty())
		{
			// Terrain adaptation receives a complete authored plan. It may translate
			// cells and inject bridges, but must not regenerate authored intents.
			Context.ParentPlannedCells = RootRequest.PlannedCells;
			Context.ParentFootprintSize = RootRequest.FootprintSize;
			Context.RootRequest.PlannedCells = Context.ParentPlannedCells;
		}
		else
		{
			TryPopulateParentPlanFromIndexedSnapshot(Context.RootRequest, Context);
			Context.RootRequest.PlannedCells = Context.ParentPlannedCells;
		}
		CollectProtectedParentTraversalCells(
			Context.ParentPlannedCells,
			Context.ParentVerticalAccessIngressDirections,
			Context.ProtectedParentTraversalCells);
		for (const FLayoutCommittedTraversalAnchor& Anchor : RootRequest.CommittedTraversalAnchors)
		{
			Context.ProtectedParentTraversalCells.Add(Anchor.Cell);
		}
		for (const FIntVector& ProtectedCell : RootRequest.ProtectedStructuralCells)
		{
			Context.ProtectedParentTraversalCells.Add(ProtectedCell);
		}
		return Context;
	}

	FNegotiationFeatureFlags BuildNegotiationFeatureFlags(
		const FLayoutRegionSolveRequest& ChildRequest,
		const FChildCapabilitySummary& ChildSummary)
	{
		FNegotiationFeatureFlags Flags;
		switch (ChildRequest.ProfileSnapshot.EntryCountMode)
		{
		case ELayoutCountConstraintMode::Exact:
			Flags.bRequiresMultiEntryContactSet =
				ChildRequest.ProfileSnapshot.EntryCount > 1;
			break;
		case ELayoutCountConstraintMode::Range:
			Flags.bRequiresMultiEntryContactSet =
				ChildRequest.ProfileSnapshot.MaxEntryCount > 1;
			break;
		case ELayoutCountConstraintMode::None:
		default:
			Flags.bRequiresMultiEntryContactSet = false;
			break;
		}
		Flags.bAllowsChildTraversalBridge =
			ChildSummary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess ||
			ChildSummary.ResponsibilityParticipation.bSupportsComposedHostAccess;
		Flags.bRequiresMultiLevelAnchorSet = ChildRequest.ProfileSnapshot.LevelCount > 1;
		Flags.bRequiresSeamPlanning = ChildSummary.ResponsibilityParticipation.bSupportsSeamOwnership;
		return Flags;
	}

	FCapabilityBackedScheduleResult BuildCapabilityBackedNegotiatedProofContract(
		const FLayoutRegionSolveRequest& RootRequest)
	{
		FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
		FCapabilityBackedScheduleResult Result;
		Result.ProofContract.ParentRequest = SolveContext.RootRequest;
		Result.ProofContract.RootExternalEndpointCommitments =
			SolveContext.RootExternalEndpointCommitments;
		Result.ProofContract.MaxConcurrentChildProofJobs =
			SolveContext.MaxConcurrentChildProofJobs;
		Result.ProofContract.PublicationMetadata =
			SolveContext.PublicationMetadata;
		if (const FLayoutValidationAssertionRecord* FailedRootRequestAssertion =
			FindFailedBlockingRequestContractAssertion(SolveContext.RootRequest))
		{
			Result.FailureReason = FailedRootRequestAssertion->FailureReason.IsEmpty()
				? FString::Printf(
					TEXT("Coordinator rejected root request '%s' because blocking request-contract assertion '%s' failed before recursive scheduling began."),
					*SolveContext.RootRequest.RegionDebugPath,
					*FailedRootRequestAssertion->AssertionId.ToString())
				: FailedRootRequestAssertion->FailureReason;
			Result.ScheduleResult = BuildCapabilityBackedFailureScheduleResult(
				SolveContext.RootRequest,
				Result.FailureReason);
			ApplyRequestOwnedSolveSurfaceToMergedSolveResult(
				Result.ScheduleResult,
				SolveContext.RootRequest);
			return Result;
		}
		const FCompiledStructuralInputs StructuralInputs =
			BuildCompiledStructuralInputs(SolveContext);
		const FTerrainSteppedPreparedSolveContract RootTerrainSteppedPreparedSolveContract =
			ValidateRootTerrainSteppedPreparedSolveContract(
				SolveContext.RootRequest,
				StructuralInputs);
		const bool bRootSteppedSolveExpected =
			SolveContext.RootRequest.RootPlacementKind != ELayoutWorldBindingPlacementKind::None
			&& SolveContext.RootRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve;
		if (bRootSteppedSolveExpected
			&& !RootTerrainSteppedPreparedSolveContract.bRootCanSatisfyTerrainSteppedTransitions)
		{
			FLayoutRegionSolveRequest FailureRequest = SolveContext.RootRequest;
			FailureRequest.RequiredRouteConstraints =
				RootTerrainSteppedPreparedSolveContract.RouteConstraints;
			LayoutRegionRequestSnapshotBuilder::RefreshStandaloneRequiredRouteConstraintAssertions(
				FailureRequest);
			Result.FailureReason =
				RootTerrainSteppedPreparedSolveContract.RejectionSummary.FailureReason.IsEmpty()
				? TEXT("Root stepped-terrain solve cannot continue because the prepared root support contract is incomplete.")
				: RootTerrainSteppedPreparedSolveContract.RejectionSummary.FailureReason;
			Result.ScheduleResult = BuildCapabilityBackedFailureScheduleResult(
				FailureRequest,
				Result.FailureReason);
			ApplySteppedTerrainTransitionDiagnosticsToScheduleResult(
				Result.ScheduleResult,
				SolveContext.RootRequest.RegionDebugPath,
				StructuralInputs);
			ApplyRootSteppedDiagnosticsToMergedSolveResult(
				Result.ScheduleResult,
				SolveContext.RootRequest.RegionDebugPath);
			ApplyRequestOwnedSolveSurfaceToMergedSolveResult(
				Result.ScheduleResult,
				FailureRequest);
			return Result;
		}

		AppendTerrainSteppedRequiredTraversalCellsToProtectedContext(
			SolveContext,
			StructuralInputs);

		if (AreDemandsEligibleForRequiredPlacementBridge(StructuralInputs))
		{
			TArray<FNegotiatedDemandResult> PlacementBackedDemandResults;
			FLayoutRegionSolveRequest PlacementBackedParentProofRequest;
			FLayoutRecursiveVerticalAccessSummary PlacementBackedVerticalAccessSummary;
			FNegotiatedSeamPlan PlacementBackedSeamPlan;
			FString BridgeFailureReason;
			ELayoutSolvePreparationFailureKind BridgePreparationFailureKind =
				ELayoutSolvePreparationFailureKind::None;
			int32 BridgePreparationCandidateAttemptCount = 0;
			FLayoutRegionalFailureRecord BridgeRegionalFailure;
			if (!TryBuildRequiredPlacementBackedDemandResults(
					SolveContext.RootRequest,
					PlacementBackedDemandResults,
					&PlacementBackedParentProofRequest,
					BridgeFailureReason,
					&PlacementBackedVerticalAccessSummary,
					&PlacementBackedSeamPlan,
					&BridgePreparationFailureKind,
					&BridgePreparationCandidateAttemptCount,
					&BridgeRegionalFailure)
				|| !DoesRequiredPlacementBridgeMatchCompiledDemandSet(
					StructuralInputs,
					PlacementBackedDemandResults,
					BridgeFailureReason))
			{
				Result.FailureReason = BridgeFailureReason.IsEmpty()
					? TEXT("Coordinator required-demand placement bridge failed before proof execution.")
					: BridgeFailureReason;
				Result.ScheduleResult = BuildCapabilityBackedFailureScheduleResult(
					SolveContext.RootRequest,
					Result.FailureReason);
				ApplyRequestOwnedSolveSurfaceToMergedSolveResult(
					Result.ScheduleResult,
					SolveContext.RootRequest);
				Result.ScheduleResult.MergedSolveResult.PreparationFailureKind =
					BridgePreparationFailureKind;
				Result.ScheduleResult.MergedSolveResult.PropagationStats.CandidateAttemptCount =
					BridgePreparationCandidateAttemptCount;
				Result.ScheduleResult.MergedSolveResult.RegionalFailure =
					BridgeRegionalFailure;
				if (!Result.ScheduleResult.RegionResults.IsEmpty())
				{
					Result.ScheduleResult.RegionResults[0].SolveResult.PreparationFailureKind =
						BridgePreparationFailureKind;
					Result.ScheduleResult.RegionResults[0].SolveResult.PropagationStats.CandidateAttemptCount =
						BridgePreparationCandidateAttemptCount;
					Result.ScheduleResult.RegionResults[0].SolveResult.RegionalFailure =
						BridgeRegionalFailure;
				}
				return Result;
			}

			Result.ProofContract = BuildNegotiatedProofScheduleContract(
				SolveContext,
				PlacementBackedDemandResults,
				PlacementBackedSeamPlan,
				&RootTerrainSteppedPreparedSolveContract);
			if (!PlacementBackedParentProofRequest.RegionDebugPath.IsEmpty())
			{
				for (const FLayoutForcedPlacementBundleInsertion& Insertion :
					Result.ProofContract.ParentRequest.ForcedPlacementBundleInsertions)
				{
					AppendForcedPlacementBundleInsertionIfMissingOnPlacementBridge(
						PlacementBackedParentProofRequest.ForcedPlacementBundleInsertions,
						Insertion);
				}
				for (const FLayoutRouteConstraintRecord& RouteConstraint :
					Result.ProofContract.ParentRequest.RequiredRouteConstraints)
				{
					AppendRequiredRouteConstraintIfMissingOnPlacementBridge(
						PlacementBackedParentProofRequest.RequiredRouteConstraints,
						RouteConstraint);
				}
				for (const FIntVector& ProtectedCell :
					Result.ProofContract.ParentRequest.ProtectedStructuralCells)
				{
					AppendProtectedStructuralCellIfMissing(
						PlacementBackedParentProofRequest.ProtectedStructuralCells,
						ProtectedCell);
				}
				PlacementBackedParentProofRequest.ProtectedStructuralCells.Sort([](
					const FIntVector& Left,
					const FIntVector& Right)
				{
					return Left.Z != Right.Z
						? Left.Z < Right.Z
						: (Left.Y != Right.Y
							? Left.Y < Right.Y
							: Left.X < Right.X);
				});
				LayoutRegionRequestSnapshotBuilder::RefreshStandaloneForcedPlacementBundleInsertionAssertions(
					PlacementBackedParentProofRequest);
				LayoutRegionRequestSnapshotBuilder::RefreshStandaloneRequiredRouteConstraintAssertions(
					PlacementBackedParentProofRequest);
				Result.ProofContract.ParentRequest = PlacementBackedParentProofRequest;
				for (const FLayoutNegotiatedChildResponsibilityContract& ResponsibilityContract :
					Result.ProofContract.ResponsibilityContracts)
				{
					const bool bAlreadyPresent =
						Result.ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts.ContainsByPredicate(
							[&ResponsibilityContract](const FLayoutNegotiatedChildResponsibilityContract& ExistingContract)
							{
								return ExistingContract.ParentRegionDebugPath == ResponsibilityContract.ParentRegionDebugPath
									&& ExistingContract.ChildRegionDebugPath == ResponsibilityContract.ChildRegionDebugPath;
							});
					if (!bAlreadyPresent)
					{
						Result.ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts.Add(
							ResponsibilityContract);
					}
				}
				AppendNegotiatedContractProofDiagnosticsToRequest(
					Result.ProofContract.ParentRequest);
			}

			Result.ProofContract.PlannedRecursiveVerticalAccessSummary =
				PlacementBackedVerticalAccessSummary;
			if (const FLayoutValidationAssertionRecord* FailedParentRequestAssertion =
				FindFailedBlockingRequestContractAssertion(Result.ProofContract.ParentRequest))
			{
				Result.FailureReason = FailedParentRequestAssertion->FailureReason.IsEmpty()
					? FString::Printf(
						TEXT("Coordinator placement-backed proof request for '%s' failed request-contract assertion '%s'."),
						*SolveContext.RootRequest.RegionDebugPath,
						*FailedParentRequestAssertion->AssertionId.ToString())
					: FailedParentRequestAssertion->FailureReason;
				Result.ScheduleResult = BuildCapabilityBackedFailureScheduleResult(
					Result.ProofContract.ParentRequest,
					Result.FailureReason);
				ApplySteppedTerrainTransitionDiagnosticsToScheduleResult(
					Result.ScheduleResult,
					SolveContext.RootRequest.RegionDebugPath,
					StructuralInputs);
				ApplyRootSteppedDiagnosticsToMergedSolveResult(
					Result.ScheduleResult,
					SolveContext.RootRequest.RegionDebugPath);
				ApplyRequestOwnedSolveSurfaceToMergedSolveResult(
					Result.ScheduleResult,
					Result.ProofContract.ParentRequest);
				return Result;
			}

			Result.bSucceeded = true;
			return Result;
		}

		TArray<FNegotiatedDemandResult> NegotiatedDemandResults;
		NegotiatedDemandResults.Reserve(StructuralInputs.Demands.Num());

		FString FirstRequiredDemandFailureReason;
		for (const FNegotiationDemandPlan& DemandPlan : StructuralInputs.Demands)
		{
			if (!IsDemandActiveForCurrentTerrainStage(DemandPlan))
			{
				continue;
			}

			const FResidualParentCapabilitySummary ParentSummary =
				RebuildResidualParentSummaryForDemand(StructuralInputs, DemandPlan);
			FNegotiatedDemandResult DemandResult =
				NegotiateDemandResponsibilities(
					SolveContext,
					DemandPlan,
					ParentSummary);
			if (!DemandResult.bSucceeded
				&& !DemandResult.ChildRequest.bSourceContentEntryOptional
				&& FirstRequiredDemandFailureReason.IsEmpty())
			{
				FirstRequiredDemandFailureReason = DemandResult.FailureReason.IsEmpty()
					? FString::Printf(
						TEXT("Negotiation failed for required child region '%s'."),
						*DemandPlan.ChildRegionDebugPath)
					: DemandResult.FailureReason;
			}

			NegotiatedDemandResults.Add(MoveTemp(DemandResult));
		}

		if (!FirstRequiredDemandFailureReason.IsEmpty())
		{
			Result.FailureReason = FirstRequiredDemandFailureReason;
			Result.ScheduleResult = BuildCapabilityBackedFailureScheduleResult(
				SolveContext.RootRequest,
				Result.FailureReason);
			ApplySteppedTerrainTransitionDiagnosticsToScheduleResult(
				Result.ScheduleResult,
				SolveContext.RootRequest.RegionDebugPath,
				StructuralInputs);
			ApplyRootSteppedDiagnosticsToMergedSolveResult(
				Result.ScheduleResult,
				SolveContext.RootRequest.RegionDebugPath);
			ApplyRequestOwnedSolveSurfaceToMergedSolveResult(
				Result.ScheduleResult,
				SolveContext.RootRequest);
			return Result;
		}

		FNegotiatedSeamPlan EmptyCommittedSeamPlan;
		EmptyCommittedSeamPlan.bPlannedPartitionSeamsAreAuthoritative = true;
		Result.ProofContract = BuildNegotiatedProofScheduleContract(
			SolveContext,
			NegotiatedDemandResults,
			EmptyCommittedSeamPlan,
			&RootTerrainSteppedPreparedSolveContract);
		if (const FLayoutValidationAssertionRecord* FailedParentRequestAssertion =
			FindFailedBlockingRequestContractAssertion(Result.ProofContract.ParentRequest))
		{
			Result.FailureReason = FailedParentRequestAssertion->FailureReason.IsEmpty()
				? FString::Printf(
					TEXT("Coordinator built an invalid parent proof request for '%s' because request-contract assertion '%s' failed after carrier mapping."),
					*SolveContext.RootRequest.RegionDebugPath,
					*FailedParentRequestAssertion->AssertionId.ToString())
				: FailedParentRequestAssertion->FailureReason;
			Result.ScheduleResult = BuildCapabilityBackedFailureScheduleResult(
				Result.ProofContract.ParentRequest,
				Result.FailureReason);
			ApplySteppedTerrainTransitionDiagnosticsToScheduleResult(
				Result.ScheduleResult,
				SolveContext.RootRequest.RegionDebugPath,
				StructuralInputs);
			ApplyRootSteppedDiagnosticsToMergedSolveResult(
				Result.ScheduleResult,
				SolveContext.RootRequest.RegionDebugPath);
			ApplyRequestOwnedSolveSurfaceToMergedSolveResult(
				Result.ScheduleResult,
				Result.ProofContract.ParentRequest);
			return Result;
		}

		Result.bSucceeded = true;
		return Result;
	}

	FCapabilityBackedScheduleResult SolveRegionTreeWithCapabilityBackedNegotiation(
		const FLayoutRegionSolveRequest& RootRequest)
	{
		FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
		FCapabilityBackedScheduleResult Result;
		Result.ProofContract.ParentRequest = SolveContext.RootRequest;
		Result.ProofContract.RootExternalEndpointCommitments =
			SolveContext.RootExternalEndpointCommitments;
		Result.ProofContract.MaxConcurrentChildProofJobs =
			SolveContext.MaxConcurrentChildProofJobs;
		Result.ProofContract.PublicationMetadata =
			SolveContext.PublicationMetadata;
		if (const FLayoutValidationAssertionRecord* FailedRootRequestAssertion =
			FindFailedBlockingRequestContractAssertion(SolveContext.RootRequest))
		{
			Result.FailureReason = FailedRootRequestAssertion->FailureReason.IsEmpty()
				? FString::Printf(
					TEXT("Coordinator rejected root request '%s' because blocking request-contract assertion '%s' failed before recursive scheduling began."),
					*SolveContext.RootRequest.RegionDebugPath,
					*FailedRootRequestAssertion->AssertionId.ToString())
				: FailedRootRequestAssertion->FailureReason;
			Result.ScheduleResult = BuildCapabilityBackedFailureScheduleResult(
				SolveContext.RootRequest,
				Result.FailureReason);
			ApplyRequestOwnedSolveSurfaceToMergedSolveResult(
				Result.ScheduleResult,
				SolveContext.RootRequest);
			return Result;
		}
		const FCompiledStructuralInputs StructuralInputs =
			BuildCompiledStructuralInputs(SolveContext);
		const FTerrainSteppedPreparedSolveContract RootTerrainSteppedPreparedSolveContract =
			ValidateRootTerrainSteppedPreparedSolveContract(
				SolveContext.RootRequest,
				StructuralInputs);
		const bool bRootSteppedSolveExpected =
			SolveContext.RootRequest.RootPlacementKind != ELayoutWorldBindingPlacementKind::None
			&& SolveContext.RootRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve;
		if (bRootSteppedSolveExpected
			&& !RootTerrainSteppedPreparedSolveContract.bRootCanSatisfyTerrainSteppedTransitions)
		{
			FLayoutRegionSolveRequest FailureRequest = SolveContext.RootRequest;
			FailureRequest.RequiredRouteConstraints =
				RootTerrainSteppedPreparedSolveContract.RouteConstraints;
			LayoutRegionRequestSnapshotBuilder::RefreshStandaloneRequiredRouteConstraintAssertions(
				FailureRequest);
			Result.FailureReason =
				RootTerrainSteppedPreparedSolveContract.RejectionSummary.FailureReason.IsEmpty()
				? TEXT("Root stepped-terrain solve cannot continue because the prepared root support contract is incomplete.")
				: RootTerrainSteppedPreparedSolveContract.RejectionSummary.FailureReason;
			Result.ScheduleResult = BuildCapabilityBackedFailureScheduleResult(
				FailureRequest,
				Result.FailureReason);
			ApplySteppedTerrainTransitionDiagnosticsToScheduleResult(
				Result.ScheduleResult,
				SolveContext.RootRequest.RegionDebugPath,
				StructuralInputs);
			ApplyRootSteppedDiagnosticsToMergedSolveResult(
				Result.ScheduleResult,
				SolveContext.RootRequest.RegionDebugPath);
			ApplyRequestOwnedSolveSurfaceToMergedSolveResult(
				Result.ScheduleResult,
				FailureRequest);
			return Result;
		}

		AppendTerrainSteppedRequiredTraversalCellsToProtectedContext(
			SolveContext,
			StructuralInputs);

		if (AreDemandsEligibleForRequiredPlacementBridge(StructuralInputs))
		{
			TArray<FNegotiatedDemandResult> PlacementBackedDemandResults;
			FLayoutRegionSolveRequest PlacementBackedParentProofRequest;
			FLayoutRecursiveVerticalAccessSummary PlacementBackedVerticalAccessSummary;
			FNegotiatedSeamPlan PlacementBackedSeamPlan;
			FString BridgeFailureReason;
			ELayoutSolvePreparationFailureKind BridgePreparationFailureKind =
				ELayoutSolvePreparationFailureKind::None;
			int32 BridgePreparationCandidateAttemptCount = 0;
			FLayoutRegionalFailureRecord BridgeRegionalFailure;
			LayoutRegionScheduleSolverPrivate::FCertifiedRegionalProofResults
				PlacementBackedCertifiedRegionalProofResults;
			if (!TryBuildRequiredPlacementBackedDemandResults(
					SolveContext.RootRequest,
					PlacementBackedDemandResults,
					&PlacementBackedParentProofRequest,
					BridgeFailureReason,
					&PlacementBackedVerticalAccessSummary,
					&PlacementBackedSeamPlan,
					&BridgePreparationFailureKind,
					&BridgePreparationCandidateAttemptCount,
					&BridgeRegionalFailure,
					&PlacementBackedCertifiedRegionalProofResults)
				|| !DoesRequiredPlacementBridgeMatchCompiledDemandSet(
					StructuralInputs,
					PlacementBackedDemandResults,
					BridgeFailureReason))
			{
				SolveContext.RootRequest.NegotiatedChildResponsibilityContracts =
					BuildNegotiatedResponsibilityContracts(PlacementBackedDemandResults);

				Result.FailureReason = BridgeFailureReason.IsEmpty()
					? TEXT("Coordinator required-demand placement bridge failed before proof execution.")
					: BridgeFailureReason;
				Result.ScheduleResult = BuildCapabilityBackedFailureScheduleResult(
					SolveContext.RootRequest,
					Result.FailureReason);
				ApplyRequestOwnedSolveSurfaceToMergedSolveResult(
					Result.ScheduleResult,
					SolveContext.RootRequest);
				Result.ScheduleResult.MergedSolveResult.PreparationFailureKind =
					BridgePreparationFailureKind;
				Result.ScheduleResult.MergedSolveResult.PropagationStats.CandidateAttemptCount =
					BridgePreparationCandidateAttemptCount;
				Result.ScheduleResult.MergedSolveResult.RegionalFailure =
					BridgeRegionalFailure;
				if (!Result.ScheduleResult.RegionResults.IsEmpty())
				{
					Result.ScheduleResult.RegionResults[0].SolveResult.PreparationFailureKind =
						BridgePreparationFailureKind;
					Result.ScheduleResult.RegionResults[0].SolveResult.PropagationStats.CandidateAttemptCount =
						BridgePreparationCandidateAttemptCount;
					Result.ScheduleResult.RegionResults[0].SolveResult.RegionalFailure =
						BridgeRegionalFailure;
				}
				return Result;
			}

			Result.ProofContract = BuildNegotiatedProofScheduleContract(
				SolveContext,
				PlacementBackedDemandResults,
				PlacementBackedSeamPlan,
				&RootTerrainSteppedPreparedSolveContract);
			if (!PlacementBackedParentProofRequest.RegionDebugPath.IsEmpty())
			{
				for (const FLayoutForcedPlacementBundleInsertion& Insertion :
					Result.ProofContract.ParentRequest.ForcedPlacementBundleInsertions)
				{
					AppendForcedPlacementBundleInsertionIfMissingOnPlacementBridge(
						PlacementBackedParentProofRequest.ForcedPlacementBundleInsertions,
						Insertion);
				}
				for (const FLayoutRouteConstraintRecord& RouteConstraint :
					Result.ProofContract.ParentRequest.RequiredRouteConstraints)
				{
					AppendRequiredRouteConstraintIfMissingOnPlacementBridge(
						PlacementBackedParentProofRequest.RequiredRouteConstraints,
						RouteConstraint);
				}
				for (const FIntVector& ProtectedCell :
					Result.ProofContract.ParentRequest.ProtectedStructuralCells)
				{
					AppendProtectedStructuralCellIfMissing(
						PlacementBackedParentProofRequest.ProtectedStructuralCells,
						ProtectedCell);
				}
				PlacementBackedParentProofRequest.ProtectedStructuralCells.Sort([](
					const FIntVector& Left,
					const FIntVector& Right)
				{
					return Left.Z != Right.Z
						? Left.Z < Right.Z
						: (Left.Y != Right.Y
							? Left.Y < Right.Y
							: Left.X < Right.X);
				});
				LayoutRegionRequestSnapshotBuilder::RefreshStandaloneForcedPlacementBundleInsertionAssertions(
					PlacementBackedParentProofRequest);
				LayoutRegionRequestSnapshotBuilder::RefreshStandaloneRequiredRouteConstraintAssertions(
					PlacementBackedParentProofRequest);
				Result.ProofContract.ParentRequest = PlacementBackedParentProofRequest;
				for (const FLayoutNegotiatedChildResponsibilityContract& ResponsibilityContract :
					Result.ProofContract.ResponsibilityContracts)
				{
					const bool bAlreadyPresent =
						Result.ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts.ContainsByPredicate(
							[&ResponsibilityContract](const FLayoutNegotiatedChildResponsibilityContract& ExistingContract)
							{
								return ExistingContract.ParentRegionDebugPath == ResponsibilityContract.ParentRegionDebugPath
									&& ExistingContract.ChildRegionDebugPath == ResponsibilityContract.ChildRegionDebugPath;
							});
					if (!bAlreadyPresent)
					{
						Result.ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts.Add(
							ResponsibilityContract);
					}
				}
				AppendNegotiatedContractProofDiagnosticsToRequest(
					Result.ProofContract.ParentRequest);
			}

			Result.ProofContract.PlannedRecursiveVerticalAccessSummary =
				PlacementBackedVerticalAccessSummary;
			if (const FLayoutValidationAssertionRecord* FailedParentRequestAssertion =
				FindFailedBlockingRequestContractAssertion(Result.ProofContract.ParentRequest))
			{
				Result.FailureReason = FailedParentRequestAssertion->FailureReason.IsEmpty()
					? FString::Printf(
						TEXT("Coordinator placement-backed proof request for '%s' failed request-contract assertion '%s'."),
						*SolveContext.RootRequest.RegionDebugPath,
						*FailedParentRequestAssertion->AssertionId.ToString())
					: FailedParentRequestAssertion->FailureReason;
				Result.ScheduleResult = BuildCapabilityBackedFailureScheduleResult(
					Result.ProofContract.ParentRequest,
					Result.FailureReason);
				ApplySteppedTerrainTransitionDiagnosticsToScheduleResult(
					Result.ScheduleResult,
					SolveContext.RootRequest.RegionDebugPath,
					StructuralInputs);
				ApplyRootSteppedDiagnosticsToMergedSolveResult(
					Result.ScheduleResult,
					SolveContext.RootRequest.RegionDebugPath);
				ApplyRequestOwnedSolveSurfaceToMergedSolveResult(
					Result.ScheduleResult,
					Result.ProofContract.ParentRequest);
				return Result;
			}

			bool bConsumedCertifiedRegionalProofResults = false;
			if (PlacementBackedCertifiedRegionalProofResults.IsSet())
			{
				FString CertifiedReductionFailureReason;
				if (BuildIndependentProofScheduleResultFromPreparedRequests(
						Result.ProofContract.ParentRequest,
						Result.ProofContract.ChildRequests,
						Result.ProofContract,
						MoveTemp(PlacementBackedCertifiedRegionalProofResults.ParentProofResult),
						MoveTemp(PlacementBackedCertifiedRegionalProofResults.ChildProofResults),
						Result.ScheduleResult,
						CertifiedReductionFailureReason))
				{
					Result.ScheduleResult.bConsumedCertifiedRegionalProofResults = true;
					bConsumedCertifiedRegionalProofResults = true;
				}
			}
			if (!bConsumedCertifiedRegionalProofResults)
			{
				Result.ScheduleResult = ExecuteNegotiatedProofSchedule(
					SolveContext,
					Result.ProofContract);
			}
			ApplySteppedTerrainTransitionDiagnosticsToScheduleResult(
				Result.ScheduleResult,
				SolveContext.RootRequest.RegionDebugPath,
				StructuralInputs);
			ApplyRootSteppedDiagnosticsToMergedSolveResult(
				Result.ScheduleResult,
				SolveContext.RootRequest.RegionDebugPath);
			ApplyRequestOwnedSolveSurfaceToMergedSolveResult(
				Result.ScheduleResult,
				Result.ProofContract.ParentRequest);
			Result.bSucceeded = Result.ScheduleResult.bSucceeded;
			Result.FailureReason = Result.ScheduleResult.FailureReason;
			return Result;
		}

		TArray<FNegotiatedDemandResult> NegotiatedDemandResults;
		NegotiatedDemandResults.Reserve(StructuralInputs.Demands.Num());

		FString FirstRequiredDemandFailureReason;
		for (const FNegotiationDemandPlan& DemandPlan : StructuralInputs.Demands)
		{
			if (!IsDemandActiveForCurrentTerrainStage(DemandPlan))
			{
				continue;
			}

			const FResidualParentCapabilitySummary ParentSummary =
				RebuildResidualParentSummaryForDemand(StructuralInputs, DemandPlan);
			FNegotiatedDemandResult DemandResult =
				NegotiateDemandResponsibilities(
					SolveContext,
					DemandPlan,
					ParentSummary);
			if (!DemandResult.bSucceeded
				&& !DemandResult.ChildRequest.bSourceContentEntryOptional
				&& FirstRequiredDemandFailureReason.IsEmpty())
			{
				FirstRequiredDemandFailureReason = DemandResult.FailureReason.IsEmpty()
					? FString::Printf(
						TEXT("Negotiation failed for required child region '%s'."),
						*DemandPlan.ChildRegionDebugPath)
					: DemandResult.FailureReason;
			}

			NegotiatedDemandResults.Add(MoveTemp(DemandResult));
		}

		if (!FirstRequiredDemandFailureReason.IsEmpty())
		{
			Result.FailureReason = FirstRequiredDemandFailureReason;
			Result.ScheduleResult = BuildCapabilityBackedFailureScheduleResult(
				SolveContext.RootRequest,
				Result.FailureReason);
			ApplySteppedTerrainTransitionDiagnosticsToScheduleResult(
				Result.ScheduleResult,
				SolveContext.RootRequest.RegionDebugPath,
				StructuralInputs);
			ApplyRootSteppedDiagnosticsToMergedSolveResult(
				Result.ScheduleResult,
				SolveContext.RootRequest.RegionDebugPath);
			ApplyRequestOwnedSolveSurfaceToMergedSolveResult(
				Result.ScheduleResult,
				SolveContext.RootRequest);
			return Result;
		}

		FNegotiatedSeamPlan EmptyCommittedSeamPlan;
		EmptyCommittedSeamPlan.bPlannedPartitionSeamsAreAuthoritative = true;
		Result.ProofContract = BuildNegotiatedProofScheduleContract(
			SolveContext,
			NegotiatedDemandResults,
			EmptyCommittedSeamPlan,
			&RootTerrainSteppedPreparedSolveContract);
		if (const FLayoutValidationAssertionRecord* FailedParentRequestAssertion =
			FindFailedBlockingRequestContractAssertion(Result.ProofContract.ParentRequest))
		{
			Result.FailureReason = FailedParentRequestAssertion->FailureReason.IsEmpty()
				? FString::Printf(
					TEXT("Coordinator built an invalid parent proof request for '%s' because request-contract assertion '%s' failed after carrier mapping."),
					*SolveContext.RootRequest.RegionDebugPath,
					*FailedParentRequestAssertion->AssertionId.ToString())
				: FailedParentRequestAssertion->FailureReason;
			Result.ScheduleResult = BuildCapabilityBackedFailureScheduleResult(
				Result.ProofContract.ParentRequest,
				Result.FailureReason);
			ApplySteppedTerrainTransitionDiagnosticsToScheduleResult(
				Result.ScheduleResult,
				SolveContext.RootRequest.RegionDebugPath,
				StructuralInputs);
			ApplyRootSteppedDiagnosticsToMergedSolveResult(
				Result.ScheduleResult,
				SolveContext.RootRequest.RegionDebugPath);
			ApplyRequestOwnedSolveSurfaceToMergedSolveResult(
				Result.ScheduleResult,
				Result.ProofContract.ParentRequest);
			return Result;
		}

		Result.ScheduleResult = ExecuteNegotiatedProofSchedule(
			SolveContext,
			Result.ProofContract);
		ApplySteppedTerrainTransitionDiagnosticsToScheduleResult(
			Result.ScheduleResult,
			SolveContext.RootRequest.RegionDebugPath,
			StructuralInputs);
		ApplyRootSteppedDiagnosticsToMergedSolveResult(
			Result.ScheduleResult,
			SolveContext.RootRequest.RegionDebugPath);
		ApplyRequestOwnedSolveSurfaceToMergedSolveResult(
			Result.ScheduleResult,
			Result.ProofContract.ParentRequest);
		Result.bSucceeded = Result.ScheduleResult.bSucceeded;
		Result.FailureReason = Result.ScheduleResult.FailureReason;
		return Result;
	}

		FLayoutRegionSolveScheduleResult SolveSingleRegionTreeWithPreparedSteppedContract(
			const FLayoutRegionSolveRequest& RootRequest)
		{
			FLayoutRegionSolveRequest EffectiveRequest = RootRequest;
			const bool bSteppedSolveExpected =
				EffectiveRequest.RootPlacementKind != ELayoutWorldBindingPlacementKind::None
				&& EffectiveRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve;

			if (bSteppedSolveExpected)
			{
			const FRecursiveScheduleSolveContext SolveContext =
				BuildSolveContext(EffectiveRequest);
			const FCompiledStructuralInputs StructuralInputs =
				BuildCompiledStructuralInputs(SolveContext);
			const FTerrainSteppedPreparedSolveContract PreparedContract =
				ValidateRootTerrainSteppedPreparedSolveContract(
					EffectiveRequest,
					StructuralInputs);
			AppendPreparedSteppedCarriersToRequest(
				EffectiveRequest,
				PreparedContract);

			LayoutRegionRequestSnapshotBuilder::RefreshStandaloneForcedPlacementBundleInsertionAssertions(
				EffectiveRequest);
			LayoutRegionRequestSnapshotBuilder::RefreshStandaloneRequiredRouteConstraintAssertions(
				EffectiveRequest);
		}

		FLayoutRegionSolveResult RootResult =
			FLayoutProfileSolver::SolveRegion(EffectiveRequest);

			if (bSteppedSolveExpected)
			{
			const FRecursiveScheduleSolveContext SolveContext =
				BuildSolveContext(EffectiveRequest);
			const FCompiledStructuralInputs StructuralInputs =
				BuildCompiledStructuralInputs(SolveContext);
			const FTerrainSteppedPreparedSolveContract PreparedContract =
				ValidateRootTerrainSteppedPreparedSolveContract(
					EffectiveRequest,
					StructuralInputs);
			ApplySteppedTerrainTransitionDiagnosticsToRegionResult(
				RootResult,
				StructuralInputs);
			RootResult.ForcedPlacementBundleInsertions =
				EffectiveRequest.ForcedPlacementBundleInsertions;
			RootResult.RequiredRouteConstraints =
				EffectiveRequest.RequiredRouteConstraints;
			RootResult.ValidationAssertions =
				EffectiveRequest.ValidationAssertions;

			if (!PreparedContract.bRootCanSatisfyTerrainSteppedTransitions)
			{
				RootResult.SolveResult.bSucceeded = false;
				RootResult.SolveResult.FailureReason =
					PreparedContract.RejectionSummary.FailureReason.IsEmpty()
						? TEXT("Root stepped-terrain solve cannot continue because the prepared root support contract is incomplete.")
						: PreparedContract.RejectionSummary.FailureReason;
				AppendCapabilityFailureMessage(
					RootResult.SolveResult,
					RootResult.SolveResult.FailureReason);
			}
		}

		return BuildSingleRegionScheduleResult(EffectiveRequest, RootResult);
	}

	FNegotiatedDemandResult BuildFailedDemandResult(
		const FString& ChildRegionDebugPath,
		const FResidualParentCapabilitySummary& ParentSummary,
		const FString& FailureReason)
	{
		FNegotiatedDemandResult Result;
		Result.ChildRegionDebugPath = ChildRegionDebugPath;
		Result.ParentSummary = ParentSummary;
		Result.FailureReason = FailureReason;
		return Result;
	}

	FCapabilityBackedScheduleResult BuildFailedCapabilityBackedScheduleResult(
		const FString& FailureReason)
	{
		FCapabilityBackedScheduleResult Result;
		Result.FailureReason = FailureReason;
		return Result;
	}
}

LayoutProfileSolverInternal::FRecursiveHostVerticalAccessComposition LayoutProfileSolverInternal::DetermineRecursiveHostVerticalAccessCompositionForSchedule(
	const FLayoutRegionSolveRequest& RootRequest,
	const TArray<FLayoutPlannedCell>& ParentPlannedCells,
	const TSet<FIntVector>& ReservedParentCells,
	const TArray<FRecursiveVerticalAccessProvider>& ChildProviders)
{
	TArray<LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement> Placements;
	Placements.Reserve(ChildProviders.Num());
	for (const FRecursiveVerticalAccessProvider& Provider : ChildProviders)
	{
		LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement& Placement = Placements.AddDefaulted_GetRef();
		Placement.ChildRegionDebugPath = Provider.ChildRegionDebugPath;
		Placement.bSupportsVerticalAccess = Provider.bSupportsVerticalAccess;
		Placement.bContributesHostVerticalAccess = Provider.bContributesHostVerticalAccess;
		Placement.ChildVerticalAccessLocalCells = Provider.VerticalAccessLocalCells;
		Placement.ChildRequest.PlannedCells = Provider.PlannedCells;
		Placement.DirectChildCommitment.EndpointCommitments = Provider.EndpointCommitments;
	}

	const LayoutRegionScheduleSolverPrivate::FCommittedVerticalAccessOwnership Ownership =
		LayoutRegionScheduleSolverPrivate::DetermineCommittedVerticalAccessOwnership(
			RootRequest,
			ParentPlannedCells,
			ReservedParentCells,
			Placements);
	FRecursiveHostVerticalAccessComposition Composition;
	Composition.RequiredHostProviderCount = Ownership.RequiredHostProviderCount;
	Composition.CountedParentProviderCount = Ownership.CountedParentProviderCount;
	Composition.CountedChildRegionDebugPaths = Ownership.CountedChildRegionDebugPaths;
	Composition.LocalOnlyChildRegionDebugPaths = Ownership.LocalOnlyChildRegionDebugPaths;
	Composition.UnusableContributingChildRegionDebugPaths = Ownership.UnusableContributingChildRegionDebugPaths;
	Composition.ExtraContributingChildRegionDebugPaths = Ownership.ExtraContributingChildRegionDebugPaths;
	Composition.FailureReason = Ownership.FailureReason;
	return Composition;
}

LayoutProfileSolverInternal::ERecursiveVerticalAccessOwnerKind LayoutProfileSolverInternal::DetermineRecursiveVerticalAccessOwnerForSchedule(
	const FLayoutRegionSolveRequest& RootRequest,
	const TArray<FLayoutPlannedCell>& ParentPlannedCells,
	const TSet<FIntVector>& ReservedParentCells,
	const TArray<FRecursiveVerticalAccessProvider>& ChildProviders,
	FString* OutChildRegionDebugPath)
{
	const FRecursiveHostVerticalAccessComposition Composition =
		DetermineRecursiveHostVerticalAccessCompositionForSchedule(
			RootRequest,
			ParentPlannedCells,
			ReservedParentCells,
			ChildProviders);
	if (OutChildRegionDebugPath != nullptr)
	{
		OutChildRegionDebugPath->Reset();
		if (Composition.CountedParentProviderCount <= 0 && Composition.CountedChildRegionDebugPaths.Num() == 1)
		{
			*OutChildRegionDebugPath = Composition.CountedChildRegionDebugPaths[0];
		}
	}

	if (Composition.CountedParentProviderCount > 0)
	{
		return ERecursiveVerticalAccessOwnerKind::Parent;
	}
	if (!Composition.CountedChildRegionDebugPaths.IsEmpty())
	{
		return ERecursiveVerticalAccessOwnerKind::ChildRegion;
	}
	return ERecursiveVerticalAccessOwnerKind::None;
}

bool LayoutProfileSolverInternal::TrySelectNegotiatedHostVerticalAccessAnchorsForTests(
	const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
	const TArray<FLayoutPlannedCell>& ChildPlannedCells,
	const TArray<FIntVector>& ChildVerticalAccessLocalCells,
	FLayoutCommittedEndpointAnchor& OutIngressAnchor,
	FLayoutCommittedEndpointAnchor& OutEgressAnchor,
	TArray<FIntVector>& OutRouteCells,
	FString& OutFailureReason)
{
	return LayoutRegionScheduleSolverPrivate::TrySelectNegotiatedHostVerticalAccessAnchors(
		CandidateCommitments,
		ChildPlannedCells,
		ChildVerticalAccessLocalCells,
		OutIngressAnchor,
		OutEgressAnchor,
		OutRouteCells,
		OutFailureReason);
}

bool LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
	const FLayoutNegotiatedChildResponsibilityContract& Contract,
	FString& OutFailureReason)
{
	return LayoutRegionScheduleSolverPrivate::ValidateNegotiatedChildResponsibilityContract(
		Contract,
		OutFailureReason);
}

void LayoutProfileSolverInternal::NormalizeSuccessfulChildPlacementCandidatesForTests(
	TArray<LayoutProfileSolverInternal::FNegotiatedCandidateNormalizationInputForTests>& InOutCandidates,
	const bool bPartialPlacementStep,
	const int32 PlacementDemandCount,
	int32& OutCollapsedCount,
	int32& OutBeamTrimmedCount)
{
	LayoutRegionScheduleSolverPrivate::NormalizeSuccessfulChildPlacementCandidatesForTestsInternal(
		InOutCandidates,
		bPartialPlacementStep,
		PlacementDemandCount,
		OutCollapsedCount,
		OutBeamTrimmedCount);
}

#if WITH_AUTOMATION_TESTS
bool FLayoutProfileSolver::DebugTryGetSiblingSharedOverlapFaceDirections(
	const FIntVector& CandidateLocalCell,
	const FIntPoint& CandidateFootprintSize,
	const FIntVector& ExistingLocalCell,
	const FIntPoint& ExistingFootprintSize,
	ELayoutFaceDirection& OutCandidateFaceDirection,
	ELayoutFaceDirection& OutExistingFaceDirection)
{
	return LayoutRegionScheduleSolverPrivate::TryGetSharedOverlapFaceDirections(
		CandidateLocalCell,
		CandidateFootprintSize,
		ExistingLocalCell,
		ExistingFootprintSize,
		OutCandidateFaceDirection,
		OutExistingFaceDirection);
}
#endif

FLayoutRegionSolveScheduleResult FLayoutProfileSolver::SolveRegionsSynchronously(const FLayoutRegionSolveScheduleRequest& ScheduleRequest)
{
	if (IsInGameThread() && !GIsAutomationTesting)
	{
		FLayoutRegionSolveScheduleResult Result;
		Result.FailureReason = TEXT("Synchronous scheduled-region proof is disabled on the game thread. Submit background layout proof work and publish the result on the game thread instead.");
		Result.MergedSolveResult.bSucceeded = false;
		Result.MergedSolveResult.FailureReason = Result.FailureReason;
		return Result;
	}

	return LayoutRegionScheduleSolverPrivate::SolveRegionsSynchronouslyInternal(ScheduleRequest);
}

FLayoutRegionSolveScheduleResult FLayoutProfileSolver::SolveRegionTree(const FLayoutRegionSolveRequest& RootRequest)
{
	PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_RegionTree, STAT_PorismLayout_RegionTreeSolve);
	if (IsInGameThread() && !GIsAutomationTesting)
	{
		FLayoutRegionSolveScheduleResult Result;
		Result.FailureReason = TEXT("Synchronous SolveRegionTree proof is disabled on the game thread. Submit a background layout solve and publish the result on the game thread instead.");
		Result.MergedSolveResult.bSucceeded = false;
		Result.MergedSolveResult.FailureReason = Result.FailureReason;
		return Result;
	}

	LayoutSolveExecution::FScope ExecutionScope(RootRequest.ExecutionSettings.MaxSolveDurationSeconds, RootRequest.ExecutionSettings.MaxCandidateAttempts);
	FLayoutRegionSolveScheduleResult Result;
	if (LayoutSolveExecution::Checkpoint(Result.FailureReason))
	{
		Result = LayoutRegionScheduleSolverPrivate::SolveRegionTreeInternal(RootRequest);
	}
	FString ExecutionFailure;
	if (!LayoutSolveExecution::Checkpoint(ExecutionFailure))
	{
		// Preserve the rejected proof behind a terminal budget/cancellation result.
		// This is diagnostic evidence only; expiration still forbids publication.
		if (Result.FailureReason.IsEmpty()) Result.FailureReason = ExecutionFailure;
		else if (!Result.FailureReason.StartsWith(ExecutionFailure))
			Result.FailureReason = ExecutionFailure + TEXT(" Last proof: ") + Result.FailureReason.Left(4096);
		Result.bSucceeded = false;
		Result.MergedSolveResult.bSucceeded = false;
		Result.MergedSolveResult.FailureReason = Result.FailureReason;
		return Result;
	}
	if (Result.MergedSolveResult.ForcedPlacementBundleInsertions.IsEmpty())
	{
		for (const FLayoutPlacedModule& Placement : Result.MergedSolveResult.Placements)
		{
			if (Placement.Intent != ELayoutCellIntent::VerticalAccess
				|| Placement.ModuleSnapshotId.IsNone()
				|| Placement.ModuleSnapshotIndex == INDEX_NONE
				|| RootRequest.EffectiveSnapshotId.IsNone())
			{
				continue;
			}
			FLayoutForcedPlacementBundleInsertion& Insertion = Result.MergedSolveResult.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
			Insertion.BundleId = FLayoutId(*FString::Printf(TEXT("%s.Bundle.%d.%s"), *RootRequest.EffectiveSnapshotId.ToString(), Placement.ModuleSnapshotIndex, *Placement.ModuleSnapshotId.ToString()));
			Insertion.AnchorCell = Placement.Cell;
			Insertion.ProvingCell = Placement.Cell;
		}
	}
	return Result;
}
