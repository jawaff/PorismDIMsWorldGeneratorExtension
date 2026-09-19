// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Misc/Crc.h"
#include "Layout/Async/LayoutBackgroundSolveTypes.h"
#include "Layout/Async/LayoutChildSolveHandoff.h"
#include "Layout/Contracts/LayoutContractPipeline.h"
#include "Layout/Contracts/LayoutContractPlacementCandidates.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Solver/LayoutRegionScheduleSolverFacade.h"

/** Parent-yield/resume carrier used when child prerequisites are scheduled as independent jobs. */
struct FLayoutParentResumeArtifact
{
	/** Stable parent contract id waiting on child/residual prerequisites. */
	FLayoutId ParentContractId;

	/** Stable parent region path used for diagnostics and late-completion filtering. */
	FString ParentRegionDebugPath;

	/** Frozen proof contract used by resume merge/audit without reopening the prepared proof plan. */
	LayoutRegionScheduleSolverFacade::FNegotiatedProofScheduleContract ProofContract;

	/** Parent request snapshot used by resume normalization without reopening the prepared proof plan. */
	FLayoutRegionSolveRequest ParentRequest;

	/** Ordered child region paths. Resume merge order must not depend on completion order. */
	TArray<FString> OrderedChildRegionDebugPaths;

	/** Ordered child request snapshots matching OrderedChildRegionDebugPaths. */
	TArray<FLayoutRegionSolveRequest> OrderedChildRequests;

	/** Ordered child job handles. Resume merge order must not depend on completion order. */
	TArray<FLayoutBackgroundSolveHandle> OrderedChildJobHandles;

	/** Ordered child certificates matching OrderedChildRegionDebugPaths. */
	TArray<FLayoutChildSolveCertificate> OrderedChildCertificates;

	/** Ordered child witness bundles matching OrderedChildRegionDebugPaths. */
	TArray<FLayoutCertifiedChildWitnessBundle> OrderedChildWitnessBundles;

	/** Ordered child branch ordering keys used as deterministic certificate/witness tiebreakers. */
	TArray<FLayoutContractCandidateOrderingKey> OrderedChildOrderingKeys;

	/** Completed child proof results keyed by child region path and consumed in OrderedChildRegionDebugPaths order. */
	TMap<FString, FLayoutRegionSolveResult> CompletedChildResultsByRegion;

	/** True when parent-resume publication expects a merged active-cell source before solved-artifact write inputs can publish. */
	bool bRequiresMergedActiveCellProvenance = false;

	/** Optional merged active-cell provenance for the parent plus certified children, produced only by an authoritative merge artifact. */
	TArray<FLayoutContractActiveCellRecord> MergedActiveCells;

	/** Debug reason explaining why parent proof yielded instead of occupying a worker slot. */
	FString YieldReason;

	/** Adds one child job in the already-certified parent contract order. */
	void AddOrderedChildJob(
		const FString& ChildRegionDebugPath,
		const FLayoutRegionSolveRequest& ChildRequest,
		const FLayoutBackgroundSolveHandle& ChildJobHandle,
		const FLayoutChildSolveHandoff& ChildHandoff,
		const FLayoutContractCandidateOrderingKey& ChildOrderingKey)
	{
		OrderedChildRegionDebugPaths.Add(ChildRegionDebugPath);
		OrderedChildRequests.Add(ChildRequest);
		OrderedChildJobHandles.Add(ChildJobHandle);
		OrderedChildCertificates.Add(ChildHandoff.ProofCertificate);
		OrderedChildWitnessBundles.Add(ChildHandoff.CertifiedWitnessBundle);
		OrderedChildOrderingKeys.Add(ChildOrderingKey);
	}

	/** Records one completed child proof result by certified child id. */
	void RecordCompletedChildResult(const FLayoutRegionSolveResult& ChildResult)
	{
		CompletedChildResultsByRegion.Add(ChildResult.RegionDebugPath, ChildResult);
	}

	/** Returns true when every ordered child handle has produced one successful child result. */
	bool HasAllOrderedChildResults() const
	{
		return OrderedChildJobHandles.Num() > 0
			&& CompletedChildResultsByRegion.Num() == OrderedChildJobHandles.Num();
	}

	/** Builds child proof results in certified artifact order instead of worker completion order. */
	bool BuildOrderedChildResults(TArray<FLayoutRegionSolveResult>& OutChildResults, FString& OutFailureReason) const
	{
		OutChildResults.Reset();
		if (!ValidateStableChildOrder(OutFailureReason))
		{
			return false;
		}

		OutChildResults.Reserve(OrderedChildRegionDebugPaths.Num());
		for (const FString& ChildRegionDebugPath : OrderedChildRegionDebugPaths)
		{
			const FLayoutRegionSolveResult* ChildResult = CompletedChildResultsByRegion.Find(ChildRegionDebugPath);
			if (ChildResult == nullptr)
			{
				OutFailureReason = FString::Printf(
					TEXT("Parent resume artifact is missing certified child proof result for region '%s'."),
					*ChildRegionDebugPath);
				return false;
			}
			OutChildResults.Add(*ChildResult);
		}

		return true;
	}

	/** Validates optional merged active-cell provenance when a parent-resume publication requires it. */
	bool ValidateMergedActiveCellProvenance(FString& OutFailureReason) const
	{
		OutFailureReason.Reset();
		if (!bRequiresMergedActiveCellProvenance)
		{
			return true;
		}
		if (MergedActiveCells.IsEmpty())
		{
			OutFailureReason = TEXT("Parent resume artifact requires merged active-cell provenance before solved-artifact publication.");
			return false;
		}
		if (!FLayoutContractPipeline::ValidateActiveCellRecords(MergedActiveCells, OutFailureReason))
		{
			return false;
		}
		const bool bHasRealActiveCell = !MergedActiveCells.IsEmpty();
		if (!bHasRealActiveCell)
		{
			OutFailureReason = TEXT("Parent resume artifact merged active-cell provenance requires at least one real active cell.");
			return false;
		}
		return true;
	}

	/** Returns true when the artifact has one handle for each certified child id in the same stable order. */
	bool ValidateStableChildOrder(FString& OutFailureReason) const
	{
		OutFailureReason.Reset();
		if (ParentContractId.IsNone())
		{
			OutFailureReason = TEXT("Parent resume artifact is missing ParentContractId.");
			return false;
		}

		if (ProofContract.ParentRequest.RegionDebugPath.IsEmpty())
		{
			OutFailureReason = TEXT("Parent resume artifact is missing proof contract.");
			return false;
		}

		if (ParentRequest.RegionDebugPath.IsEmpty())
		{
			OutFailureReason = TEXT("Parent resume artifact is missing parent request.");
			return false;
		}

		if (!ValidateMergedActiveCellProvenance(OutFailureReason))
		{
			return false;
		}

		if (OrderedChildRegionDebugPaths.Num() != OrderedChildRequests.Num()
			|| OrderedChildRegionDebugPaths.Num() != OrderedChildJobHandles.Num()
			|| OrderedChildRegionDebugPaths.Num() != OrderedChildCertificates.Num()
			|| OrderedChildRegionDebugPaths.Num() != OrderedChildWitnessBundles.Num()
			|| OrderedChildRegionDebugPaths.Num() != OrderedChildOrderingKeys.Num())
		{
			OutFailureReason = TEXT("Parent resume artifact child id/request/handle/certificate/witness counts do not match.");
			return false;
		}

		const auto BuildWitnessOrderingId = [](const FLayoutCertifiedChildWitnessBundle& Bundle)
		{
			FString Material;
			Material.Reserve(1024);
			auto AppendName = [&Material](const FLayoutId Name)
			{
				Material += Name.ToString();
				Material += TEXT("|");
			};
			auto AppendNames = [&AppendName](const TArray<FLayoutId>& Names)
			{
				TArray<FLayoutId> SortedNames = Names;
				SortedNames.Sort([](const FLayoutId& A, const FLayoutId& B)
				{
					return A.LexicalLess(B);
				});
				for (const FLayoutId Name : SortedNames)
				{
					AppendName(Name);
				}
			};

			AppendName(Bundle.SourceContentEntryId);
			AppendName(Bundle.StableChildId);
			AppendName(Bundle.BranchId);
			AppendName(Bundle.SelectedClosureSpanCapabilityId);
			AppendName(Bundle.SelectedSeamCapabilityId);
			AppendName(Bundle.SelectedSeamWitnessId);
			AppendNames(Bundle.SelectedEndpointCapabilityIds);
			AppendNames(Bundle.SelectedTraversalCapabilityIds);
			AppendNames(Bundle.SelectedVerticalCapabilityIds);
			AppendNames(Bundle.DelegatedFeatureRequirementIds);
			AppendNames(Bundle.DelegatedClosureRequirementIds);
			AppendNames(Bundle.DelegatedSeamWitnessIds);
			AppendNames(Bundle.DelegatedJunctionWitnessIds);
			AppendNames(Bundle.DelegatedHostVerticalAccessWitnessIds);
			AppendNames(Bundle.AssertionIds);
			AppendNames(Bundle.ParentAssertionIds);
			return FLayoutId(*FString::Printf(
				TEXT("ChildWitness.%08X"),
				FCrc::StrCrc32(*Material)));
		};

		const auto AreNameArraysEquivalent = [](const TArray<FLayoutId>& Left, const TArray<FLayoutId>& Right)
		{
			TArray<FLayoutId> SortedLeft = Left;
			TArray<FLayoutId> SortedRight = Right;
			SortedLeft.Sort([](const FLayoutId& A, const FLayoutId& B)
			{
				return A.LexicalLess(B);
			});
			SortedRight.Sort([](const FLayoutId& A, const FLayoutId& B)
			{
				return A.LexicalLess(B);
			});
			return SortedLeft == SortedRight;
		};

		TSet<FLayoutId> SeenOrderingIds;
		for (int32 ChildIndex = 0; ChildIndex < OrderedChildRegionDebugPaths.Num(); ++ChildIndex)
		{
			if (OrderedChildRegionDebugPaths[ChildIndex].IsEmpty())
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child id %d is empty."), ChildIndex);
				return false;
			}

			if (OrderedChildRequests[ChildIndex].RegionDebugPath != OrderedChildRegionDebugPaths[ChildIndex])
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child request %d does not match ordered child id."), ChildIndex);
				return false;
			}

			if (!OrderedChildJobHandles[ChildIndex].IsValid())
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child handle %d is invalid."), ChildIndex);
				return false;
			}

			if (OrderedChildCertificates[ChildIndex].CertificateId.IsNone()
				|| OrderedChildCertificates[ChildIndex].InputHash == 0)
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child certificate %d is missing id/hash."), ChildIndex);
				return false;
			}

			if (OrderedChildWitnessBundles[ChildIndex].IsEmpty())
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child witness bundle %d is empty."), ChildIndex);
				return false;
			}

			if (!OrderedChildWitnessBundles[ChildIndex].SourceContentEntryId.IsNone()
				&& !OrderedChildRequests[ChildIndex].SourceContentEntryId.IsNone()
				&& OrderedChildWitnessBundles[ChildIndex].SourceContentEntryId != OrderedChildRequests[ChildIndex].SourceContentEntryId)
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child witness %d source content entry does not match request."), ChildIndex);
				return false;
			}

			const FLayoutRegionSolveRequest& ChildRequest = OrderedChildRequests[ChildIndex];
			const FLayoutCertifiedChildWitnessBundle& WitnessBundle = OrderedChildWitnessBundles[ChildIndex];
			if (!ChildRequest.CertifiedSelectedEndpointCapabilityIds.IsEmpty()
				&& !AreNameArraysEquivalent(ChildRequest.CertifiedSelectedEndpointCapabilityIds, WitnessBundle.SelectedEndpointCapabilityIds))
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child witness %d selected endpoint ids do not match request."), ChildIndex);
				return false;
			}
			if (ChildRequest.bHasCertifiedSelectedTraversalCapabilityArtifact
				&& !AreNameArraysEquivalent(ChildRequest.CertifiedSelectedTraversalCapabilityIds, WitnessBundle.SelectedTraversalCapabilityIds))
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child witness %d selected traversal ids do not match request."), ChildIndex);
				return false;
			}
			if (!ChildRequest.CertifiedSelectedVerticalCapabilityIds.IsEmpty()
				&& !AreNameArraysEquivalent(ChildRequest.CertifiedSelectedVerticalCapabilityIds, WitnessBundle.SelectedVerticalCapabilityIds))
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child witness %d selected vertical ids do not match request."), ChildIndex);
				return false;
			}
			if (!ChildRequest.CertifiedSelectedSeamCapabilityId.IsNone()
				&& ChildRequest.CertifiedSelectedSeamCapabilityId != WitnessBundle.SelectedSeamCapabilityId)
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child witness %d selected seam capability id does not match request."), ChildIndex);
				return false;
			}
			if (!ChildRequest.CertifiedSelectedSeamWitnessId.IsNone()
				&& ChildRequest.CertifiedSelectedSeamWitnessId != WitnessBundle.SelectedSeamWitnessId)
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child witness %d selected seam witness id does not match request."), ChildIndex);
				return false;
			}
			if (!ChildRequest.CertifiedDelegatedSeamWitnessIds.IsEmpty()
				&& !AreNameArraysEquivalent(ChildRequest.CertifiedDelegatedSeamWitnessIds, WitnessBundle.DelegatedSeamWitnessIds))
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child witness %d delegated seam witness ids do not match request."), ChildIndex);
				return false;
			}
			if (!ChildRequest.CertifiedDelegatedJunctionWitnessIds.IsEmpty()
				&& !AreNameArraysEquivalent(ChildRequest.CertifiedDelegatedJunctionWitnessIds, WitnessBundle.DelegatedJunctionWitnessIds))
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child witness %d delegated junction witness ids do not match request."), ChildIndex);
				return false;
			}
			if (!ChildRequest.CertifiedDelegatedHostVerticalAccessWitnessIds.IsEmpty()
				&& !AreNameArraysEquivalent(ChildRequest.CertifiedDelegatedHostVerticalAccessWitnessIds, WitnessBundle.DelegatedHostVerticalAccessWitnessIds))
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child witness %d delegated host VerticalAccess witness ids do not match request."), ChildIndex);
				return false;
			}
			if (ChildRequest.bHasCertifiedParentAssertionSubsetArtifact
				&& !AreNameArraysEquivalent(ChildRequest.CertifiedParentAssertionSubsetIds, WitnessBundle.ParentAssertionIds))
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child witness %d parent assertion subset ids do not match request."), ChildIndex);
				return false;
			}

			if (OrderedChildOrderingKeys[ChildIndex].ChildCertificateId != OrderedChildCertificates[ChildIndex].CertificateId
				|| OrderedChildOrderingKeys[ChildIndex].ChildCertificateInputHash != OrderedChildCertificates[ChildIndex].InputHash)
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child ordering key %d does not match certificate."), ChildIndex);
				return false;
			}
			const FLayoutId ExpectedWitnessOrderingId = BuildWitnessOrderingId(OrderedChildWitnessBundles[ChildIndex]);
			if (!OrderedChildOrderingKeys[ChildIndex].ChildWitnessOrderingId.IsNone()
				&& OrderedChildOrderingKeys[ChildIndex].ChildWitnessOrderingId != ExpectedWitnessOrderingId)
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child ordering key %d does not match witness ordering id."), ChildIndex);
				return false;
			}
			const FLayoutId OrderingId = OrderedChildOrderingKeys[ChildIndex].BuildOrderingId();
			if (SeenOrderingIds.Contains(OrderingId))
			{
				OutFailureReason = FString::Printf(TEXT("Parent resume artifact child ordering key %d duplicates another certified child branch."), ChildIndex);
				return false;
			}
			SeenOrderingIds.Add(OrderingId);
		}

		return true;
	}
};
