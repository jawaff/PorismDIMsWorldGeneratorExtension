// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutRegionRequestSnapshotBuilder.h"

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"

namespace LayoutRegionRequestSnapshotBuilder
{
namespace
{
		/** Returns whether frozen request must carry Stepped support after mode selection. */
		bool IsWorldFacingSteppedSolveExpected(const FLayoutRegionSolveRequest& Request)
		{
			// Local project change: classified flat fallback preserves profile capability
			// but its finalized mode is authoritative for worker support requirements.
			return Request.RootPlacementKind != ELayoutWorldBindingPlacementKind::None
				&& Request.ProfileSnapshot.bSupportsSteppedTerrainSolve
				&& (!Request.bHasSelectedModePlan || Request.SelectedModePlan.bUsesSteppedTerrainTopology);
		}

		TArray<FLayoutId> BuildSteppedRequestRelatedIds(const FLayoutRegionSolveRequest& Request)
		{
			TArray<FLayoutId> RelatedIds;
			RelatedIds.Add(Request.ProfileSnapshot.SnapshotId);
			if (Request.RootPlacementPolicyId != NAME_None)
			{
				RelatedIds.Add(Request.RootPlacementPolicyId);
			}
			if (Request.RootCandidateId != NAME_None)
			{
				RelatedIds.Add(Request.RootCandidateId);
			}
			if (Request.RootSolveId != NAME_None)
			{
				RelatedIds.Add(Request.RootSolveId);
			}
			return RelatedIds;
		}

		FString BuildSteppedRequestProfileBreadcrumb(const FLayoutRegionSolveRequest& Request)
		{
			const FString LiveProfileBreadcrumb =
				Request.ProfileSnapshot.SourceProfile != nullptr
					? FString::Printf(TEXT(" Profile: %s."), *Request.ProfileSnapshot.SourceProfile->GetPathName())
					: FString();
			return FString::Printf(
				TEXT(" Frozen profile snapshot %s (%s).%s"),
				*Request.ProfileSnapshot.DebugName.ToString(),
				*Request.ProfileSnapshot.SnapshotId.ToString(),
				*LiveProfileBreadcrumb);
		}

		FString BuildProfileSnapshotBreadcrumb(const FLayoutProfileSolveSnapshot& ProfileSnapshot)
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

		FString BuildContentSetSnapshotBreadcrumb(const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot)
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

		FString BuildModuleCatalogBreadcrumb(const FLayoutModuleCatalog& ModuleCatalog)
		{
			return FString::Printf(
				TEXT(" Module-set snapshot %s (%s).%s"),
				*ModuleCatalog.DebugName.ToString(),
				*ModuleCatalog.SnapshotId.ToString(),
				*FString());
		}

		FString BuildRootRequestBreadcrumb(const FLayoutRegionSolveRequest& Request)
		{
			return FString::Printf(
				TEXT(" Request '%s' (%s)."),
				*Request.RegionDebugPath,
				*Request.EffectiveSnapshotId.ToString());
		}

		FIntVector ResolveRequestSharedCellSizeFromSnapshots(const FLayoutRegionSolveRequest& Request)
		{
			if (Request.ContentSetSnapshot.SharedCellSizeInBlocks != FIntVector::ZeroValue)
			{
				return Request.ContentSetSnapshot.SharedCellSizeInBlocks;
			}

			return Request.ModuleCatalog.SharedCellSizeInBlocks;
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

		FLayoutValidationAssertionRecord MakeRequestSharedCellSizeAssertion(
			const FLayoutRegionSolveRequest& Request)
		{
			const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot = Request.ContentSetSnapshot;
			const FLayoutModuleCatalog& ModuleCatalog = Request.ModuleCatalog;
			const FIntVector EffectiveSharedCellSize = ResolveRequestSharedCellSizeFromSnapshots(Request);
			const bool bPassed =
				EffectiveSharedCellSize != FIntVector::ZeroValue
				|| (ContentSetSnapshot.SharedCellSizeInBlocks == FIntVector::ZeroValue
					&& ModuleCatalog.SharedCellSizeInBlocks == FIntVector::ZeroValue);

			return MakeSnapshotAssertionRecord(
				TEXT("RegionRequest.SharedCellSizeContractValid"),
				ELayoutValidationAssertionKind::RequestContractValid,
				bPassed,
				{
					Request.EffectiveSnapshotId,
					ContentSetSnapshot.SnapshotId,
					ModuleCatalog.SnapshotId
				},
				bPassed
					? FString()
					: FString::Printf(
						TEXT("Region solve request does not carry a usable shared cell size on either frozen snapshot.%s%s"),
						*BuildContentSetSnapshotBreadcrumb(ContentSetSnapshot),
						*BuildModuleCatalogBreadcrumb(ModuleCatalog)));
		}

		FLayoutValidationAssertionRecord MakeRequestRootPublicationIdentityAssertion(
			const FLayoutRegionSolveRequest& Request)
		{
			TArray<FLayoutId> RelatedIds;
			if (Request.EffectiveSnapshotId != NAME_None)
			{
				RelatedIds.Add(Request.EffectiveSnapshotId);
			}
			if (Request.RootPlacementPolicyId != NAME_None)
			{
				RelatedIds.Add(Request.RootPlacementPolicyId);
			}
			if (Request.RootCandidateId != NAME_None)
			{
				RelatedIds.Add(Request.RootCandidateId);
			}
			if (Request.RootSolveId != NAME_None)
			{
				RelatedIds.Add(Request.RootSolveId);
			}

			const bool bHasAnyPublicationIdentity =
				Request.RootPlacementPolicyId != NAME_None
				|| Request.RootCandidateId != NAME_None
				|| Request.RootSolveId != NAME_None;
			const bool bPassed =
				!bHasAnyPublicationIdentity
				|| (Request.RootCandidateId != NAME_None && Request.RootSolveId != NAME_None);

			return MakeSnapshotAssertionRecord(
				TEXT("RegionRequest.RootPublicationIdentityContractValid"),
				ELayoutValidationAssertionKind::RequestContractValid,
				bPassed,
				RelatedIds,
				bPassed
					? FString()
					: FString::Printf(
						TEXT("Region solve request '%s' (%s) carries incomplete root publication identity. Requests that specify root publication or world-binding identity must carry both RootCandidateId and RootSolveId before recursive scheduling begins."),
						*Request.RegionDebugPath,
						*Request.EffectiveSnapshotId.ToString()));
		}

		FLayoutValidationAssertionRecord MakeRequestTemplatePlacementOffsetAssertion(
			const FLayoutRegionSolveRequest& Request)
		{
			const int32 TemplatePlacementZOffsetBlocks = Request.TemplatePlacementZOffsetBlocks;
			const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot = Request.ContentSetSnapshot;
			const FLayoutModuleCatalog& ModuleCatalog = Request.ModuleCatalog;
			const bool bHasUsableSharedCellSize =
				ResolveRequestSharedCellSizeFromSnapshots(Request) != FIntVector::ZeroValue;
			const bool bPassed =
				TemplatePlacementZOffsetBlocks == 0
				|| bHasUsableSharedCellSize;

			return MakeSnapshotAssertionRecord(
				TEXT("RegionRequest.TemplatePlacementOffsetContractValid"),
				ELayoutValidationAssertionKind::RequestContractValid,
				bPassed,
				{
					Request.EffectiveSnapshotId,
					ContentSetSnapshot.SnapshotId,
					ModuleCatalog.SnapshotId
				},
				bPassed
					? FString()
					: FString::Printf(
						TEXT("Region solve request '%s' (%s) carries TemplatePlacementZOffsetBlocks=%d without a usable shared cell-size contract.%s%s Request-owned placement offsets require one usable request shared-cell metric before recursive scheduling begins."),
						*Request.RegionDebugPath,
						*Request.EffectiveSnapshotId.ToString(),
						TemplatePlacementZOffsetBlocks,
						*BuildContentSetSnapshotBreadcrumb(ContentSetSnapshot),
						*BuildModuleCatalogBreadcrumb(ModuleCatalog)));
		}

		FLayoutValidationAssertionRecord MakeRequestSteppedTerrainSupportAssertion(
			const FLayoutRegionSolveRequest& Request)
		{
			const bool bSteppedSolveRequested =
				IsWorldFacingSteppedSolveExpected(Request);
			const bool bHasSupportSamples = !Request.SteppedTerrainSupportMap.SupportSamples.IsEmpty();
			const bool bPassed =
				!bSteppedSolveRequested
				|| (Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks > 0 && bHasSupportSamples);

			return MakeSnapshotAssertionRecord(
				TEXT("RegionRequest.SteppedTerrainSupportContractValid"),
				ELayoutValidationAssertionKind::RequestContractValid,
				bPassed,
				BuildSteppedRequestRelatedIds(Request),
				bPassed
					? FString()
					: FString::Printf(
						TEXT("Region solve request selects a world-facing stepped-capable profile but does not carry a usable stepped terrain support map.%s Requests that enter recursive scheduling with stepped terrain enabled must preserve request-owned per-cell support samples and a positive shared cell height before stepped adjacency is compiled."),
						*BuildSteppedRequestProfileBreadcrumb(Request)));
		}

		FLayoutValidationAssertionRecord MakeRequestSteppedTerrainNeighborDeltaAssertion(
			const FLayoutRegionSolveRequest& Request)
		{
			const bool bSteppedSolveRequested =
				IsWorldFacingSteppedSolveExpected(Request);
			const bool bHasUsableSupportMap =
				Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks > 0
				&& !Request.SteppedTerrainSupportMap.SupportSamples.IsEmpty();
			const bool bPassed =
				!bSteppedSolveRequested
				|| !bHasUsableSupportMap
				|| true;

			return MakeSnapshotAssertionRecord(
				TEXT("RegionRequest.SteppedTerrainNeighborDeltaContractValid"),
				ELayoutValidationAssertionKind::RequestContractValid,
				bPassed,
				BuildSteppedRequestRelatedIds(Request),
				bPassed
					? FString()
					: FString::Printf(
						TEXT("Stepped terrain neighbor delta check (%d) exceeds contract — field removed."),
						Request.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta));
		}

		FLayoutValidationAssertionRecord MakeRequestSteppedProfileCapabilityAssertion(
			const FLayoutRegionSolveRequest& Request)
		{
			const bool bSteppedSupportPresent =
				Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks > 0
				|| !Request.SteppedTerrainSupportMap.SupportSamples.IsEmpty();
			const bool bPassed = !bSteppedSupportPresent || Request.ProfileSnapshot.bSupportsSteppedTerrainSolve;

			return MakeSnapshotAssertionRecord(
				TEXT("RegionRequest.SteppedProfileCapabilityContractValid"),
				ELayoutValidationAssertionKind::RequestContractValid,
				bPassed,
				BuildSteppedRequestRelatedIds(Request),
				bPassed
					? FString()
					: FString::Printf(
						TEXT("Region solve request carries stepped terrain support, but frozen profile snapshot %s (%s) does not declare stepped-terrain capability.%s Frozen stepped requests must preserve whether the selected profile supports stepped terrain solving before recursive scheduling begins."),
						*Request.ProfileSnapshot.DebugName.ToString(),
						*Request.ProfileSnapshot.SnapshotId.ToString(),
						*BuildSteppedRequestProfileBreadcrumb(Request).RightChop(1)));
		}

		FLayoutValidationAssertionRecord MakeRequestSteppedTerrainCoverageAssertion(
			const FLayoutRegionSolveRequest& Request)
		{
			const bool bSteppedSolveRequested =
				IsWorldFacingSteppedSolveExpected(Request);
			const bool bHasUsableSupportMap =
				Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks > 0
				&& !Request.SteppedTerrainSupportMap.SupportSamples.IsEmpty();
			const bool bRequiresCoverageValidation =
				bSteppedSolveRequested
				&& !Request.PlannedCells.IsEmpty()
				&& bHasUsableSupportMap;

			// Local project fix: support samples own source XY columns; finalized
			// authored cells may move to different physical Z stages.
			FIntVector MissingPlannedCell = FIntVector::ZeroValue;
			FIntVector UnsupportedSupportCell = FIntVector::ZeroValue;
			const bool bMissingPlannedCell =
				bRequiresCoverageValidation
				&& Request.PlannedCells.ContainsByPredicate(
					[&Request, &MissingPlannedCell](const FLayoutPlannedCell& PlannedCell)
					{
						const bool bHasSupportSample =
							Request.SteppedTerrainSupportMap.SupportSamples.ContainsByPredicate(
								[&PlannedCell](const FLayoutSteppedTerrainSupportSample& SupportSample)
								{
									return SupportSample.LocalCell.X == PlannedCell.Cell.X
										&& SupportSample.LocalCell.Y == PlannedCell.Cell.Y;
								});
						if (!bHasSupportSample)
						{
							MissingPlannedCell = PlannedCell.Cell;
						}

						return !bHasSupportSample;
					});
			const bool bSupportOutsidePlannedCells =
				bRequiresCoverageValidation
				&& Request.SteppedTerrainSupportMap.SupportSamples.ContainsByPredicate(
					[&Request, &UnsupportedSupportCell](const FLayoutSteppedTerrainSupportSample& SupportSample)
					{
						const bool bHasPlannedCell =
							Request.PlannedCells.ContainsByPredicate(
								[&SupportSample](const FLayoutPlannedCell& PlannedCell)
								{
									return PlannedCell.Cell.X == SupportSample.LocalCell.X
										&& PlannedCell.Cell.Y == SupportSample.LocalCell.Y;
								});
						if (!bHasPlannedCell)
						{
							UnsupportedSupportCell = SupportSample.LocalCell;
						}

						return !bHasPlannedCell;
					});
			const bool bPassed =
				!bRequiresCoverageValidation
				|| (!bMissingPlannedCell && !bSupportOutsidePlannedCells);

			return MakeSnapshotAssertionRecord(
				TEXT("RegionRequest.SteppedTerrainSupportCoverageContractValid"),
				ELayoutValidationAssertionKind::RequestContractValid,
				bPassed,
				BuildSteppedRequestRelatedIds(Request),
				bPassed ? FString() :
					bMissingPlannedCell ? FString::Printf(
						TEXT("Region solve request supplies stepped terrain support plus override-planned cells, but the support map is missing planned column for cell %s.%s Request-owned stepped support must cover every supplied planned XY column before recursive scheduling begins."),
						*MissingPlannedCell.ToString(),
						*BuildSteppedRequestProfileBreadcrumb(Request))
					: FString::Printf(
						TEXT("Region solve request supplies stepped terrain support plus override-planned cells, but the support map also contains unsupported local column %s.%s Request-owned stepped support must stay within the supplied planned XY columns before recursive scheduling begins."),
						*UnsupportedSupportCell.ToString(),
						*BuildSteppedRequestProfileBreadcrumb(Request)));
		}

		FLayoutValidationAssertionRecord MakeRequestLiveCompositeBundleAssertion(
			const FLayoutRegionSolveRequest& Request)
		{
			const FLayoutModuleCatalog& ModuleCatalog = Request.ModuleCatalog;
			const FLayoutModuleSolveSnapshot* InvalidCompositeSnapshot = nullptr;
			for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : ModuleCatalog.Modules)
			{
				if (ModuleSnapshot.SourceCompositeModule == nullptr)
				{
					continue;
				}

				const bool bHasOccupiedRootCell = ModuleSnapshot.OccupiedLocalCells.Contains(FIntVector::ZeroValue);
				const bool bHasRootAnchorIntent = !ModuleSnapshot.RootSupportedCellIntents.IsEmpty();
				if (!bHasOccupiedRootCell || !bHasRootAnchorIntent)
				{
					InvalidCompositeSnapshot = &ModuleSnapshot;
					break;
				}
			}
			const bool bPassed = InvalidCompositeSnapshot == nullptr;

			TArray<FLayoutId> RelatedIds;
			if (Request.EffectiveSnapshotId != NAME_None)
			{
				RelatedIds.Add(Request.EffectiveSnapshotId);
			}
			if (ModuleCatalog.SnapshotId != NAME_None)
			{
				RelatedIds.Add(ModuleCatalog.SnapshotId);
			}
			if (InvalidCompositeSnapshot != nullptr)
			{
				RelatedIds.Add(InvalidCompositeSnapshot->SnapshotId);
				if (InvalidCompositeSnapshot->SourceCompositeModule != nullptr)
				{
					RelatedIds.Add(InvalidCompositeSnapshot->SourceCompositeModule->GetFName());
				}
			}

			return MakeSnapshotAssertionRecord(
				TEXT("RegionRequest.LiveCompositeBundlePlacementSupported"),
				ELayoutValidationAssertionKind::RequestContractValid,
				bPassed,
				RelatedIds,
				bPassed
					? FString()
					: [InvalidCompositeSnapshot, &Request, &ModuleCatalog]()
					{
						const FString CompositeModuleBreadcrumb =
							InvalidCompositeSnapshot->SourceCompositeModule != nullptr
								? FString::Printf(
									TEXT(" CompositeModule: %s."),
									*InvalidCompositeSnapshot->SourceCompositeModule->GetPathName())
								: FString();
						return FString::Printf(
							TEXT("Region solve request includes composite-backed module snapshot '%s' without the root-anchor bundle contract the live LayoutProfileSolver path now requires.%s%s%s Composite-backed requests must preserve an occupied local root cell at (0,0,0) plus non-empty RootSupportedCellIntents."),
							*InvalidCompositeSnapshot->DebugName.ToString(),
							*BuildRootRequestBreadcrumb(Request),
							*BuildModuleCatalogBreadcrumb(ModuleCatalog),
							*CompositeModuleBreadcrumb);
					}());
		}

		FLayoutValidationAssertionRecord MakeRequestSnapshotContractAssertion(
			const FLayoutRegionSolveRequest& Request)
		{
			const bool bPassed = Request.EffectiveSnapshotId != NAME_None;
			return MakeSnapshotAssertionRecord(
				TEXT("RegionRequest.SnapshotContractInitialized"),
				ELayoutValidationAssertionKind::SnapshotContractInitialized,
				bPassed,
				{
					Request.ProfileSnapshot.SnapshotId,
					Request.ContentSetSnapshot.SnapshotId,
					Request.ModuleCatalog.SnapshotId
				},
				bPassed
					? FString()
					: FString::Printf(
						TEXT("Region solve request failed to initialize an effective snapshot id.%s%s%s%s"),
						*BuildRootRequestBreadcrumb(Request),
						*BuildProfileSnapshotBreadcrumb(Request.ProfileSnapshot),
						*BuildContentSetSnapshotBreadcrumb(Request.ContentSetSnapshot),
						*BuildModuleCatalogBreadcrumb(Request.ModuleCatalog)));
		}

		FLayoutId BuildRequestPlacementBundleId(
			const FLayoutRegionSolveRequest& Request,
			const int32 ModuleIndex,
			const FLayoutModuleSolveSnapshot& ModuleSnapshot)
		{
			return FLayoutId(*FString::Printf(
				TEXT("%s.Bundle.%d.%s"),
				*Request.EffectiveSnapshotId.ToString(),
				ModuleIndex,
				*ModuleSnapshot.SnapshotId.ToString()));
		}

		FLayoutValidationAssertionRecord MakeRequestForcedPlacementBundleInsertionAssertion(
			const FLayoutRegionSolveRequest& Request)
		{
			TArray<FLayoutId> RelatedIds;
			if (Request.EffectiveSnapshotId != NAME_None)
			{
				RelatedIds.Add(Request.EffectiveSnapshotId);
			}
			if (Request.RootPlacementPolicyId != NAME_None)
			{
				RelatedIds.Add(Request.RootPlacementPolicyId);
			}
			if (Request.RootCandidateId != NAME_None)
			{
				RelatedIds.Add(Request.RootCandidateId);
			}
			if (Request.RootSolveId != NAME_None)
			{
				RelatedIds.Add(Request.RootSolveId);
			}

			TSet<FLayoutId> ValidBundleIds;
			for (int32 ModuleIndex = 0; ModuleIndex < Request.ModuleCatalog.Modules.Num(); ++ModuleIndex)
			{
				ValidBundleIds.Add(BuildRequestPlacementBundleId(
					Request,
					ModuleIndex,
					Request.ModuleCatalog.Modules[ModuleIndex]));
			}

			const auto HasPlannedCell =
				[&Request](const FIntVector& Cell)
				{
					return Request.PlannedCells.ContainsByPredicate(
						[&Cell](const FLayoutPlannedCell& PlannedCell)
						{
							return PlannedCell.Cell == Cell;
						});
				};

			bool bPassed = true;
			FString FailureReason;
			TMap<FIntVector, FLayoutForcedPlacementBundleInsertion> SeenInsertionsByAnchorCell;
			TMap<FIntVector, FLayoutForcedPlacementBundleInsertion> SeenInsertionsByProvingCell;
			for (const FLayoutForcedPlacementBundleInsertion& Insertion : Request.ForcedPlacementBundleInsertions)
			{
				if (Insertion.BundleId != NAME_None)
				{
					RelatedIds.AddUnique(Insertion.BundleId);
				}

				if (Insertion.BundleId == NAME_None)
				{
					bPassed = false;
					FailureReason = FString::Printf(
						TEXT("Region solve request carries a forced placement bundle insertion without a BundleId.%s%s Forced insertions must name the frozen root placement bundle they expect to anchor before recursive solving begins."),
						*BuildRootRequestBreadcrumb(Request),
						*BuildModuleCatalogBreadcrumb(Request.ModuleCatalog));
					break;
				}

				if (!ValidBundleIds.Contains(Insertion.BundleId))
				{
					bPassed = false;
					FailureReason = FString::Printf(
						TEXT("Region solve request carries forced placement bundle insertion '%s', but that bundle id does not exist in the frozen module snapshot set for request '%s'.%s%s Forced insertions must reference a root placement bundle compiled from the same request snapshots."),
						*Insertion.BundleId.ToString(),
						*Request.RegionDebugPath,
						*BuildRootRequestBreadcrumb(Request),
						*BuildModuleCatalogBreadcrumb(Request.ModuleCatalog));
					break;
				}

				if (!Request.PlannedCells.IsEmpty() && !HasPlannedCell(Insertion.AnchorCell))
				{
					bPassed = false;
					FailureReason = FString::Printf(
						TEXT("Region solve request carries forced placement bundle insertion '%s' anchored at %s, but that anchor cell is not present in the supplied planned-cell set.%s Forced insertions on override-planned requests must anchor on an authored planned cell."),
						*Insertion.BundleId.ToString(),
						*Insertion.AnchorCell.ToString(),
						*BuildRootRequestBreadcrumb(Request));
					break;
				}

				if (!Request.PlannedCells.IsEmpty() && !HasPlannedCell(Insertion.ProvingCell))
				{
					bPassed = false;
					FailureReason = FString::Printf(
						TEXT("Region solve request carries forced placement bundle insertion '%s' with proving cell %s, but that proving cell is not present in the supplied planned-cell set.%s Forced insertions on override-planned requests must preserve the proving cell that justified the anchored bundle."),
						*Insertion.BundleId.ToString(),
						*Insertion.ProvingCell.ToString(),
						*BuildRootRequestBreadcrumb(Request));
					break;
				}

				if (const FLayoutForcedPlacementBundleInsertion* ExistingInsertion =
					SeenInsertionsByAnchorCell.Find(Insertion.AnchorCell))
				{
					if (ExistingInsertion->BundleId != Insertion.BundleId
						|| ExistingInsertion->ProvingCell != Insertion.ProvingCell)
					{
						bPassed = false;
						FailureReason = FString::Printf(
							TEXT("Region solve request carries conflicting forced placement bundle insertions on anchor cell %s.%s Existing insertion uses bundle '%s' with proving cell %s, but another insertion uses bundle '%s' with proving cell %s. Forced insertions must not ask the same anchor cell to realize contradictory root bundles."),
							*Insertion.AnchorCell.ToString(),
							*BuildRootRequestBreadcrumb(Request),
							*ExistingInsertion->BundleId.ToString(),
							*ExistingInsertion->ProvingCell.ToString(),
							*Insertion.BundleId.ToString(),
							*Insertion.ProvingCell.ToString());
						break;
					}
				}
				else
				{
					SeenInsertionsByAnchorCell.Add(Insertion.AnchorCell, Insertion);
				}

				if (const FLayoutForcedPlacementBundleInsertion* ExistingInsertion =
					SeenInsertionsByProvingCell.Find(Insertion.ProvingCell))
				{
					if (ExistingInsertion->BundleId != Insertion.BundleId
						|| ExistingInsertion->AnchorCell != Insertion.AnchorCell)
					{
						bPassed = false;
						FailureReason = FString::Printf(
							TEXT("Region solve request carries conflicting forced placement bundle insertions for proving cell %s.%s Existing insertion anchors bundle '%s' at %s, but another insertion anchors bundle '%s' at %s. Forced insertions must keep one deterministic support bundle for each proving cell."),
							*Insertion.ProvingCell.ToString(),
							*BuildRootRequestBreadcrumb(Request),
							*ExistingInsertion->BundleId.ToString(),
							*ExistingInsertion->AnchorCell.ToString(),
							*Insertion.BundleId.ToString(),
							*Insertion.AnchorCell.ToString());
						break;
					}
				}
				else
				{
					SeenInsertionsByProvingCell.Add(Insertion.ProvingCell, Insertion);
				}
			}

			return MakeSnapshotAssertionRecord(
				TEXT("RegionRequest.ForcedPlacementBundleInsertionContractValid"),
				ELayoutValidationAssertionKind::RequestContractValid,
				bPassed,
				RelatedIds,
				FailureReason);
		}

		FLayoutValidationAssertionRecord MakeRequestRequiredRouteConstraintAssertion(
			const FLayoutRegionSolveRequest& Request)
		{
			TArray<FLayoutId> RelatedIds;
			if (Request.EffectiveSnapshotId != NAME_None)
			{
				RelatedIds.Add(Request.EffectiveSnapshotId);
			}
			if (Request.RootPlacementPolicyId != NAME_None)
			{
				RelatedIds.Add(Request.RootPlacementPolicyId);
			}
			if (Request.RootCandidateId != NAME_None)
			{
				RelatedIds.Add(Request.RootCandidateId);
			}
			if (Request.RootSolveId != NAME_None)
			{
				RelatedIds.Add(Request.RootSolveId);
			}

			const auto HasPlannedCell =
				[&Request](const FIntVector& Cell)
				{
					return Request.PlannedCells.ContainsByPredicate(
						[&Cell](const FLayoutPlannedCell& PlannedCell)
						{
							return PlannedCell.Cell == Cell;
						});
				};

			bool bPassed = true;
			FString FailureReason;
			TMap<FIntVector, ELayoutCellIntent> SeenIntentsByCell;
			TMap<FString, FGameplayTag> SeenTraversalChannelsByCellFace;
			for (const FLayoutRouteConstraintRecord& RouteConstraint : Request.RequiredRouteConstraints)
			{
				if (RouteConstraint.ConstraintId != NAME_None)
				{
					RelatedIds.AddUnique(RouteConstraint.ConstraintId);
				}

				if (!Request.PlannedCells.IsEmpty() && !HasPlannedCell(RouteConstraint.Cell))
				{
					bPassed = false;
					FailureReason = FString::Printf(
						TEXT("Region solve request carries required route constraint '%s' on cell %s, but that cell is not present in the supplied planned-cell set.%s Request-owned route constraints on override-planned requests must target authored planned cells."),
						RouteConstraint.ConstraintId != NAME_None ? *RouteConstraint.ConstraintId.ToString() : TEXT("<unnamed>"),
						*RouteConstraint.Cell.ToString(),
						*BuildRootRequestBreadcrumb(Request));
					break;
				}

				if (RouteConstraint.FaceRequirements.IsEmpty())
				{
					bPassed = false;
					FailureReason = FString::Printf(
						TEXT("Region solve request carries required route constraint '%s' on cell %s without any face requirements.%s Request-owned route constraints must preserve at least one constrained traversal face before recursive solving begins."),
						RouteConstraint.ConstraintId != NAME_None ? *RouteConstraint.ConstraintId.ToString() : TEXT("<unnamed>"),
						*RouteConstraint.Cell.ToString(),
						*BuildRootRequestBreadcrumb(Request));
					break;
				}

				if (const ELayoutCellIntent* ExistingIntent = SeenIntentsByCell.Find(RouteConstraint.Cell))
				{
					if (*ExistingIntent != RouteConstraint.Intent)
					{
						bPassed = false;
						FailureReason = FString::Printf(
							TEXT("Region solve request carries conflicting required route constraints on cell %s.%s Existing route intent is %s, but another route constraint uses %s. Request-owned route constraints must keep one deterministic route intent per constrained cell."),
							*RouteConstraint.Cell.ToString(),
							*BuildRootRequestBreadcrumb(Request),
							*StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(static_cast<int64>(*ExistingIntent)),
							*StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(static_cast<int64>(RouteConstraint.Intent)));
						break;
					}
				}
				else
				{
					SeenIntentsByCell.Add(RouteConstraint.Cell, RouteConstraint.Intent);
				}

				for (const FLayoutRouteFaceRequirement& Requirement : RouteConstraint.FaceRequirements)
				{
					if (!Requirement.TraversalChannel.IsValid())
					{
						bPassed = false;
						FailureReason = FString::Printf(
							TEXT("Region solve request carries required route constraint '%s' on cell %s with face %s missing a traversal channel.%s Request-owned route constraints must preserve the traversal channel each constrained face expects."),
							RouteConstraint.ConstraintId != NAME_None ? *RouteConstraint.ConstraintId.ToString() : TEXT("<unnamed>"),
							*RouteConstraint.Cell.ToString(),
							*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Requirement.FaceDirection)),
							*BuildRootRequestBreadcrumb(Request));
						break;
					}

					const FString CellFaceKey = FString::Printf(
						TEXT("%s|%d"),
						*RouteConstraint.Cell.ToString(),
						static_cast<int32>(Requirement.FaceDirection));
					if (const FGameplayTag* ExistingTraversalChannel =
						SeenTraversalChannelsByCellFace.Find(CellFaceKey))
					{
						if (*ExistingTraversalChannel != Requirement.TraversalChannel)
						{
							bPassed = false;
							FailureReason = FString::Printf(
								TEXT("Region solve request carries conflicting required route face requirements on cell %s face %s.%s Existing traversal channel is '%s', but another route requirement uses '%s'. Request-owned route constraints must keep one deterministic traversal channel per constrained cell face."),
								*RouteConstraint.Cell.ToString(),
								*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Requirement.FaceDirection)),
								*BuildRootRequestBreadcrumb(Request),
								*ExistingTraversalChannel->ToString(),
								*Requirement.TraversalChannel.ToString());
							break;
						}
					}
					else
					{
						SeenTraversalChannelsByCellFace.Add(CellFaceKey, Requirement.TraversalChannel);
					}
				}

				if (!bPassed)
				{
					break;
				}
			}

			return MakeSnapshotAssertionRecord(
				TEXT("RegionRequest.RequiredRouteConstraintContractValid"),
				ELayoutValidationAssertionKind::RequestContractValid,
				bPassed,
				RelatedIds,
				FailureReason);
		}
	}

	void PopulateStandaloneRequestBase(
		FLayoutRegionSolveRequest& Request,
		const int32 SnapshotSchemaVersion,
		const int32 Seed,
		const FString& RegionDebugPath,
		const FLayoutSolverExecutionSettings& ExecutionSettings,
		const FLayoutId RootPlacementPolicyId,
		const FLayoutId RootCandidateId,
		const FLayoutId RootSolveId,
		const int32 TemplatePlacementZOffsetBlocks,
		const ELayoutWorldBindingPlacementKind RootPlacementKind,
		const FLayoutWorldBindingPlacementPolicy& WorldBindingPlacementPolicy)
	{
		Request.SnapshotSchemaVersion = SnapshotSchemaVersion;
		Request.RegionDebugPath = RegionDebugPath;
		Request.Seed = Seed;
		Request.RootSolveId = RootSolveId;
		Request.RootPlacementPolicyId = RootPlacementPolicyId;
		Request.RootCandidateId = RootCandidateId;
		Request.TemplatePlacementZOffsetBlocks = TemplatePlacementZOffsetBlocks;
		Request.RootPlacementKind = RootPlacementKind;
		Request.WorldBindingPlacementPolicy = WorldBindingPlacementPolicy;
		Request.ExecutionSettings = ExecutionSettings;
	}

	void AppendStandaloneRequestBaseAssertions(FLayoutRegionSolveRequest& Request)
	{
		Request.ValidationAssertions.Add(MakeRequestRootPublicationIdentityAssertion(Request));
		Request.ValidationAssertions.Add(MakeRequestTemplatePlacementOffsetAssertion(Request));
		Request.ValidationAssertions.Add(MakeRequestSteppedTerrainSupportAssertion(Request));
		Request.ValidationAssertions.Add(MakeRequestSteppedTerrainNeighborDeltaAssertion(Request));
		Request.ValidationAssertions.Add(MakeRequestSteppedTerrainCoverageAssertion(Request));
	}

	void RefreshStandaloneForcedPlacementBundleInsertionAssertions(FLayoutRegionSolveRequest& Request)
	{
		Request.ValidationAssertions.RemoveAll([](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == TEXT("RegionRequest.ForcedPlacementBundleInsertionContractValid");
		});
		Request.ValidationAssertions.Add(MakeRequestForcedPlacementBundleInsertionAssertion(Request));
	}

	void RefreshStandaloneLiveCompositeBundleAssertions(FLayoutRegionSolveRequest& Request)
	{
		Request.ValidationAssertions.RemoveAll([](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == TEXT("RegionRequest.LiveCompositeBundlePlacementSupported");
		});
		Request.ValidationAssertions.Add(MakeRequestLiveCompositeBundleAssertion(Request));
	}

	void RefreshStandaloneRequiredRouteConstraintAssertions(FLayoutRegionSolveRequest& Request)
	{
		Request.ValidationAssertions.RemoveAll([](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == TEXT("RegionRequest.RequiredRouteConstraintContractValid");
		});
		Request.ValidationAssertions.Add(MakeRequestRequiredRouteConstraintAssertion(Request));
	}

	void RefreshStandaloneSteppedTerrainAssertions(FLayoutRegionSolveRequest& Request)
	{
		Request.ValidationAssertions.RemoveAll([](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == TEXT("RegionRequest.SteppedProfileCapabilityContractValid")
				|| Assertion.AssertionId == TEXT("RegionRequest.SteppedTerrainSupportContractValid")
				|| Assertion.AssertionId == TEXT("RegionRequest.SteppedTerrainNeighborDeltaContractValid")
				|| Assertion.AssertionId == TEXT("RegionRequest.SteppedTerrainSupportCoverageContractValid");
		});
		Request.ValidationAssertions.Add(MakeRequestSteppedProfileCapabilityAssertion(Request));
		Request.ValidationAssertions.Add(MakeRequestSteppedTerrainSupportAssertion(Request));
		Request.ValidationAssertions.Add(MakeRequestSteppedTerrainNeighborDeltaAssertion(Request));
		Request.ValidationAssertions.Add(MakeRequestSteppedTerrainCoverageAssertion(Request));
	}

	void RefreshStandaloneSnapshotContractAssertions(FLayoutRegionSolveRequest& Request)
	{
		Request.ValidationAssertions.RemoveAll([](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == TEXT("RegionRequest.SnapshotContractInitialized");
		});
		Request.ValidationAssertions.Add(MakeRequestSnapshotContractAssertion(Request));
	}

	void FinalizeStandaloneRequestSnapshots(FLayoutRegionSolveRequest& Request)
	{
		Request.EffectiveSnapshotId = FLayoutId(*FString::Printf(
			TEXT("%s__%s__%s__Seams%d"),
			*Request.RegionDebugPath,
			*Request.ProfileSnapshot.DebugName.ToString(),
			*Request.ContentSetSnapshot.DebugName.ToString(),
			(Request.ProfileSnapshot.bSupportsSteppedTerrainSolve
				&& Request.ProfileSnapshot.bEnableTerrainSeams) ? 1 : 0));
		Request.ProofRecords.Append(Request.ContentSetSnapshot.ProofRecords);
		Request.ProofRecords.Append(Request.ModuleCatalog.ProofRecords);
		Request.ProofRecords.Append(Request.ProfileSnapshot.ProofRecords);
		Request.ValidationAssertions.Append(Request.ContentSetSnapshot.ValidationAssertions);
		Request.ValidationAssertions.Append(Request.ModuleCatalog.ValidationAssertions);
		Request.ValidationAssertions.Append(Request.ProfileSnapshot.ValidationAssertions);
		Request.ValidationAssertions.Add(MakeRequestSharedCellSizeAssertion(Request));
		AppendStandaloneRequestBaseAssertions(Request);
		RefreshStandaloneSteppedTerrainAssertions(Request);
		RefreshStandaloneForcedPlacementBundleInsertionAssertions(Request);
		RefreshStandaloneLiveCompositeBundleAssertions(Request);
		RefreshStandaloneRequiredRouteConstraintAssertions(Request);
		RefreshStandaloneSnapshotContractAssertions(Request);
	}
}
