// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Testing/LayoutProfileJsonFixture.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionScheduleSolverFacade.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace LayoutRegionScheduleSolverFacade;
	using namespace PorismLayoutTestUtilities;

	UObject* CreateRewriteTestOuter(const TCHAR* BaseName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return CreatePackage(*PackageName);
	}

	bool AreRewriteGameplayTagContainersEquivalent(
		const FGameplayTagContainer& Left,
		const FGameplayTagContainer& Right)
	{
		return Left.HasAllExact(Right) && Right.HasAllExact(Left);
	}

	bool IsRewriteEarlierCell(const FIntVector& Left, const FIntVector& Right)
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
	}

	bool AreRewriteFaceRulesEquivalent(
		const FLayoutFaceRule& Left,
		const FLayoutFaceRule& Right)
	{
		return Left.Direction == Right.Direction
			&& Left.ConnectionTag == Right.ConnectionTag
			&& AreRewriteGameplayTagContainersEquivalent(Left.AllowedConnectionTags, Right.AllowedConnectionTags)
			&& Left.OccupancyPolicy == Right.OccupancyPolicy
			&& AreRewriteGameplayTagContainersEquivalent(Left.ConnectedTraversalChannels, Right.ConnectedTraversalChannels)
			&& Left.BoundaryRequirement == Right.BoundaryRequirement
			&& Left.bRequireMatchingYawWithFilledNeighbor == Right.bRequireMatchingYawWithFilledNeighbor;
	}

	bool AreRewriteLocalCellFaceRuleSnapshotsEquivalent(
		const FLayoutLocalCellFaceRuleSnapshot& Left,
		const FLayoutLocalCellFaceRuleSnapshot& Right)
	{
		if (Left.LocalCell != Right.LocalCell || Left.ExposedFaceRules.Num() != Right.ExposedFaceRules.Num())
		{
			return false;
		}

		for (int32 FaceIndex = 0; FaceIndex < Left.ExposedFaceRules.Num(); ++FaceIndex)
		{
			if (!AreRewriteFaceRulesEquivalent(Left.ExposedFaceRules[FaceIndex], Right.ExposedFaceRules[FaceIndex]))
			{
				return false;
			}
		}

		return true;
	}

	bool AreRewriteInternalAccessLinksEquivalent(
		const FLayoutInternalAccessLink& Left,
		const FLayoutInternalAccessLink& Right)
	{
		return Left.FromTraversalChannel == Right.FromTraversalChannel
			&& Left.ToTraversalChannel == Right.ToTraversalChannel
			&& Left.bBidirectional == Right.bBidirectional;
	}

	bool AreRewritePlacedModulesEquivalent(
		const FLayoutPlacedModule& Left,
		const FLayoutPlacedModule& Right)
	{
		return Left.Cell == Right.Cell
			&& Left.Intent == Right.Intent
			&& Left.Module == Right.Module
			&& Left.YawRotationSteps == Right.YawRotationSteps
			&& Left.SourceContentEntryId == Right.SourceContentEntryId;
	}

	bool AreRewritePlacedModuleArraysEquivalent(
		const TArray<FLayoutPlacedModule>& Left,
		const TArray<FLayoutPlacedModule>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (!AreRewritePlacedModulesEquivalent(Left[Index], Right[Index]))
			{
				return false;
			}
		}

		return true;
	}

	bool AreRewriteSteppedTerrainSupportMapsEquivalent(
		const FLayoutSteppedTerrainSupportMap& Left,
		const FLayoutSteppedTerrainSupportMap& Right)
	{
		if (Left.SharedCellHeightInBlocks != Right.SharedCellHeightInBlocks
			|| Left.MaximumObservedNeighborHeightDelta != Right.MaximumObservedNeighborHeightDelta
			|| Left.SupportSamples.Num() != Right.SupportSamples.Num()
			|| Left.AdjacencySteps.Num() != Right.AdjacencySteps.Num())
		{
			return false;
		}

		for (int32 SupportSampleIndex = 0; SupportSampleIndex < Left.SupportSamples.Num(); ++SupportSampleIndex)
		{
			if (Left.SupportSamples[SupportSampleIndex].LocalCell != Right.SupportSamples[SupportSampleIndex].LocalCell
				|| Left.SupportSamples[SupportSampleIndex].SupportSurfaceZ != Right.SupportSamples[SupportSampleIndex].SupportSurfaceZ)
			{
				return false;
			}
		}

		for (int32 AdjacencyStepIndex = 0; AdjacencyStepIndex < Left.AdjacencySteps.Num(); ++AdjacencyStepIndex)
		{
			if (Left.AdjacencySteps[AdjacencyStepIndex].FromCell != Right.AdjacencySteps[AdjacencyStepIndex].FromCell
				|| Left.AdjacencySteps[AdjacencyStepIndex].ToCell != Right.AdjacencySteps[AdjacencyStepIndex].ToCell
				|| Left.AdjacencySteps[AdjacencyStepIndex].StepHeightBlocks != Right.AdjacencySteps[AdjacencyStepIndex].StepHeightBlocks)
			{
				return false;
			}
		}

		return true;
	}

	bool AreRewriteRouteFaceRequirementArraysEquivalent(
		const TArray<FLayoutRouteFaceRequirement>& Left,
		const TArray<FLayoutRouteFaceRequirement>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}

		for (const FLayoutRouteFaceRequirement& LeftRequirement : Left)
		{
			const FLayoutRouteFaceRequirement* MatchingRequirement =
				Right.FindByPredicate(
					[&LeftRequirement](const FLayoutRouteFaceRequirement& RightRequirement)
					{
						return RightRequirement.FaceDirection == LeftRequirement.FaceDirection
							&& RightRequirement.TraversalChannel == LeftRequirement.TraversalChannel;
					});
			if (MatchingRequirement == nullptr)
			{
				return false;
			}
		}

		return true;
	}

	bool AreRewriteRouteConstraintsEquivalent(
		const FLayoutRouteConstraintRecord& Left,
		const FLayoutRouteConstraintRecord& Right)
	{
		return Left.ConstraintId == Right.ConstraintId
			&& Left.Cell == Right.Cell
			&& Left.Intent == Right.Intent
			&& Left.bScoreAsMainRoute == Right.bScoreAsMainRoute
			&& AreRewriteRouteFaceRequirementArraysEquivalent(
				Left.FaceRequirements,
				Right.FaceRequirements);
	}

	bool AreRewriteRouteConstraintArraysEquivalent(
		const TArray<FLayoutRouteConstraintRecord>& Left,
		const TArray<FLayoutRouteConstraintRecord>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}

		for (const FLayoutRouteConstraintRecord& LeftConstraint : Left)
		{
			const FLayoutRouteConstraintRecord* MatchingConstraint =
				Right.FindByPredicate(
					[&LeftConstraint](const FLayoutRouteConstraintRecord& RightConstraint)
					{
						return AreRewriteRouteConstraintsEquivalent(
							LeftConstraint,
							RightConstraint);
					});
			if (MatchingConstraint == nullptr)
			{
				return false;
			}
		}

		return true;
	}

	bool AreRewriteForcedPlacementBundleInsertionsEquivalent(
		const TArray<FLayoutForcedPlacementBundleInsertion>& Left,
		const TArray<FLayoutForcedPlacementBundleInsertion>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}

		for (const FLayoutForcedPlacementBundleInsertion& LeftInsertion : Left)
		{
			const FLayoutForcedPlacementBundleInsertion* MatchingInsertion =
				Right.FindByPredicate(
					[&LeftInsertion](const FLayoutForcedPlacementBundleInsertion& RightInsertion)
					{
						return RightInsertion.BundleId == LeftInsertion.BundleId
							&& RightInsertion.AnchorCell == LeftInsertion.AnchorCell
							&& RightInsertion.ProvingCell == LeftInsertion.ProvingCell;
					});
			if (MatchingInsertion == nullptr)
			{
				return false;
			}
		}

		return true;
	}

	void PopulateRewriteSteppedTerrainSupportMap(
		FLayoutSteppedTerrainSupportMap& SupportMap)
	{
		SupportMap = FLayoutSteppedTerrainSupportMap();
		SupportMap.SharedCellHeightInBlocks = 16;

		FLayoutSteppedTerrainSupportSample& FirstSupportSample =
			SupportMap.SupportSamples.AddDefaulted_GetRef();
		FirstSupportSample.LocalCell = FIntVector(0, 0, 0);
		FirstSupportSample.SupportSurfaceZ = 64;

		FLayoutSteppedTerrainSupportSample& SecondSupportSample =
			SupportMap.SupportSamples.AddDefaulted_GetRef();
		SecondSupportSample.LocalCell = FIntVector(1, 0, 0);
		SecondSupportSample.SupportSurfaceZ = 67;

		FLayoutSteppedTerrainAdjacencyStep& AdjacencyStep =
			SupportMap.AdjacencySteps.AddDefaulted_GetRef();
		AdjacencyStep.FromCell = FIntVector(0, 0, 0);
		AdjacencyStep.ToCell = FIntVector(1, 0, 0);
		AdjacencyStep.StepHeightBlocks = 3;

		SupportMap.MaximumObservedNeighborHeightDelta = 3;
	}

	FLayoutId BuildRewriteRequestPlacementBundleId(
		const FLayoutRegionSolveRequest& Request,
		const int32 ModuleIndex)
	{
		return FLayoutId(*FString::Printf(
			TEXT("%s.Bundle.%d.%s"),
			*Request.EffectiveSnapshotId.ToString(),
			ModuleIndex,
			*Request.ModuleCatalog.Modules[ModuleIndex].SnapshotId.ToString()));
	}

	void PopulateRewriteRouteConstraint(
		FLayoutRouteConstraintRecord& RouteConstraint,
		const FLayoutId ConstraintId,
		const FIntVector& Cell,
		const ELayoutCellIntent Intent)
	{
		RouteConstraint.ConstraintId = ConstraintId;
		RouteConstraint.Cell = Cell;
		RouteConstraint.Intent = Intent;
		RouteConstraint.bScoreAsMainRoute = true;
		FLayoutRouteFaceRequirement& FaceRequirement =
			RouteConstraint.FaceRequirements.AddDefaulted_GetRef();
		FaceRequirement.FaceDirection = ELayoutFaceDirection::PosX;
		FaceRequirement.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	}

	void AddRewriteSnapshotValidationMessages(
		FAutomationTestBase& Test,
		const TCHAR* Label,
		const FLayoutValidationResult& Validation)
	{
		if (Validation.IsValid())
		{
			return;
		}

		for (const FLayoutValidationMessage& Message : Validation.Messages)
		{
			Test.AddInfo(FString::Printf(TEXT("%s validation: %s"), Label, *Message.Message));
		}
	}

	void SortRewritePlacedModulesForComparison(TArray<FLayoutPlacedModule>& Placements)
	{
		Placements.Sort([](const FLayoutPlacedModule& Left, const FLayoutPlacedModule& Right)
		{
			return IsRewriteEarlierCell(Left.Cell, Right.Cell);
		});
	}

	TArray<FLayoutPlacedModule> BuildRewriteTranslatedPlacedModules(
		const FLayoutSolveResult& SolveResult,
		const FIntVector& RegionOffset)
	{
		TArray<FLayoutPlacedModule> TranslatedPlacements = SolveResult.Placements;
		for (FLayoutPlacedModule& Placement : TranslatedPlacements)
		{
			Placement.Cell += RegionOffset;
		}

		SortRewritePlacedModulesForComparison(TranslatedPlacements);
		return TranslatedPlacements;
	}

	FString DescribeRewritePlacedModules(
		const TArray<FLayoutPlacedModule>& Placements)
	{
		TArray<FString> Descriptions;
		Descriptions.Reserve(Placements.Num());
		for (const FLayoutPlacedModule& Placement : Placements)
		{
			Descriptions.Add(FString::Printf(
				TEXT("%s@(%d,%d,%d)#%s"),
				Placement.Module != nullptr
					? *Placement.Module->GetName()
					: TEXT("<null>"),
				Placement.Cell.X,
				Placement.Cell.Y,
				Placement.Cell.Z,
				*Placement.SourceContentEntryId.ToString()));
		}
		return FString::Join(Descriptions, TEXT(", "));
	}

	void StripRewriteLiveSourcesFromChildRequestTemplate(FLayoutChildRequestTemplateSnapshot& TemplateSnapshot);

	void StripRewriteLiveSourcesFromContentSetSnapshot(FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot)
	{
		ContentSetSnapshot.SourceContentSet = nullptr;
		for (FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : ContentSetSnapshot.Entries)
		{
			if (EntrySnapshot.CompiledChildRequestTemplate.IsValid())
			{
				StripRewriteLiveSourcesFromChildRequestTemplate(*EntrySnapshot.CompiledChildRequestTemplate);
			}
		}
	}

	void StripRewriteLiveSourcesFromChildRequestTemplate(FLayoutChildRequestTemplateSnapshot& TemplateSnapshot)
	{
		TemplateSnapshot.ProfileSnapshot.SourceProfile = nullptr;
		StripRewriteLiveSourcesFromContentSetSnapshot(TemplateSnapshot.ContentSetSnapshot);
	}

	void StripRewriteLiveSourcesFromRequest(FLayoutRegionSolveRequest& Request)
	{
		Request.ProfileSnapshot.SourceProfile = nullptr;
		StripRewriteLiveSourcesFromContentSetSnapshot(Request.ContentSetSnapshot);
	}

	void StripRewriteLiveSourcesFromProofExecutionInputs(
		FRecursiveScheduleSolveContext& SolveContext,
		FNegotiatedProofScheduleContract& ProofContract)
	{
		StripRewriteLiveSourcesFromRequest(SolveContext.RootRequest);
		StripRewriteLiveSourcesFromRequest(ProofContract.ParentRequest);
		for (FLayoutRegionSolveRequest& ChildRequest : ProofContract.ChildRequests)
		{
			StripRewriteLiveSourcesFromRequest(ChildRequest);
		}

		AppendNegotiatedContractProofDiagnosticsToRequest(SolveContext.RootRequest);
		AppendNegotiatedContractProofDiagnosticsToRequest(ProofContract.ParentRequest);
		for (FLayoutRegionSolveRequest& ChildRequest : ProofContract.ChildRequests)
		{
			AppendNegotiatedContractProofDiagnosticsToRequest(ChildRequest);
		}
	}

	void InvalidateRewriteNegotiatedChildCommitmentCarrier(
		FNegotiatedProofScheduleContract& ProofContract)
	{
		const auto InvalidateContract = [](FLayoutNegotiatedChildResponsibilityContract& Contract)
		{
			Contract.RequiredHostProviderCount = 1;
			Contract.CountedParentProviderCount = 0;
			Contract.CountedChildProviderRegionDebugPaths.Reset();
		};

		for (FLayoutNegotiatedChildResponsibilityContract& Contract :
			ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts)
		{
			InvalidateContract(Contract);
		}
		for (FLayoutNegotiatedChildResponsibilityContract& Contract :
			ProofContract.ResponsibilityContracts)
		{
			InvalidateContract(Contract);
		}
		for (FLayoutDirectChildRegionCommitment& Commitment :
			ProofContract.DirectChildCommitments)
		{
			InvalidateContract(Commitment.NegotiatedResponsibilityContract);
		}
		for (FLayoutRegionSolveRequest& ChildRequest : ProofContract.ChildRequests)
		{
			for (FLayoutNegotiatedChildResponsibilityContract& Contract :
				ChildRequest.NegotiatedChildResponsibilityContracts)
			{
				InvalidateContract(Contract);
			}
		}
	}

	const FLayoutValidationAssertionRecord* FindRewriteAssertion(
		const TArray<FLayoutValidationAssertionRecord>& Assertions,
		const FLayoutId AssertionId)
	{
		return Assertions.FindByPredicate([AssertionId](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == AssertionId;
		});
	}

	bool DoesCapabilityExposeExactRoles(
		const TArray<ELayoutModuleRole>& ActualRoles,
		const TArray<ELayoutModuleRole>& ExpectedRoles)
	{
		if (ActualRoles.Num() != ExpectedRoles.Num())
		{
			return false;
		}

		for (const ELayoutModuleRole ExpectedRole : ExpectedRoles)
		{
			if (!ActualRoles.Contains(ExpectedRole))
			{
				return false;
			}
		}

		return true;
	}

	FLayoutCommittedEndpointAnchor MakeEndpointAnchor(
		const FLayoutId CommitmentId,
		const FIntVector& LocalCell,
		const ELayoutFaceDirection FaceDirection)
	{
		FLayoutCommittedEndpointAnchor Anchor;
		Anchor.CommitmentId = CommitmentId;
		Anchor.LocalCell = LocalCell;
		Anchor.FaceDirection = FaceDirection;
		Anchor.ConnectionTag = LayoutGameplayTags::FaceEntry;
		Anchor.TraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
		return Anchor;
	}

	FLayoutCommittedTraversalAnchor MakeTraversalAnchor(const FIntVector& Cell)
	{
		FLayoutCommittedTraversalAnchor Anchor;
		Anchor.Cell = Cell;
		Anchor.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
		return Anchor;
	}

	FLayoutPlannedCell MakePlannedCell(
		const FIntVector& Cell,
		const ELayoutCellIntent Intent)
	{
		FLayoutPlannedCell PlannedCell;
		PlannedCell.Cell = Cell;
		PlannedCell.Intent = Intent;
		return PlannedCell;
	}

	bool AreRewritePlannedCellArraysEquivalent(
		const TArray<FLayoutPlannedCell>& Left,
		const TArray<FLayoutPlannedCell>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}

		TArray<FLayoutPlannedCell> LeftSorted = Left;
		TArray<FLayoutPlannedCell> RightSorted = Right;
		const auto SortPredicate = [](const FLayoutPlannedCell& A, const FLayoutPlannedCell& B)
		{
			return IsRewriteEarlierCell(A.Cell, B.Cell);
		};
		LeftSorted.Sort(SortPredicate);
		RightSorted.Sort(SortPredicate);

		for (int32 Index = 0; Index < LeftSorted.Num(); ++Index)
		{
			if (LeftSorted[Index].Cell != RightSorted[Index].Cell
				|| LeftSorted[Index].Intent != RightSorted[Index].Intent)
			{
				return false;
			}
		}

		return true;
	}

	ULayoutModuleAsset* CreateRewriteOpenFillModule(
		UObject* Outer,
		const TCHAR* ModuleName,
		const TArray<ELayoutCellIntent>& SupportedIntents,
		const FGameplayTagContainer& HorizontalTraversalChannels = FGameplayTagContainer())
	{
		UChunkStructureTemplate* Template = CreateTemplate(
			Outer,
			*FString::Printf(TEXT("%s_Template"), ModuleName),
			FIntVector(1, 1, 1));
		return CreateModule(
			Outer,
			ModuleName,
			Template,
			SupportedIntents,
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				HorizontalTraversalChannels));
	}

	FLayoutSeamProviderIntent MakeRewriteSeamIntent(
		const TCHAR* SeamIntentId,
		const FGameplayTag& InterfaceFamily,
		const bool bCanOwnSeam,
		const bool bCanAcceptSeam)
	{
		FLayoutSeamProviderIntent Intent;
		Intent.SeamIntentId = FName(SeamIntentId);
		Intent.InterfaceFamily = InterfaceFamily;
		Intent.bCanOwnSeam = bCanOwnSeam;
		Intent.bCanAcceptSeam = bCanAcceptSeam;
		return Intent;
	}

	FLayoutNegotiatedChildResponsibilityContract MakeNegotiatedContract(
		const FString& ParentRegionPath,
		const FString& ChildRegionPath,
		const FLayoutCommittedEndpointAnchor& IngressAnchor,
		const FLayoutCommittedEndpointAnchor& EgressAnchor)
	{
		FLayoutNegotiatedChildResponsibilityContract Contract;
		Contract.ParentRegionDebugPath = ParentRegionPath;
		Contract.ChildRegionDebugPath = ChildRegionPath;
		Contract.HostVerticalAccessResponsibility = ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned;
		Contract.RequiredHostProviderCount = 1;
		Contract.CountedChildProviderRegionDebugPaths = {ChildRegionPath};
		Contract.bHasRequiredHostIngressAnchor = true;
		Contract.RequiredHostIngressAnchor = IngressAnchor;
		Contract.bHasRequiredHostEgressAnchor = true;
		Contract.RequiredHostEgressAnchor = EgressAnchor;
		Contract.RequiredChildInternalVerticalRouteCells = {
			FIntVector(0, 0, 0),
			FIntVector(1, 0, 0),
			FIntVector(1, 0, 1)
		};

		FLayoutNegotiatedLevelInterfaceContract& LowerInterface =
			Contract.CommittedParentChildInterfacesByLevel.AddDefaulted_GetRef();
		LowerInterface.Level = 0;
		LowerInterface.EndpointAnchors = {IngressAnchor};

		FLayoutNegotiatedLevelInterfaceContract& UpperInterface =
			Contract.CommittedParentChildInterfacesByLevel.AddDefaulted_GetRef();
		UpperInterface.Level = 1;
		UpperInterface.EndpointAnchors = {EgressAnchor};
		return Contract;
	}

	FNegotiatedDemandResult MakeSuccessfulDemandResult(
		const FString& ParentRegionPath,
		const FString& ChildRegionPath,
		const int32 Seed)
	{
		const FLayoutCommittedEndpointAnchor IngressAnchor =
			MakeEndpointAnchor(TEXT("Ingress"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX);
		const FLayoutCommittedEndpointAnchor EgressAnchor =
			MakeEndpointAnchor(TEXT("Egress"), FIntVector(1, 0, 1), ELayoutFaceDirection::PosX);

		FNegotiatedDemandResult Result;
		Result.bSucceeded = true;
		Result.ChildRegionDebugPath = ChildRegionPath;
		Result.ChildRequest.RegionDebugPath = ChildRegionPath;
		Result.ChildRequest.SourceParentRegionDebugPath = ParentRegionPath;
		Result.ChildRequest.SourceContentEntryId = FName(*FString::Printf(TEXT("%sEntry"), *ChildRegionPath));
		Result.ChildRequest.Seed = Seed;
		Result.ResponsibilitySet.ContactSet.FamilyId = FName(*FString::Printf(TEXT("%sFamily"), *ChildRegionPath));
		Result.ResponsibilitySet.ContactSet.EndpointCommitments = {IngressAnchor, EgressAnchor};
		Result.ResponsibilitySet.ContactSet.ParentTraversalIngressAnchors = {MakeTraversalAnchor(FIntVector(2, 0, 0))};
		Result.ResponsibilitySet.ResponsibilityContract =
			MakeNegotiatedContract(ParentRegionPath, ChildRegionPath, IngressAnchor, EgressAnchor);
		Result.ResponsibilitySet.BoundedParentProofEvidence.ChildRegionDebugPath =
			ChildRegionPath;
		Result.ResponsibilitySet.BoundedParentProofEvidence.ParentComponentId = 17;
		Result.ResponsibilitySet.BoundedParentProofEvidence.ConfirmedParentRouteSeedCells = {
			FIntVector(2, 0, 0)
		};
		Result.ResponsibilitySet.BoundedParentProofEvidence.ConfirmedParentContactCells = {
			FIntVector(2, 0, 0),
			FIntVector(3, 0, 1)
		};
		Result.ResponsibilitySet.BoundedParentProofEvidence.ConfirmedParentTraversalIngressCells = {
			FIntVector(2, 0, 0)
		};
		Result.ResponsibilitySet.BoundedParentProofEvidence.ConfirmedRequiredChildBundleSupportCells = {
			FIntVector(4, 0, 0)
		};
		Result.ResponsibilitySet.BoundedParentProofEvidence.PreservedRootExternalEndpointCommitmentIds = {
			FLayoutId(TEXT("RootDoor"))
		};
		Result.ChildVerticalAccessProvider.bSupportsVerticalAccess = true;
		Result.ChildVerticalAccessProvider.bContributesHostVerticalAccess = true;
		Result.ChildVerticalAccessProvider.VerticalAccessLocalCells =
			Result.ResponsibilitySet.ResponsibilityContract.RequiredChildInternalVerticalRouteCells;
		Result.ChildVerticalAccessProvider.ChildRegionDebugPath = ChildRegionPath;
		Result.ParentBranchCertificate.bHasProducedArtifact = true;
		Result.ParentBranchCertificate.ArtifactId = FLayoutId(*FString::Printf(TEXT("ParentBranchCertificate.%s"), *ChildRegionPath));
		Result.ParentBranchCertificate.ParentContractId = FLayoutId(*FString::Printf(TEXT("ParentContract.%s.%s"), *ParentRegionPath, *ChildRegionPath));
		Result.ParentBranchCertificate.ParentContractHash = static_cast<uint64>(Seed) + 1;
		Result.ParentBranchCertificate.BranchId = FLayoutId(*FString::Printf(TEXT("ParentBranch.%s"), *ChildRegionPath));
		Result.ParentBranchCertificate.ChildScoutResultId = FLayoutId(*FString::Printf(TEXT("ChildBranch.%s"), *ChildRegionPath));
		Result.ParentBranchCertificate.StableChildId = ChildRegionPath;
		Result.ParentBranchCertificate.SourceContentEntryId = Result.ChildRequest.SourceContentEntryId;
		Result.ParentBranchCertificate.CertificateInputHash = static_cast<uint64>(Seed) + 17;
		Result.ParentBranchCertificate.SelectedEndpointCapabilityIds = {
			FLayoutId(*FString::Printf(TEXT("Endpoint.%s.Ingress"), *ChildRegionPath))
		};
		Result.ParentBranchCertificate.SelectedVerticalCapabilityIds = {
			FLayoutId(*FString::Printf(TEXT("Vertical.%s.AnchorPair"), *ChildRegionPath))
		};
		Result.ParentBranchCertificate.SelectedClosureSpanCapabilityId =
			FLayoutId(*FString::Printf(TEXT("ClosureSpan.%s"), *ChildRegionPath));
		Result.ParentBranchCertificate.SelectedSeamCapabilityIds = {
			FLayoutId(*FString::Printf(TEXT("SeamCapability.%s"), *ChildRegionPath))
		};
		Result.ParentBranchCertificate.SelectedSeamWitnessIds = {
			FLayoutId(*FString::Printf(TEXT("SeamWitness.%s"), *ChildRegionPath))
		};
		return Result;
	}

	FNegotiatedDemandResult MakeFailedOptionalDemandResult(
		const FString& ParentRegionPath,
		const FString& ChildRegionPath,
		const FName SourceContentEntryId,
		const FLayoutId EffectiveSnapshotId,
		const FString& FailureReason)
	{
		FNegotiatedDemandResult Result;
		Result.bSucceeded = false;
		Result.FailureReason = FailureReason;
		Result.ChildRegionDebugPath = ChildRegionPath;
		Result.ChildRequest.RegionDebugPath = ChildRegionPath;
		Result.ChildRequest.SourceParentRegionDebugPath = ParentRegionPath;
		Result.ChildRequest.SourceContentEntryId = SourceContentEntryId;
		Result.ChildRequest.EffectiveSnapshotId = EffectiveSnapshotId;
		Result.ChildRequest.bSourceContentEntryOptional = true;
		return Result;
	}

	FOwnedSeamJunctionRequirement MakeOwnedSeamJunctionRequirement(
		const FString& OwnerRegionPath,
		const FString& PassiveRegionPath)
	{
		FOwnedSeamJunctionRequirement Requirement;
		Requirement.JunctionRequirementId = TEXT("Root.ChildA.ChildB.Junction");
		Requirement.OwnerRegionDebugPath = OwnerRegionPath;
		Requirement.PassiveRegionDebugPath = PassiveRegionPath;
		Requirement.AdjacencyClassId = TEXT("SiblingEdgeAttach");
		Requirement.JunctionCell = FIntVector(2, 1, 0);
		Requirement.ContinuingSeamId = TEXT("Root.ChildA.ChildB.Run");
		Requirement.BranchSeamId = TEXT("Root.ChildA.ChildB.Branch");
		Requirement.ContinuingPassiveRegionDebugPath = TEXT("ChildC");
		Requirement.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
		Requirement.ContinuingOwnerFaceDirection = ELayoutFaceDirection::PosX;
		Requirement.BranchOwnerFaceDirection = ELayoutFaceDirection::PosY;
		return Requirement;
	}

	FLayoutProofRecord MakeRewriteProofRecord(
		const FLayoutId ProofId,
		const FLayoutId TargetId)
	{
		FLayoutProofRecord Record;
		Record.ProofId = ProofId;
		Record.ProofKind = ELayoutProofKind::SnapshotCopy;
		Record.TargetId = TargetId;
		Record.SourceIds = {FLayoutId(*FString::Printf(TEXT("%s.Source"), *ProofId.ToString()))};
		Record.ProofSummary = FString::Printf(TEXT("Proof summary for %s"), *ProofId.ToString());
		return Record;
	}

	FLayoutValidationAssertionRecord MakeRewriteValidationAssertion(
		const FLayoutId AssertionId,
		const bool bPassed)
	{
		FLayoutValidationAssertionRecord Record;
		Record.AssertionId = AssertionId;
		Record.AssertionKind = ELayoutValidationAssertionKind::SnapshotContractInitialized;
		Record.bPassed = bPassed;
		Record.RelatedIds = {FLayoutId(*FString::Printf(TEXT("%s.Related"), *AssertionId.ToString()))};
		Record.FailureReason = bPassed
			? FString()
			: FString::Printf(TEXT("Assertion %s failed."), *AssertionId.ToString());
		return Record;
	}

	FLayoutDroppedOptionalChildRecord MakeRewriteDroppedOptionalChildRecord(
		const FLayoutId DropDecisionId,
		const FString& ParentRegionPath,
		const FString& ChildRegionPath)
	{
		FLayoutDroppedOptionalChildRecord Record;
		Record.DropDecisionId = DropDecisionId;
		Record.ParentRegionDebugPath = ParentRegionPath;
		Record.ChildRegionDebugPath = ChildRegionPath;
		Record.SourceContentEntryId = FName(*FString::Printf(TEXT("%s.Entry"), *ChildRegionPath));
		Record.ChildProfileSnapshotId = FLayoutId(*FString::Printf(TEXT("%s.Snapshot"), *ChildRegionPath));
		FLayoutPlannedCell& PlannedCell = Record.DroppedPlannedCells.AddDefaulted_GetRef();
		PlannedCell.Cell = FIntVector(1, 0, 0);
		PlannedCell.Intent = ELayoutCellIntent::Interior;
		Record.RelatedIds = {Record.SourceContentEntryId, Record.ChildProfileSnapshotId};
		Record.FailureReason = FString::Printf(TEXT("Optional child '%s' was dropped."), *ChildRegionPath);
		return Record;
	}

	FInstancedStruct MakeRewriteSparseRule(
		const FName RuleId,
		ULayoutRegionContentSetAsset* ContentSet,
		const int32 Count,
		const ELayoutPlacementZone PlacementZone)
	{
		FLayoutSparseExactPlacementRule Rule;
		Rule.RuleId = RuleId;
		Rule.CandidateSource = ELayoutSparseCandidateSource::ExistingResiduals;
		Rule.ContentSet = ContentSet;
		Rule.Count = Count;
		Rule.PlacementZone = PlacementZone;
		return FInstancedStruct::Make(Rule);
	}

	FLayoutModuleSolveSnapshot MakeDemandCompilationModuleSnapshot(
		const FLayoutId SnapshotId,
		const bool bSupportsVerticalAccess,
		const bool bCanOwnSeam);

	FLayoutChildRequestTemplateSnapshot MakeDemandCompilationChildTemplate(
		const FLayoutId ProfileSnapshotId,
		const FLayoutId ContentSetSnapshotId,
		const FLayoutId ModuleSnapshotId,
		const bool bSupportsVerticalAccess,
		const bool bCanOwnSeam);

	FLayoutRegionContentEntrySolveSnapshot MakeDemandCompilationChildEntry(
		const FName EntryId,
		const FLayoutChildRequestTemplateSnapshot& Template,
		const bool bOptional,
		const bool bContributesHostVerticalAccess);

	bool BuildExecutableZeroEndpointProofFixture(
		FAutomationTestBase& Test,
		FLayoutRegionSolveRequest& OutRootRequest,
		FNegotiatedDemandResult& OutDemandResult)
	{
		const FString FixturePath = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir() / TEXT("Plugins/PorismDIMsWorldGeneratorExtension/Source/PorismDIMsWorldGeneratorExtensionTests/Private/Layout/Fixtures/Data/DA_Profile_Castle_DA_ParentChildDoorSeamTest.json"));
		FString FixtureJson;
		Test.TestTrue(TEXT("Zero-endpoint proof fixture file exists"), FPaths::FileExists(FixturePath));
		Test.TestTrue(TEXT("Zero-endpoint proof fixture loads"), FFileHelper::LoadFileToString(FixtureJson, *FixturePath));
		if (FixtureJson.IsEmpty())
		{
			return false;
		}

		UObject* FixtureOuter = CreateRewriteTestOuter(TEXT("ZeroEndpointProofFixture"));
		FLayoutProfileJsonFixtureAssets FixtureAssets;
		TArray<FString> ImportIssues;
		Test.TestTrue(
			TEXT("Zero-endpoint proof fixture imports"),
			FLayoutProfileJsonFixture::ImportFromString(FixtureJson, FixtureOuter, FixtureAssets, ImportIssues));
		if (!Test.TestEqual(TEXT("Zero-endpoint proof fixture imports without issues"), ImportIssues.Num(), 0))
		{
			for (const FString& Issue : ImportIssues)
			{
				Test.AddInfo(Issue);
			}
			return false;
		}

		Test.TestNotNull(TEXT("Zero-endpoint proof fixture profile reconstructed"), FixtureAssets.Profile.Get());
		Test.TestNotNull(TEXT("Zero-endpoint proof fixture content set reconstructed"), FixtureAssets.ContentSet.Get());
		if (FixtureAssets.Profile == nullptr || FixtureAssets.ContentSet == nullptr)
		{
			return false;
		}

		FixtureAssets.Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::None;
		FixtureAssets.Profile->VerticalAccessCount = 0;
		FixtureAssets.Profile->MinVerticalAccessCount = 0;
		FixtureAssets.Profile->MaxVerticalAccessCount = 0;
		FixtureAssets.Profile->bRequireAllTraversalChannelsReachable = false;
		FixtureAssets.Profile->ZoneFeatureRequirements.Reset();

		OutRootRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
			FixtureAssets.Profile.Get(),
			9031,
			TEXT("ZeroEndpointProofFixture"));
		AddRewriteSnapshotValidationMessages(Test, TEXT("Root profile snapshot"), OutRootRequest.ProfileSnapshot.Validation);
		AddRewriteSnapshotValidationMessages(Test, TEXT("Root content-set snapshot"), OutRootRequest.ContentSetSnapshot.Validation);
		AddRewriteSnapshotValidationMessages(Test, TEXT("Root module-set snapshot"), OutRootRequest.ModuleCatalog.Validation);

		const FCompiledStructuralInputs StructuralInputs =
			BuildCompiledStructuralInputs(BuildSolveContext(OutRootRequest));
		if (!Test.TestTrue(TEXT("Zero-endpoint proof fixture compiles at least one child demand"), StructuralInputs.Demands.Num() > 0))
		{
			return false;
		}

		const FNegotiationDemandPlan& Demand = StructuralInputs.Demands[0];
		OutDemandResult = FNegotiatedDemandResult();
		OutDemandResult.bSucceeded = true;
		OutDemandResult.ChildRegionDebugPath = Demand.ChildRegionDebugPath;
		OutDemandResult.ChildRequest = Demand.ChildRequest;
		AddRewriteSnapshotValidationMessages(Test, TEXT("Zero-endpoint child profile snapshot"), OutDemandResult.ChildRequest.ProfileSnapshot.Validation);
		AddRewriteSnapshotValidationMessages(Test, TEXT("Zero-endpoint child content-set snapshot"), OutDemandResult.ChildRequest.ContentSetSnapshot.Validation);
		AddRewriteSnapshotValidationMessages(Test, TEXT("Zero-endpoint child module-set snapshot"), OutDemandResult.ChildRequest.ModuleCatalog.Validation);
		OutDemandResult.ChildVerticalAccessProvider.ChildRegionDebugPath =
			Demand.ChildRegionDebugPath;
		OutDemandResult.ResponsibilitySet.ResponsibilityContract.ParentRegionDebugPath =
			OutRootRequest.RegionDebugPath;
		OutDemandResult.ResponsibilitySet.ResponsibilityContract.ChildRegionDebugPath =
			Demand.ChildRegionDebugPath;
		return true;
	}

	bool BuildExecutableMixedProofFixture(
		FAutomationTestBase& Test,
		FLayoutRegionSolveRequest& OutRootRequest,
		TArray<FNegotiatedDemandResult>& OutDemandResults)
	{
		UObject* FixtureOuter =
			CreateRewriteTestOuter(TEXT("MixedProofFixture"));
		const FGameplayTagContainer HorizontalTraversalChannels =
			MakeTags({LayoutGameplayTags::TraversalPrimary});

		ULayoutModuleAsset* ParentModule = CreateRewriteOpenFillModule(
			FixtureOuter,
			TEXT("RewriteMixedParentShell"),
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core});
		ULayoutModuleAsset* CommittedChildModule = CreateRewriteOpenFillModule(
			FixtureOuter,
			TEXT("RewriteMixedCommittedChild"),
			{ELayoutCellIntent::Boundary},
			HorizontalTraversalChannels);
		ULayoutModuleAsset* IndependentChildModule = CreateRewriteOpenFillModule(
			FixtureOuter,
			TEXT("RewriteMixedIndependentChild"),
			{ELayoutCellIntent::Boundary});

		FLayoutRegionContentEntry CommittedChildModuleEntry;
		CommittedChildModuleEntry.EntryId = TEXT("CommittedShell");
		CommittedChildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		CommittedChildModuleEntry.ModuleSettings.Module = CommittedChildModule;
		ULayoutRegionContentSetAsset* CommittedChildContentSet = CreateRegionContentSet(
			FixtureOuter,
			TEXT("RewriteMixedCommittedChildContentSet"),
			{CommittedChildModuleEntry});
		ULayoutProfileAsset* CommittedChildProfile = CreateProfile(
			FixtureOuter,
			TEXT("RewriteMixedCommittedChildProfile"),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);
		CommittedChildProfile->ContentSet = CommittedChildContentSet;

		FLayoutRegionContentEntry IndependentChildModuleEntry;
		IndependentChildModuleEntry.EntryId = TEXT("IndependentShell");
		IndependentChildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		IndependentChildModuleEntry.ModuleSettings.Module = IndependentChildModule;
		ULayoutRegionContentSetAsset* IndependentChildContentSet = CreateRegionContentSet(
			FixtureOuter,
			TEXT("RewriteMixedIndependentChildContentSet"),
			{IndependentChildModuleEntry});
		ULayoutProfileAsset* IndependentChildProfile = CreateProfile(
			FixtureOuter,
			TEXT("RewriteMixedIndependentChildProfile"),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);
		IndependentChildProfile->ContentSet = IndependentChildContentSet;

		FLayoutRegionContentEntry ParentModuleEntry;
		ParentModuleEntry.EntryId = TEXT("ParentShell");
		ParentModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		ParentModuleEntry.ModuleSettings.Module = ParentModule;

		FLayoutRegionContentEntry CommittedChildRegionEntry;
		CommittedChildRegionEntry.EntryId = TEXT("ChildRoom");
		CommittedChildRegionEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
		CommittedChildRegionEntry.ChildRegionSettings.RegionProfile = CommittedChildProfile;
		CommittedChildRegionEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;

		FLayoutRegionContentEntry IndependentChildRegionEntry;
		IndependentChildRegionEntry.EntryId = TEXT("IndependentChild");
		IndependentChildRegionEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
		IndependentChildRegionEntry.ChildRegionSettings.RegionProfile = IndependentChildProfile;
		IndependentChildRegionEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;

		ULayoutRegionContentSetAsset* ParentContentSet = CreateRegionContentSet(
			FixtureOuter,
			TEXT("RewriteMixedParentContentSet"),
			{ParentModuleEntry, CommittedChildRegionEntry, IndependentChildRegionEntry});
		ULayoutProfileAsset* ParentProfile = CreateProfile(
			FixtureOuter,
			TEXT("RewriteMixedParentProfile"),
			FIntPoint(5, 3),
			FIntPoint(5, 3),
			1,
			0,
			false);
		ParentProfile->ContentSet = ParentContentSet;

		OutRootRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ParentContentSet,
			ParentProfile,
			9031,
			TEXT("MixedProofFixture"));

		FNegotiatedDemandResult CommittedDemandResult;
		CommittedDemandResult.bSucceeded = true;
		CommittedDemandResult.ChildRegionDebugPath =
			FString::Printf(TEXT("%s/ChildRoom"), *OutRootRequest.RegionDebugPath);
		CommittedDemandResult.ChildRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
			CommittedChildContentSet,
			CommittedChildProfile,
			9032,
			*CommittedDemandResult.ChildRegionDebugPath);
		CommittedDemandResult.ChildRequest.SourceParentRegionDebugPath =
			OutRootRequest.RegionDebugPath;
		CommittedDemandResult.ChildRequest.SourceContentEntryId = TEXT("ChildRoom");
		CommittedDemandResult.ChildRequest.RegionCellOffset = FIntVector(1, 1, 0);

		FLayoutCommittedEndpointAnchor IngressAnchor =
			MakeEndpointAnchor(
				TEXT("MixedCommittedIngress"),
				FIntVector(0, 0, 0),
				ELayoutFaceDirection::NegX);
		IngressAnchor.ConnectionTag = LayoutGameplayTags::FaceOpen;
		IngressAnchor.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
		IngressAnchor.TraversalChannels.Reset();

		CommittedDemandResult.ResponsibilitySet.ContactSet.FamilyId =
			TEXT("MixedCommittedChildFamily");
		CommittedDemandResult.ResponsibilitySet.ContactSet.EndpointCommitments = {
			IngressAnchor
		};
		CommittedDemandResult.ResponsibilitySet.ContactSet.ParentTraversalIngressAnchors = {
			MakeTraversalAnchor(FIntVector(2, 1, 0))
		};
		CommittedDemandResult.ResponsibilitySet.ResponsibilityContract =
			MakeNegotiatedContract(
				OutRootRequest.RegionDebugPath,
				CommittedDemandResult.ChildRegionDebugPath,
				IngressAnchor,
				IngressAnchor);
		CommittedDemandResult.ResponsibilitySet.ResponsibilityContract.bHasRequiredHostEgressAnchor =
			false;
		CommittedDemandResult.ResponsibilitySet.ResponsibilityContract.RequiredHostEgressAnchor =
			FLayoutCommittedEndpointAnchor();
		CommittedDemandResult.ResponsibilitySet.ResponsibilityContract.RequiredChildInternalVerticalRouteCells = {
			FIntVector(0, 0, 0)
		};
		CommittedDemandResult.ResponsibilitySet.ResponsibilityContract.CommittedParentChildInterfacesByLevel.SetNum(
			1,
			EAllowShrinking::No);
		CommittedDemandResult.ResponsibilitySet.BoundedParentProofEvidence.ChildRegionDebugPath =
			CommittedDemandResult.ChildRegionDebugPath;
		CommittedDemandResult.ResponsibilitySet.BoundedParentProofEvidence.ParentComponentId = 17;
		CommittedDemandResult.ResponsibilitySet.BoundedParentProofEvidence.ConfirmedParentRouteSeedCells = {
			FIntVector(2, 1, 0)
		};
		CommittedDemandResult.ResponsibilitySet.BoundedParentProofEvidence.ConfirmedParentContactCells = {
			FIntVector(1, 1, 0)
		};
		CommittedDemandResult.ResponsibilitySet.BoundedParentProofEvidence.ConfirmedParentTraversalIngressCells = {
			FIntVector(2, 1, 0)
		};
		CommittedDemandResult.ResponsibilitySet.BoundedParentProofEvidence.ConfirmedRequiredChildBundleSupportCells = {
			FIntVector(1, 1, 0)
		};
		CommittedDemandResult.ResponsibilitySet.BoundedParentProofEvidence.PreservedRootExternalEndpointCommitmentIds = {
			FLayoutId(TEXT("RootDoor"))
		};
		CommittedDemandResult.ChildVerticalAccessProvider.bSupportsVerticalAccess = true;
		CommittedDemandResult.ChildVerticalAccessProvider.bContributesHostVerticalAccess = true;
		CommittedDemandResult.ChildVerticalAccessProvider.VerticalAccessLocalCells = {
			FIntVector(0, 0, 0)
		};
		CommittedDemandResult.ChildVerticalAccessProvider.ChildRegionDebugPath =
			CommittedDemandResult.ChildRegionDebugPath;

		FNegotiatedDemandResult IndependentChildDemand;
		IndependentChildDemand.bSucceeded = true;
		IndependentChildDemand.ChildRegionDebugPath =
			FString::Printf(TEXT("%s/IndependentChild"), *OutRootRequest.RegionDebugPath);
		IndependentChildDemand.ChildRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
			IndependentChildContentSet,
			IndependentChildProfile,
			9033,
			*IndependentChildDemand.ChildRegionDebugPath);
		IndependentChildDemand.ChildRequest.SourceParentRegionDebugPath =
			OutRootRequest.RegionDebugPath;
		IndependentChildDemand.ChildRequest.SourceContentEntryId = TEXT("IndependentChild");
		IndependentChildDemand.ChildRequest.RegionCellOffset = FIntVector(3, 1, 0);
		IndependentChildDemand.ChildVerticalAccessProvider.ChildRegionDebugPath =
			IndependentChildDemand.ChildRegionDebugPath;
		IndependentChildDemand.ResponsibilitySet.ResponsibilityContract.ParentRegionDebugPath =
			OutRootRequest.RegionDebugPath;
		IndependentChildDemand.ResponsibilitySet.ResponsibilityContract.ChildRegionDebugPath =
			IndependentChildDemand.ChildRegionDebugPath;

		OutDemandResults = {
			CommittedDemandResult,
			IndependentChildDemand
		};
		return true;
	}

	bool BuildExecutableSupportableMixedProofFixture(
		FAutomationTestBase& Test,
		FLayoutRegionSolveRequest& OutRootRequest,
		ULayoutModuleAsset** OutCommittedChildModule,
		ULayoutModuleAsset** OutIndependentChildModule)
	{
		UObject* FixtureOuter =
			CreateRewriteTestOuter(TEXT("SupportableMixedProofFixture"));

		ULayoutModuleAsset* ParentModule = CreateRewriteOpenFillModule(
			FixtureOuter,
			TEXT("RewriteSupportableMixedParentShell"),
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core},
			MakeTags({LayoutGameplayTags::TraversalPrimary}));
		ULayoutModuleAsset* CommittedChildModule = CreateRewriteOpenFillModule(
			FixtureOuter,
			TEXT("RewriteSupportableMixedCommittedChild"),
			{ELayoutCellIntent::Boundary});
		ULayoutModuleAsset* IndependentChildModule = CreateRewriteOpenFillModule(
			FixtureOuter,
			TEXT("RewriteSupportableMixedIndependentChild"),
			{ELayoutCellIntent::Boundary});

		FLayoutRegionContentEntry CommittedChildModuleEntry;
		CommittedChildModuleEntry.EntryId = TEXT("CommittedShell");
		CommittedChildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		CommittedChildModuleEntry.ModuleSettings.Module = CommittedChildModule;
		CommittedChildModuleEntry.SeamProviderIntents = {
			MakeRewriteSeamIntent(
				TEXT("CommittedSharedWall"),
				LayoutGameplayTags::InterfacePartitionSolid,
				true,
				true)
		};
		ULayoutRegionContentSetAsset* CommittedChildContentSet = CreateRegionContentSet(
			FixtureOuter,
			TEXT("RewriteSupportableMixedCommittedChildContentSet"),
			{CommittedChildModuleEntry});
		ULayoutProfileAsset* CommittedChildProfile = CreateProfile(
			FixtureOuter,
			TEXT("RewriteSupportableMixedCommittedChildProfile"),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);
		CommittedChildProfile->ContentSet = CommittedChildContentSet;

		FLayoutRegionContentEntry IndependentChildModuleEntry;
		IndependentChildModuleEntry.EntryId = TEXT("IndependentShell");
		IndependentChildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		IndependentChildModuleEntry.ModuleSettings.Module = IndependentChildModule;
		IndependentChildModuleEntry.SeamProviderIntents = {
			MakeRewriteSeamIntent(
				TEXT("IndependentSharedWall"),
				LayoutGameplayTags::InterfacePartitionSolid,
				true,
				true)
		};
		ULayoutRegionContentSetAsset* IndependentChildContentSet = CreateRegionContentSet(
			FixtureOuter,
			TEXT("RewriteSupportableMixedIndependentChildContentSet"),
			{IndependentChildModuleEntry});
		ULayoutProfileAsset* IndependentChildProfile = CreateProfile(
			FixtureOuter,
			TEXT("RewriteSupportableMixedIndependentChildProfile"),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);
		IndependentChildProfile->ContentSet = IndependentChildContentSet;

		FLayoutRegionContentEntry ParentModuleEntry;
		ParentModuleEntry.EntryId = TEXT("ParentShell");
		ParentModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		ParentModuleEntry.ModuleSettings.Module = ParentModule;

		FLayoutRegionContentEntry CommittedChildRegionEntry;
		CommittedChildRegionEntry.EntryId = TEXT("ChildRoom");
		CommittedChildRegionEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
		CommittedChildRegionEntry.ChildRegionSettings.RegionProfile = CommittedChildProfile;
		CommittedChildRegionEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;

		FLayoutRegionContentEntry IndependentChildRegionEntry;
		IndependentChildRegionEntry.EntryId = TEXT("IndependentChild");
		IndependentChildRegionEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
		IndependentChildRegionEntry.ChildRegionSettings.RegionProfile = IndependentChildProfile;
		IndependentChildRegionEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;

		ULayoutRegionContentSetAsset* ParentContentSet = CreateRegionContentSet(
			FixtureOuter,
			TEXT("RewriteSupportableMixedParentContentSet"),
			{ParentModuleEntry, CommittedChildRegionEntry, IndependentChildRegionEntry});
		ULayoutProfileAsset* ParentProfile = CreateProfile(
			FixtureOuter,
			TEXT("RewriteSupportableMixedParentProfile"),
			FIntPoint(5, 3),
			FIntPoint(5, 3),
			1,
			0,
			false);
		ParentProfile->ContentSet = ParentContentSet;

		OutRootRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ParentContentSet,
			ParentProfile,
			9041,
			TEXT("SupportableMixedProofFixture"));
		if (OutCommittedChildModule != nullptr)
		{
			*OutCommittedChildModule = CommittedChildModule;
		}
		if (OutIndependentChildModule != nullptr)
		{
			*OutIndependentChildModule = IndependentChildModule;
		}
		return Test.TestNotNull(
			TEXT("Supportable mixed proof fixture builds a root profile"),
			ParentProfile);
	}

	bool BuildExecutableOptionalDropCoordinatorFixture(
		FAutomationTestBase& Test,
		FLayoutRegionSolveRequest& OutRootRequest,
		ULayoutModuleAsset*& OutRequiredChildModule)
	{
		UObject* FixtureOuter =
			CreateRewriteTestOuter(TEXT("OptionalDropCoordinatorFixture"));

		ULayoutModuleAsset* ParentModule = CreateRewriteOpenFillModule(
			FixtureOuter,
			TEXT("RewriteOptionalDropParentShell"),
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core});
		OutRequiredChildModule = CreateRewriteOpenFillModule(
			FixtureOuter,
			TEXT("RewriteOptionalDropRequiredChild"),
			{ELayoutCellIntent::Boundary});
		ULayoutModuleAsset* OptionalChildModule = CreateRewriteOpenFillModule(
			FixtureOuter,
			TEXT("RewriteOptionalDropOversizedChild"),
			{ELayoutCellIntent::Boundary});

		FLayoutRegionContentEntry RequiredChildModuleEntry;
		RequiredChildModuleEntry.EntryId = TEXT("RequiredShell");
		RequiredChildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		RequiredChildModuleEntry.ModuleSettings.Module = OutRequiredChildModule;
		ULayoutRegionContentSetAsset* RequiredChildContentSet = CreateRegionContentSet(
			FixtureOuter,
			TEXT("RewriteOptionalDropRequiredChildContentSet"),
			{RequiredChildModuleEntry});
		ULayoutProfileAsset* RequiredChildProfile = CreateProfile(
			FixtureOuter,
			TEXT("RewriteOptionalDropRequiredChildProfile"),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);
		RequiredChildProfile->ContentSet = RequiredChildContentSet;

		FLayoutRegionContentEntry OptionalChildModuleEntry;
		OptionalChildModuleEntry.EntryId = TEXT("OptionalShell");
		OptionalChildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		OptionalChildModuleEntry.ModuleSettings.Module = OptionalChildModule;
		ULayoutRegionContentSetAsset* OptionalChildContentSet = CreateRegionContentSet(
			FixtureOuter,
			TEXT("RewriteOptionalDropOptionalChildContentSet"),
			{OptionalChildModuleEntry});
		ULayoutProfileAsset* OptionalChildProfile = CreateProfile(
			FixtureOuter,
			TEXT("RewriteOptionalDropOptionalChildProfile"),
			FIntPoint(6, 1),
			FIntPoint(6, 1),
			1,
			0,
			false);
		OptionalChildProfile->ContentSet = OptionalChildContentSet;

		FLayoutRegionContentEntry ParentModuleEntry;
		ParentModuleEntry.EntryId = TEXT("ParentShell");
		ParentModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		ParentModuleEntry.ModuleSettings.Module = ParentModule;

		FLayoutRegionContentEntry RequiredChildRegionEntry;
		RequiredChildRegionEntry.EntryId = TEXT("RequiredChild");
		RequiredChildRegionEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
		RequiredChildRegionEntry.ChildRegionSettings.RegionProfile = RequiredChildProfile;
		RequiredChildRegionEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;

		FLayoutRegionContentEntry OptionalChildRegionEntry;
		OptionalChildRegionEntry.EntryId = TEXT("OptionalChild");
		OptionalChildRegionEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
		OptionalChildRegionEntry.ChildRegionSettings.RegionProfile = OptionalChildProfile;
		OptionalChildRegionEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;
		OptionalChildRegionEntry.ChildRegionSettings.bOptional = true;

		ULayoutRegionContentSetAsset* ParentContentSet = CreateRegionContentSet(
			FixtureOuter,
			TEXT("RewriteOptionalDropParentContentSet"),
			{ParentModuleEntry, RequiredChildRegionEntry, OptionalChildRegionEntry});
		ULayoutProfileAsset* ParentProfile = CreateProfile(
			FixtureOuter,
			TEXT("RewriteOptionalDropParentProfile"),
			FIntPoint(5, 3),
			FIntPoint(5, 3),
			1,
			0,
			false);
		ParentProfile->ContentSet = ParentContentSet;

		OutRootRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ParentContentSet,
			ParentProfile,
			9047,
			TEXT("OptionalDropCoordinatorFixture"));
		return Test.TestNotNull(
			TEXT("Optional-drop coordinator fixture builds a root profile"),
			ParentProfile);
	}

	bool BuildExecutableRecursiveChildSubtreeProofFixture(
		FAutomationTestBase& Test,
		FLayoutRegionSolveRequest& OutRootRequest,
		FNegotiatedDemandResult& OutDemandResult,
		ULayoutModuleAsset*& OutGrandchildModule)
	{
		UObject* FixtureOuter =
			CreateRewriteTestOuter(TEXT("RecursiveChildSubtreeProofFixture"));
		const FGameplayTagContainer HorizontalTraversalChannels =
			MakeTags({LayoutGameplayTags::TraversalPrimary});

		ULayoutModuleAsset* ParentModule = CreateRewriteOpenFillModule(
			FixtureOuter,
			TEXT("RewriteRecursiveParentShell"),
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core});
		ULayoutModuleAsset* ChildModule = CreateRewriteOpenFillModule(
			FixtureOuter,
			TEXT("RewriteRecursiveChildShell"),
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core},
			HorizontalTraversalChannels);
		OutGrandchildModule = CreateRewriteOpenFillModule(
			FixtureOuter,
			TEXT("RewriteRecursiveGrandchildRoom"),
			{ELayoutCellIntent::Boundary});

		FLayoutRegionContentEntry GrandchildModuleEntry;
		GrandchildModuleEntry.EntryId = TEXT("GrandchildShell");
		GrandchildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		GrandchildModuleEntry.ModuleSettings.Module = OutGrandchildModule;
		ULayoutRegionContentSetAsset* GrandchildContentSet = CreateRegionContentSet(
			FixtureOuter,
			TEXT("RewriteRecursiveGrandchildContentSet"),
			{GrandchildModuleEntry});

		ULayoutProfileAsset* GrandchildProfile = CreateProfile(
			FixtureOuter,
			TEXT("RewriteRecursiveGrandchildProfile"),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);
		GrandchildProfile->ContentSet = GrandchildContentSet;

		FLayoutRegionContentEntry ChildModuleEntry;
		ChildModuleEntry.EntryId = TEXT("ChildShell");
		ChildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		ChildModuleEntry.ModuleSettings.Module = ChildModule;

		FLayoutRegionContentEntry GrandchildRegionEntry;
		GrandchildRegionEntry.EntryId = TEXT("GrandchildRoom");
		GrandchildRegionEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
		GrandchildRegionEntry.ChildRegionSettings.RegionProfile = GrandchildProfile;
		GrandchildRegionEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;

		ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
			FixtureOuter,
			TEXT("RewriteRecursiveChildContentSet"),
			{ChildModuleEntry, GrandchildRegionEntry});

		ULayoutProfileAsset* ChildProfile = CreateProfile(
			FixtureOuter,
			TEXT("RewriteRecursiveChildProfile"),
			FIntPoint(3, 3),
			FIntPoint(3, 3),
			1,
			0,
			false);
		ChildProfile->ContentSet = ChildContentSet;

		FLayoutRegionContentEntry ParentModuleEntry;
		ParentModuleEntry.EntryId = TEXT("ParentShell");
		ParentModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		ParentModuleEntry.ModuleSettings.Module = ParentModule;

		FLayoutRegionContentEntry ChildRegionEntry;
		ChildRegionEntry.EntryId = TEXT("ChildRoom");
		ChildRegionEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
		ChildRegionEntry.ChildRegionSettings.RegionProfile = ChildProfile;
		ChildRegionEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;

		ULayoutRegionContentSetAsset* ParentContentSet = CreateRegionContentSet(
			FixtureOuter,
			TEXT("RewriteRecursiveParentContentSet"),
			{ParentModuleEntry, ChildRegionEntry});

		ULayoutProfileAsset* ParentProfile = CreateProfile(
			FixtureOuter,
			TEXT("RewriteRecursiveParentProfile"),
			FIntPoint(5, 5),
			FIntPoint(5, 5),
			1,
			0,
			false);
		ParentProfile->ContentSet = ParentContentSet;

		OutRootRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ParentContentSet,
			ParentProfile,
			9321,
			TEXT("RecursiveChildSubtreeRoot"));

		OutDemandResult = FNegotiatedDemandResult();
		OutDemandResult.bSucceeded = true;
		OutDemandResult.ChildRegionDebugPath =
			FString::Printf(TEXT("%s/ChildRoom"), *OutRootRequest.RegionDebugPath);
		OutDemandResult.ChildRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ChildContentSet,
			ChildProfile,
			9322,
			*OutDemandResult.ChildRegionDebugPath);
		OutDemandResult.ChildRequest.SourceParentRegionDebugPath =
			OutRootRequest.RegionDebugPath;
		OutDemandResult.ChildRequest.SourceContentEntryId = TEXT("ChildRoom");
		OutDemandResult.ChildRequest.RegionCellOffset = FIntVector(1, 1, 0);
		OutDemandResult.ChildVerticalAccessProvider.ChildRegionDebugPath =
			OutDemandResult.ChildRegionDebugPath;
		OutDemandResult.ResponsibilitySet.ResponsibilityContract.ParentRegionDebugPath =
			OutRootRequest.RegionDebugPath;
		OutDemandResult.ResponsibilitySet.ResponsibilityContract.ChildRegionDebugPath =
			OutDemandResult.ChildRegionDebugPath;
		return true;
	}

	bool BuildExecutableCommittedRecursiveChildSubtreeProofFixture(
		FAutomationTestBase& Test,
		FLayoutRegionSolveRequest& OutRootRequest,
		FNegotiatedDemandResult& OutDemandResult,
		ULayoutModuleAsset*& OutGrandchildModule)
	{
		if (!BuildExecutableRecursiveChildSubtreeProofFixture(
			Test,
			OutRootRequest,
			OutDemandResult,
			OutGrandchildModule))
		{
			return false;
		}

		FLayoutCommittedEndpointAnchor IngressAnchor =
			MakeEndpointAnchor(
				TEXT("RecursiveCommittedIngress"),
				FIntVector(0, 0, 0),
				ELayoutFaceDirection::NegX);
		IngressAnchor.ConnectionTag = LayoutGameplayTags::FaceOpen;
		IngressAnchor.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
		IngressAnchor.TraversalChannels.Reset();

		OutDemandResult.ResponsibilitySet.ContactSet.FamilyId =
			TEXT("RecursiveCommittedChildFamily");
		OutDemandResult.ResponsibilitySet.ContactSet.EndpointCommitments = {
			IngressAnchor
		};
		OutDemandResult.ResponsibilitySet.ContactSet.ParentTraversalIngressAnchors = {
			MakeTraversalAnchor(FIntVector(2, 1, 0))
		};
		OutDemandResult.ResponsibilitySet.ResponsibilityContract =
			MakeNegotiatedContract(
				OutRootRequest.RegionDebugPath,
				OutDemandResult.ChildRegionDebugPath,
				IngressAnchor,
				IngressAnchor);
		OutDemandResult.ResponsibilitySet.ResponsibilityContract.bHasRequiredHostEgressAnchor =
			false;
		OutDemandResult.ResponsibilitySet.ResponsibilityContract.RequiredHostEgressAnchor =
			FLayoutCommittedEndpointAnchor();
		OutDemandResult.ResponsibilitySet.ResponsibilityContract.RequiredChildInternalVerticalRouteCells = {
			FIntVector(0, 0, 0)
		};
		OutDemandResult.ResponsibilitySet.ResponsibilityContract.CommittedParentChildInterfacesByLevel.SetNum(
			1,
			EAllowShrinking::No);
		OutDemandResult.ResponsibilitySet.BoundedParentProofEvidence.ChildRegionDebugPath =
			OutDemandResult.ChildRegionDebugPath;
		OutDemandResult.ResponsibilitySet.BoundedParentProofEvidence.ParentComponentId = 23;
		OutDemandResult.ResponsibilitySet.BoundedParentProofEvidence.ConfirmedParentRouteSeedCells = {
			FIntVector(1, 1, 0)
		};
		OutDemandResult.ResponsibilitySet.BoundedParentProofEvidence.ConfirmedParentContactCells = {
			FIntVector(1, 1, 0),
			FIntVector(2, 1, 1)
		};
		OutDemandResult.ResponsibilitySet.BoundedParentProofEvidence.ConfirmedParentTraversalIngressCells = {
			FIntVector(2, 1, 0)
		};
		OutDemandResult.ResponsibilitySet.BoundedParentProofEvidence.ConfirmedRequiredChildBundleSupportCells = {
			FIntVector(2, 2, 0)
		};
		OutDemandResult.ResponsibilitySet.BoundedParentProofEvidence.PreservedRootExternalEndpointCommitmentIds = {
			FLayoutId(TEXT("RootDoor"))
		};
		return true;
	}

	void AppendProofContractJunctionCarrier(FNegotiatedProofScheduleContract& InOutProofContract)
	{
		FLayoutPartitionSeamRecord& PlannedSeam =
			InOutProofContract.SeamPlan.PlannedPartitionSeams.AddDefaulted_GetRef();
		PlannedSeam.ParentRegionDebugPath = TEXT("Root");
		PlannedSeam.OwnerRegionDebugPath = TEXT("ChildA");
		PlannedSeam.PassiveRegionDebugPath = TEXT("ChildB");
		PlannedSeam.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
		PlannedSeam.OwnerFaceDirection = ELayoutFaceDirection::PosX;
		PlannedSeam.PassiveFaceDirection = ELayoutFaceDirection::NegX;
		PlannedSeam.OwnerStartCell = FIntVector(2, 0, 0);
		PlannedSeam.OwnerEndCell = FIntVector(2, 1, 0);
		PlannedSeam.PassiveStartCell = FIntVector(2, 0, 0);
		PlannedSeam.PassiveEndCell = FIntVector(2, 1, 0);
		PlannedSeam.SegmentCount = 2;
		PlannedSeam.SeamId = TEXT("Root.ChildA.ChildB.SharedWall");
		InOutProofContract.SeamPlan.bPlannedPartitionSeamsAreAuthoritative = true;
		InOutProofContract.SeamPlan.JunctionRequirements.Add(
			MakeOwnedSeamJunctionRequirement(TEXT("ChildA"), TEXT("ChildB")));
	}

	void InitializeSuccessfulRewriteScheduleResult(
		FLayoutRegionSolveScheduleResult& InOutScheduleResult,
		const FNegotiatedProofScheduleContract& ProofContract)
	{
		InOutScheduleResult.bSucceeded = true;
		InOutScheduleResult.RecursiveVerticalAccessSummary =
			ProofContract.PlannedRecursiveVerticalAccessSummary;
	}

	void InitializeValidationReadyRewriteParentAndChildResults(
		FLayoutRegionSolveScheduleResult& InOutScheduleResult,
		const FNegotiatedProofScheduleContract& ProofContract)
	{
		FLayoutRegionSolveResult& ParentResult =
			InOutScheduleResult.RegionResults.AddDefaulted_GetRef();
		ParentResult.RegionDebugPath = ProofContract.ParentRequest.RegionDebugPath;
		ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
		ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
		ParentResult.NegotiatedChildResponsibilityContracts =
			ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
		ParentResult.CommittedEndpointAnchors =
			ProofContract.ParentRequest.CommittedEndpointAnchors;
		ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
		ParentResult.ValidationAssertions =
			ProofContract.ParentRequest.ValidationAssertions;
		ParentResult.SteppedTerrainSupportMap =
			ProofContract.ParentRequest.SteppedTerrainSupportMap;
		ParentResult.ForcedPlacementBundleInsertions =
			ProofContract.ParentRequest.ForcedPlacementBundleInsertions;
		ParentResult.RequiredRouteConstraints =
			ProofContract.ParentRequest.RequiredRouteConstraints;

		if (ProofContract.ChildRequests.Num() > 0)
		{
			const FLayoutRegionSolveRequest& ChildRequest = ProofContract.ChildRequests[0];
			FLayoutRegionSolveResult& ChildResult =
				InOutScheduleResult.RegionResults.AddDefaulted_GetRef();
			ChildResult.RegionDebugPath = ChildRequest.RegionDebugPath;
			ChildResult.RegionCellOffset = ChildRequest.RegionCellOffset;
			ChildResult.SourceContentEntryId = ChildRequest.SourceContentEntryId;
			if (ProofContract.DirectChildCommitments.Num() > 0)
			{
				ChildResult.NegotiatedChildResponsibilityContracts = {
					ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
				};
				ChildResult.CommittedEndpointAnchors =
					ProofContract.DirectChildCommitments[0].EndpointCommitments;
			}
			ChildResult.ProofRecords = ChildRequest.ProofRecords;
			ChildResult.ValidationAssertions = ChildRequest.ValidationAssertions;
			ChildResult.SteppedTerrainSupportMap =
				ChildRequest.SteppedTerrainSupportMap;
			ChildResult.ForcedPlacementBundleInsertions =
				ChildRequest.ForcedPlacementBundleInsertions;
			ChildResult.RequiredRouteConstraints =
				ChildRequest.RequiredRouteConstraints;
		}

		InOutScheduleResult.MergedSolveResult.SteppedTerrainSupportMap =
			ProofContract.ParentRequest.SteppedTerrainSupportMap;
		InOutScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions =
			ProofContract.ParentRequest.ForcedPlacementBundleInsertions;
		InOutScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints =
			ProofContract.ParentRequest.RequiredRouteConstraints;
		InOutScheduleResult.MergedSolveResult.ValidationAssertions =
			ProofContract.ParentRequest.ValidationAssertions;
	}

	void BuildRewriteParentCarrierValidationFixture(
		FRecursiveScheduleSolveContext& OutSolveContext,
		FNegotiatedProofScheduleContract& OutProofContract)
	{
		FLayoutRegionSolveRequest RootRequest;
		RootRequest.RegionDebugPath = TEXT("Root");
		RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
		PopulateRewriteSteppedTerrainSupportMap(
			RootRequest.SteppedTerrainSupportMap);
		FLayoutForcedPlacementBundleInsertion& ForcedInsertion =
			RootRequest.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
		ForcedInsertion.BundleId = TEXT("ParentCarrierBundle");
		ForcedInsertion.AnchorCell = FIntVector(2, 0, 0);
		ForcedInsertion.ProvingCell = FIntVector(1, 0, 0);
		PopulateRewriteRouteConstraint(
			RootRequest.RequiredRouteConstraints.AddDefaulted_GetRef(),
			TEXT("ParentCarrierRoute"),
			FIntVector(1, 0, 0),
			ELayoutCellIntent::Interior);

		OutSolveContext = BuildSolveContext(RootRequest);
		OutProofContract = BuildNegotiatedProofScheduleContract(
			OutSolveContext,
			{
				MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8020)
			});
	}

	bool BuildLiveSupportableMixedParentCarrierValidationFixture(
		FAutomationTestBase& Test,
		FRecursiveScheduleSolveContext& OutSolveContext,
		FNegotiatedProofScheduleContract& OutProofContract,
		FLayoutRegionSolveScheduleResult& OutScheduleResult)
	{
		FLayoutRegionSolveRequest RootRequest;
		if (!BuildExecutableSupportableMixedProofFixture(
			Test,
			RootRequest,
			nullptr,
			nullptr))
		{
			return false;
		}

		RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
		PopulateRewriteRouteConstraint(
			RootRequest.RequiredRouteConstraints.AddDefaulted_GetRef(),
			TEXT("RootValidationRoute"),
			FIntVector(1, 0, 0),
			ELayoutCellIntent::Interior);
		FLayoutSteppedTerrainSupportSample& FirstRootSupportSample =
			RootRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
		FirstRootSupportSample.LocalCell = FIntVector(0, 0, 0);
		FirstRootSupportSample.SupportSurfaceZ = 64;
		FLayoutSteppedTerrainSupportSample& SecondRootSupportSample =
			RootRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
		SecondRootSupportSample.LocalCell = FIntVector(1, 0, 0);
		SecondRootSupportSample.SupportSurfaceZ = 80;
		FLayoutSteppedTerrainAdjacencyStep& RootAdjacencyStep =
			RootRequest.SteppedTerrainSupportMap.AdjacencySteps.AddDefaulted_GetRef();
		RootAdjacencyStep.FromCell = FIntVector(0, 0, 0);
		RootAdjacencyStep.ToCell = FIntVector(1, 0, 0);
		RootAdjacencyStep.StepHeightBlocks = 16;
		RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 16;
		FLayoutForcedPlacementBundleInsertion& RootInsertion =
			RootRequest.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
		RootInsertion.BundleId = BuildRewriteRequestPlacementBundleId(RootRequest, 0);
		RootInsertion.AnchorCell = FIntVector(0, 0, 0);
		RootInsertion.ProvingCell = FIntVector(0, 0, 0);

		const FCapabilityBackedScheduleResult CoordinatorResult =
			SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
		if (!Test.TestTrue(
			TEXT("Supportable mixed validation fixture executes successfully"),
			CoordinatorResult.bSucceeded))
		{
			Test.AddError(CoordinatorResult.FailureReason);
			return false;
		}

		OutSolveContext = BuildSolveContext(RootRequest);
		OutProofContract = CoordinatorResult.ProofContract;
		OutScheduleResult = CoordinatorResult.ScheduleResult;
		return true;
	}

	FLayoutLocalCellFaceRuleSnapshot MakeLocalFaceSnapshot(const FIntVector& Cell)
	{
		FLayoutLocalCellFaceRuleSnapshot Snapshot;
		Snapshot.LocalCell = Cell;
		return Snapshot;
	}

	FLayoutModuleSolveSnapshot MakeDemandCompilationModuleSnapshot(
		const FLayoutId SnapshotId,
		const bool bSupportsVerticalAccess,
		const bool bCanOwnSeam)
	{
		FLayoutModuleSolveSnapshot Snapshot;
		Snapshot.SnapshotId = SnapshotId;
		Snapshot.DebugName = FName(*SnapshotId.ToString());
		Snapshot.BoundsCells = FIntVector(1, 1, bSupportsVerticalAccess ? 2 : 1);
		Snapshot.OccupiedLocalCells = bSupportsVerticalAccess
			? TArray<FIntVector>{FIntVector(0, 0, 0), FIntVector(0, 0, 1)}
			: TArray<FIntVector>{FIntVector(0, 0, 0)};
		for (const FIntVector& OccupiedCell : Snapshot.OccupiedLocalCells)
		{
			Snapshot.GeneratedLocalCellFaceRules.Add(MakeLocalFaceSnapshot(OccupiedCell));
		}
		Snapshot.Roles = {
			ELayoutModuleRole::Boundary,
			ELayoutModuleRole::Entry
		};
		if (bSupportsVerticalAccess)
		{
			Snapshot.Roles.Add(ELayoutModuleRole::VerticalAccess);
		}
		Snapshot.AllowedYawRotationSteps = {0};

		FLayoutDerivedEndpointOffer& EndpointOffer = Snapshot.DerivedEndpointOffers.AddDefaulted_GetRef();
		EndpointOffer.OfferId = FLayoutId(*FString::Printf(TEXT("%s.Endpoint"), *SnapshotId.ToString()));
		EndpointOffer.LocalCell = FIntVector::ZeroValue;
		EndpointOffer.FaceDirection = ELayoutFaceDirection::PosX;
		EndpointOffer.ConnectionTag = LayoutGameplayTags::FaceEntry;
		EndpointOffer.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
		EndpointOffer.TraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
		EndpointOffer.Roles = Snapshot.Roles;

		FLayoutDerivedSpanOffer& SpanOffer = Snapshot.DerivedSpanOffers.AddDefaulted_GetRef();
		SpanOffer.SpanOfferId = FLayoutId(*FString::Printf(TEXT("%s.Span"), *SnapshotId.ToString()));
		SpanOffer.LocalCell = FIntVector::ZeroValue;
		SpanOffer.FaceDirection = ELayoutFaceDirection::NegY;
		SpanOffer.ConnectionTag = LayoutGameplayTags::FaceOpen;
		SpanOffer.Roles = Snapshot.Roles;
		SpanOffer.ThicknessCells = 1;
		SpanOffer.bSealsBoundary = true;

		FLayoutClosureProviderIntent& ClosureIntent = Snapshot.ClosureProviderIntents.AddDefaulted_GetRef();
		ClosureIntent.ProviderIntentId = FName(*FString::Printf(TEXT("%s.Closure"), *SnapshotId.ToString()));
		ClosureIntent.ClosureId = TEXT("PerimeterClosure");

		FLayoutSeamProviderIntent& SeamIntent = Snapshot.SeamProviderIntents.AddDefaulted_GetRef();
		SeamIntent.SeamIntentId = FName(*FString::Printf(TEXT("%s.Seam"), *SnapshotId.ToString()));
		SeamIntent.InterfaceFamily = LayoutGameplayTags::InterfacePartitionDoor;
		SeamIntent.bCanOwnSeam = bCanOwnSeam;
		SeamIntent.bCanAcceptSeam = true;

		if (bSupportsVerticalAccess)
		{
			FLayoutDerivedVerticalAccessContract& VerticalAccessContract =
				Snapshot.DerivedVerticalAccessContracts.AddDefaulted_GetRef();
			VerticalAccessContract.ContractId =
				FLayoutId(*FString::Printf(TEXT("%s.Vertical"), *SnapshotId.ToString()));
			VerticalAccessContract.LocalCell = FIntVector(0, 0, 1);
			VerticalAccessContract.SourceTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
			VerticalAccessContract.ExitTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
		}

		return Snapshot;
	}

	FLayoutChildRequestTemplateSnapshot MakeDemandCompilationChildTemplate(
		const FLayoutId ProfileSnapshotId,
		const FLayoutId ContentSetSnapshotId,
		const FLayoutId ModuleSnapshotId,
		const bool bSupportsVerticalAccess,
		const bool bCanOwnSeam)
	{
		FLayoutChildRequestTemplateSnapshot Template;
		Template.EffectiveSnapshotId = FLayoutId(*FString::Printf(
			TEXT("Template.%s.%s"),
			*ProfileSnapshotId.ToString(),
			*ContentSetSnapshotId.ToString()));
		Template.ProfileSnapshot.SnapshotId = ProfileSnapshotId;
		Template.ProfileSnapshot.DebugName = FName(*ProfileSnapshotId.ToString());
		Template.ProfileSnapshot.EntryCountMode = ELayoutCountConstraintMode::Exact;
		Template.ProfileSnapshot.EntryCount = 2;
		Template.ProfileSnapshot.LevelCount = bSupportsVerticalAccess ? 2 : 1;
		Template.ContentSetSnapshot.SnapshotId = ContentSetSnapshotId;
		Template.ContentSetSnapshot.DebugName = FName(*ContentSetSnapshotId.ToString());
		Template.ModuleCatalog.SnapshotId = ModuleSnapshotId;
		Template.ModuleCatalog.DebugName = FName(*ModuleSnapshotId.ToString());
		Template.ModuleCatalog.Modules = {
			MakeDemandCompilationModuleSnapshot(ModuleSnapshotId, bSupportsVerticalAccess, bCanOwnSeam)
		};
		return Template;
	}

		FLayoutRegionContentEntrySolveSnapshot MakeDemandCompilationChildEntry(
			const FName EntryId,
			const FLayoutChildRequestTemplateSnapshot& Template,
			const bool bOptional,
			const bool bContributesHostVerticalAccess)
		{
			FLayoutRegionContentEntrySolveSnapshot Entry;
			Entry.EntryId = EntryId;
			Entry.ContentKind = ELayoutRegionContentKind::ChildRegion;
			Entry.ChildProfileSnapshotId = Template.ProfileSnapshot.SnapshotId;
			Entry.ChildContentSetSnapshotId = Template.ContentSetSnapshot.SnapshotId;
			Entry.CompiledChildRequestTemplate = MakeShared<FLayoutChildRequestTemplateSnapshot>(Template);
			Entry.bChildOptional = bOptional;
			Entry.bChildContributesHostVerticalAccess = bContributesHostVerticalAccess;
			Entry.ChildPlacementZone = ELayoutPlacementZone::Perimeter;
			Entry.ChildLevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;
			return Entry;
		}

		TArray<FIntVector> SelectDeterministicallyReservedCells(
			TArray<FIntVector> EligibleCells,
			const FName RuleId,
			const int32 Seed,
			const int32 ReservedCount)
		{
			EligibleCells.Sort([RuleId, Seed](const FIntVector& Left, const FIntVector& Right)
			{
				const uint32 SelectionSeed = HashCombine(GetTypeHash(RuleId), static_cast<uint32>(Seed));
				const uint32 LeftHash = HashCombineFast(SelectionSeed, GetTypeHash(Left));
				const uint32 RightHash = HashCombineFast(SelectionSeed, GetTypeHash(Right));
				if (LeftHash != RightHash)
				{
					return LeftHash < RightHash;
				}

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

			TArray<FIntVector> ReservedCells;
			for (int32 CellIndex = 0; CellIndex < ReservedCount && CellIndex < EligibleCells.Num(); ++CellIndex)
			{
				ReservedCells.Add(EligibleCells[CellIndex]);
			}
			ReservedCells.Sort([](const FIntVector& Left, const FIntVector& Right)
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
			return ReservedCells;
		}
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofContractBuildTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofContractPackagesSuccessfulDemandResults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofContractPreservesSupportableMixedRootSteppedCarriersTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofContractPreservesSupportableMixedRootSteppedCarriers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofContractPreservesBoundedParentProofEvidenceTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofContractPreservesBoundedParentProofEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofContractPreservesRecursiveVerticalAccessSummaryTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofContractPreservesRecursiveVerticalAccessSummary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofContractPreservesPublicationMetadataTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofContractPreservesPublicationMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofContractKeepsSharedHostAnchorsProofOnlyTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofContractKeepsSharedHostAnchorsProofOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofContractFailedChildCommitmentAssertionKeepsFrozenBreadcrumbsTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofContractFailedChildCommitmentAssertionKeepsFrozenBreadcrumbs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofExecutionCompatibilityRequestCarriesSeamJunctionRequirementsTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofExecutionCompatibilityRequestCarriesSeamJunctionRequirements",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofExecutionCompatibilityRequestUsesStableParentChildOrderTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofExecutionCompatibilityRequestUsesStableParentChildOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofExecutionCompatibilityRequestPreservesDistinctMultiChildProofInputsTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofExecutionCompatibilityRequestPreservesDistinctMultiChildProofInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofExecutionCompatibilityRequestPreservesDirectChildCommitmentsTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofExecutionCompatibilityRequestPreservesDirectChildCommitments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofExecutionCompatibilityRequestPreservesRequestCarriedProofInputsTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofExecutionCompatibilityRequestPreservesRequestCarriedProofInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofExecutionCompatibilityRequestPreservesSupportableMixedRootSteppedCarriersTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofExecutionCompatibilityRequestPreservesSupportableMixedRootSteppedCarriers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteExecuteNegotiatedProofScheduleUsesIndependentZeroEndpointSplitTest,
	"PorismExtension.Layout.Solver.Rewrite.ExecuteNegotiatedProofScheduleUsesIndependentZeroEndpointSplit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteFrozenHandoffCarriesParentFaceDataTest,
	"PorismExtension.Layout.Solver.Rewrite.FrozenHandoffCarriesParentFaceData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteExecuteNegotiatedProofScheduleMatchesSnapshotOnlyPseudoAsyncSupportableMixedRootSteppedCarriersTest,
	"PorismExtension.Layout.Solver.Rewrite.ExecuteNegotiatedProofScheduleMatchesSnapshotOnlyPseudoAsyncSupportableMixedRootSteppedCarriers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteExecuteNegotiatedProofScheduleMatchesSnapshotOnlyPseudoAsyncOptionalDropContractTest,
	"PorismExtension.Layout.Solver.Rewrite.ExecuteNegotiatedProofScheduleMatchesSnapshotOnlyPseudoAsyncOptionalDropContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteExecuteNegotiatedProofScheduleUsesIndependentOptionalDropContractTest,
	"PorismExtension.Layout.Solver.Rewrite.ExecuteNegotiatedProofScheduleUsesIndependentOptionalDropContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteCoordinatorBuildsSupportableMixedRequiredPlacementBackedDemandResultsTest,
	"PorismExtension.Layout.Solver.Rewrite.CoordinatorBuildsSupportableMixedRequiredPlacementBackedDemandResults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofJobPlanTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofJobPlanSortsStableMergeOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofJobPlanPreservesSupportableMixedRootSteppedCarriersTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofJobPlanPreservesSupportableMixedRootSteppedCarriers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofJobPlanPreservesDistinctMultiChildProofInputsTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofJobPlanPreservesDistinctMultiChildProofInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofJobPlanMixedContractSnapshotOnlyChildProofsTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofJobPlanMixedContractSnapshotOnlyChildProofs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePostStructuralHandoffTest,
	"PorismExtension.Layout.Solver.Rewrite.PostStructuralHandoffPreservesMergedArtifacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePostStructuralZoneFeatureAuditTest,
	"PorismExtension.Layout.Solver.Rewrite.PostStructuralHandoffRejectsMissingZoneFeatureRequirement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePostStructuralSparsePlacementTest,
	"PorismExtension.Layout.Solver.Rewrite.PostStructuralHandoffAppliesSparsePlacementFromDroppedOptionalChildResidual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedAnchorTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedAnchorContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedJunctionRequirementSetTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedJunctionRequirementSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedAuthoritativeSeamSetTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedAuthoritativeSeamSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedParentRootEndpointCommitmentSetTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedParentRootEndpointCommitmentSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedParentProofRecordSetTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedParentProofRecordSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedParentValidationAssertionSetTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedParentValidationAssertionSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedParentSteppedTerrainSupportMapTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedParentSteppedTerrainSupportMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedParentForcedPlacementBundleInsertionsTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedParentForcedPlacementBundleInsertions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedParentRequiredRouteConstraintsTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedParentRequiredRouteConstraints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedMergedRootSteppedTerrainSupportMapTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedMergedRootSteppedTerrainSupportMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedMergedRootForcedPlacementBundleInsertionsTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedMergedRootForcedPlacementBundleInsertions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedMergedRootRequiredRouteConstraintsTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedMergedRootRequiredRouteConstraints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedSupportableMixedParentSteppedCarriersTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedSupportableMixedParentSteppedCarriers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedSupportableMixedMergedRootSteppedCarriersTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedSupportableMixedMergedRootSteppedCarriers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedChildProofRecordSetTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedChildProofRecordSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedChildValidationAssertionSetTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedChildValidationAssertionSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedParentRegionCellOffsetTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedParentRegionCellOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedParentSourceContentEntryIdTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedParentSourceContentEntryId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedChildRegionCellOffsetTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedChildRegionCellOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedChildSourceContentEntryIdTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedChildSourceContentEntryId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsUnexpectedOptionalDropOnProvedParentTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsUnexpectedOptionalDropOnProvedParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsUnexpectedOptionalDropOnProvedChildTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsUnexpectedOptionalDropOnProvedChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsUnexpectedMergedOptionalDropRecordTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsUnexpectedMergedOptionalDropRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsUnexpectedDroppedChildResidualCellTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsUnexpectedDroppedChildResidualCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsUnexpectedParentResultDroppedChildResidualCellTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsUnexpectedParentResultDroppedChildResidualCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsUnexpectedChildResultOptionalDropRecordTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsUnexpectedChildResultOptionalDropRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsChangedRecursiveVerticalAccessSummaryTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsChangedRecursiveVerticalAccessSummary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsUnexpectedRecursiveVerticalAccessChildClassificationTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsUnexpectedRecursiveVerticalAccessChildClassification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsUnexpectedProvedRegionResultTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsUnexpectedProvedRegionResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofContractSkipsEmptyDirectCommitmentsTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofContractSkipsEmptyDirectCommitments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsMissingZeroEndpointChildContractTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsMissingZeroEndpointChildContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteValidationRejectsExtraChildAnchorTest,
	"PorismExtension.Layout.Solver.Rewrite.ValidationRejectsExtraChildAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteProofContractPreservesEndpointMetadataTest,
	"PorismExtension.Layout.Solver.Rewrite.ProofContractPreservesEndpointMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)



IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteDemandPlanPreservesMissingChildTemplateFailureTest,
	"PorismExtension.Layout.Solver.Rewrite.DemandPlanPreservesMissingChildTemplateFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteChildCapabilityDedupePreservesRoleDistinctVariantsTest,
	"PorismExtension.Layout.Solver.Rewrite.ChildCapabilityDedupePreservesRoleDistinctVariants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteBuildsMultiLevelConnectableAnchorPairsTest,
	"PorismExtension.Layout.Solver.Rewrite.BuildsMultiLevelConnectableAnchorPairs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteRejectsHostContributingChildWithoutConnectableUpperLowerAnchorPairTest,
	"PorismExtension.Layout.Solver.Rewrite.RejectsHostContributingChildWithoutConnectableUpperLowerAnchorPair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteExpandsRepeatedChildDemandFromHardFeatureRequirementTest,
	"PorismExtension.Layout.Solver.Rewrite.ExpandsRepeatedChildDemandFromHardFeatureRequirement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePlacementBridgePreservesProtectedStructuralCellsTest,
	"PorismExtension.Layout.Solver.Rewrite.PlacementBridgePreservesProtectedStructuralCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePlacementBridgePreservesRootExternalEndpointCommitmentsTest,
	"PorismExtension.Layout.Solver.Rewrite.PlacementBridgePreservesRootExternalEndpointCommitments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePlacementBridgeBuildPreservesCommittedTraversalAnchorsTest,
	"PorismExtension.Layout.Solver.Rewrite.PlacementBridgeBuildPreservesCommittedTraversalAnchors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePlacementBridgePreservesCommittedTraversalAnchorsOnProofContractTest,
	"PorismExtension.Layout.Solver.Rewrite.PlacementBridgePreservesCommittedTraversalAnchorsOnProofContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePlacementBridgePreservesIncomingBoundaryPointsOnProofContractTest,
	"PorismExtension.Layout.Solver.Rewrite.PlacementBridgePreservesIncomingBoundaryPointsOnProofContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePlacementBridgePreservesNegotiatedContractDiagnosticsOnProofContractTest,
	"PorismExtension.Layout.Solver.Rewrite.PlacementBridgePreservesNegotiatedContractDiagnosticsOnProofContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePlacementBridgePreservesRootProofDiagnosticsOnProofContractTest,
	"PorismExtension.Layout.Solver.Rewrite.PlacementBridgePreservesRootProofDiagnosticsOnProofContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePlacementBridgePreservesRootPublicationAndWorldBindingMetadataOnProofContractTest,
	"PorismExtension.Layout.Solver.Rewrite.PlacementBridgePreservesRootPublicationAndWorldBindingMetadataOnProofContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePlacementBridgePreservesRootContinuationSelectionAndSourceEntryOnProofContractTest,
	"PorismExtension.Layout.Solver.Rewrite.PlacementBridgePreservesRootContinuationSelectionAndSourceEntryOnProofContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePlacementBridgePreservesRebuiltParentPlanOnProofContractTest,
	"PorismExtension.Layout.Solver.Rewrite.PlacementBridgePreservesRebuiltParentPlanOnProofContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePlacementBridgePreservesRootRequestIdentityOnProofContractTest,
	"PorismExtension.Layout.Solver.Rewrite.PlacementBridgePreservesRootRequestIdentityOnProofContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePlacementBridgePreservesRootSnapshotSchemaVersionOnProofContractTest,
	"PorismExtension.Layout.Solver.Rewrite.PlacementBridgePreservesRootSnapshotSchemaVersionOnProofContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePlacementBridgePreservesRootDeferredValidationFlagsOnProofContractTest,
	"PorismExtension.Layout.Solver.Rewrite.PlacementBridgePreservesRootDeferredValidationFlagsOnProofContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePlacementBridgePreservesRootSteppedTerrainSupportMapOnProofContractTest,
	"PorismExtension.Layout.Solver.Rewrite.PlacementBridgePreservesRootSteppedTerrainSupportMapOnProofContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePlacementBridgePreservesFullRootSteppedCarrierSetOnProofContractTest,
	"PorismExtension.Layout.Solver.Rewrite.PlacementBridgePreservesFullRootSteppedCarrierSetOnProofContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteCompilesAmbiguousRepeatedChildRequirementAsChoiceSlotsTest,
	"PorismExtension.Layout.Solver.Rewrite.CompilesAmbiguousRepeatedChildRequirementAsChoiceSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRecursiveScheduleRewriteProofContractBuildTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.SourceContentEntryId = TEXT("RootCandidate");
	RootRequest.ExecutionSettings.MaxConcurrentChildProofJobs = 2;
	RootRequest.CommittedEndpointAnchors = {
		MakeEndpointAnchor(TEXT("RootDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX)
	};

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedDemandResult SuccessfulResult =
		MakeSuccessfulDemandResult(TEXT("Root"), TEXT("ChildB"), 7001);
	FNegotiatedDemandResult FailedResult;
	FailedResult.ChildRegionDebugPath = TEXT("ChildFailed");
	FailedResult.ChildRequest.RegionDebugPath = TEXT("ChildFailed");

	const FNegotiatedProofScheduleContract ProofContract =
		BuildNegotiatedProofScheduleContract(SolveContext, {FailedResult, SuccessfulResult});

	TestEqual(TEXT("Proof contract keeps one successful child proof request"), ProofContract.ChildRequests.Num(), 1);
	TestEqual(TEXT("Proof contract keeps one direct child commitment"), ProofContract.DirectChildCommitments.Num(), 1);
	TestEqual(TEXT("Proof contract keeps one negotiated responsibility contract"), ProofContract.ResponsibilityContracts.Num(), 1);
	TestEqual(TEXT("Proof contract carries one parent branch certificate"), ProofContract.ParentBranchCertificates.Num(), 1);
	TestEqual(TEXT("Proof contract keeps the request-owned child-proof concurrency"), ProofContract.MaxConcurrentChildProofJobs, 2);
	TestEqual(TEXT("Proof contract keeps root external endpoint commitments"), ProofContract.RootExternalEndpointCommitments.Num(), 1);
	TestEqual(TEXT("Proof contract copies the successful child request"), ProofContract.ChildRequests[0].RegionDebugPath, TEXT("ChildB"));
	TestEqual(TEXT("Proof contract carries the successful child certificate identity"), ProofContract.ParentBranchCertificates[0].StableChildId, TEXT("ChildB"));
	TestTrue(TEXT("Child proof request carries parent branch certificate metadata"), ProofContract.ChildRequests[0].bHasCertifiedParentBranchCertificateArtifact);
	TestEqual(TEXT("Child proof request keeps parent branch certificate id"), ProofContract.ChildRequests[0].CertifiedParentBranchCertificateArtifactId, FLayoutId(TEXT("ParentBranchCertificate.ChildB")));
	TestEqual(TEXT("Child proof request keeps parent branch id"), ProofContract.ChildRequests[0].CertifiedParentBranchId, FLayoutId(TEXT("ParentBranch.ChildB")));
	TestTrue(TEXT("Child proof request carries certificate endpoint witness"), ProofContract.ChildRequests[0].CertifiedSelectedEndpointCapabilityIds.Contains(FLayoutId(TEXT("Endpoint.ChildB.Ingress"))));
	TestTrue(TEXT("Child proof request carries certificate vertical witness"), ProofContract.ChildRequests[0].CertifiedSelectedVerticalCapabilityIds.Contains(FLayoutId(TEXT("Vertical.ChildB.AnchorPair"))));
	TestEqual(TEXT("Child proof request carries certificate closure-span witness"), ProofContract.ChildRequests[0].CertifiedSelectedClosureSpanCapabilityId, FLayoutId(TEXT("ClosureSpan.ChildB")));
	TestEqual(TEXT("Child proof request carries certificate seam capability witness"), ProofContract.ChildRequests[0].CertifiedSelectedSeamCapabilityId, FLayoutId(TEXT("SeamCapability.ChildB")));
	TestEqual(TEXT("Child proof request carries certificate seam witness id"), ProofContract.ChildRequests[0].CertifiedSelectedSeamWitnessId, FLayoutId(TEXT("SeamWitness.ChildB")));
	TestFalse(TEXT("Child proof request leaves traversal artifact absent without exact writer"), ProofContract.ChildRequests[0].bHasCertifiedSelectedTraversalCapabilityArtifact);
	TestFalse(TEXT("Child proof request leaves parent assertion mapping absent without exact writer"), ProofContract.ChildRequests[0].bHasCertifiedParentAssertionSubsetArtifact);
	TestEqual(TEXT("Child proof request keeps both negotiated endpoint commitments"), ProofContract.ChildRequests[0].CommittedEndpointAnchors.Num(), 2);
	TestEqual(TEXT("Parent proof request keeps one negotiated child contract"), ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts.Num(), 1);
	TestEqual(TEXT("Direct child commitment targets the successful child"), ProofContract.DirectChildCommitments[0].ChildRegionDebugPath, TEXT("ChildB"));
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofContractPreservesSupportableMixedRootSteppedCarriersTest::RunTest(
	const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	PopulateRewriteRouteConstraint(
		RootRequest.RequiredRouteConstraints.AddDefaulted_GetRef(),
		TEXT("RootProofContractRoute"),
		FIntVector(1, 0, 0),
		ELayoutCellIntent::Interior);
	FLayoutSteppedTerrainSupportSample& FirstRootSupportSample =
		RootRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
	FirstRootSupportSample.LocalCell = FIntVector(0, 0, 0);
	FirstRootSupportSample.SupportSurfaceZ = 64;
	FLayoutSteppedTerrainSupportSample& SecondRootSupportSample =
		RootRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
	SecondRootSupportSample.LocalCell = FIntVector(1, 0, 0);
	SecondRootSupportSample.SupportSurfaceZ = 80;
	FLayoutSteppedTerrainAdjacencyStep& RootAdjacencyStep =
		RootRequest.SteppedTerrainSupportMap.AdjacencySteps.AddDefaulted_GetRef();
	RootAdjacencyStep.FromCell = FIntVector(0, 0, 0);
	RootAdjacencyStep.ToCell = FIntVector(1, 0, 0);
	RootAdjacencyStep.StepHeightBlocks = 16;
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 16;
	FLayoutForcedPlacementBundleInsertion& RootInsertion =
		RootRequest.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
	RootInsertion.BundleId = BuildRewriteRequestPlacementBundleId(RootRequest, 0);
	RootInsertion.AnchorCell = FIntVector(0, 0, 0);
	RootInsertion.ProvingCell = FIntVector(0, 0, 0);

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Supportable mixed proof-contract fixture executes successfully"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	TestEqual(
		TEXT("Supportable mixed proof contract keeps one parent plus two child proof requests"),
		CoordinatorResult.ProofContract.ChildRequests.Num(),
		2);
	TestTrue(
		TEXT("Supportable mixed proof contract keeps the root stepped terrain support map on the parent request"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			CoordinatorResult.ProofContract.ParentRequest.SteppedTerrainSupportMap,
			RootRequest.SteppedTerrainSupportMap));
	TestTrue(
		TEXT("Supportable mixed proof contract keeps the full root forced placement bundle insertion set on the parent request"),
		AreRewriteForcedPlacementBundleInsertionsEquivalent(
			CoordinatorResult.ProofContract.ParentRequest.ForcedPlacementBundleInsertions,
			RootRequest.ForcedPlacementBundleInsertions));
	TestTrue(
		TEXT("Supportable mixed proof contract keeps the full root required route constraint set on the parent request"),
		AreRewriteRouteConstraintArraysEquivalent(
			CoordinatorResult.ProofContract.ParentRequest.RequiredRouteConstraints,
			RootRequest.RequiredRouteConstraints));
	TestEqual(
		TEXT("Supportable mixed proof contract keeps one root forced placement bundle insertion on the parent request"),
		CoordinatorResult.ProofContract.ParentRequest.ForcedPlacementBundleInsertions.Num(),
		1);
	TestEqual(
		TEXT("Supportable mixed proof contract keeps the root forced placement bundle id on the parent request"),
		CoordinatorResult.ProofContract.ParentRequest.ForcedPlacementBundleInsertions[0].BundleId,
		RootInsertion.BundleId);
	TestEqual(
		TEXT("Supportable mixed proof contract keeps one root required route constraint on the parent request"),
		CoordinatorResult.ProofContract.ParentRequest.RequiredRouteConstraints.Num(),
		1);
	TestEqual(
		TEXT("Supportable mixed proof contract keeps the root required route constraint id on the parent request"),
		CoordinatorResult.ProofContract.ParentRequest.RequiredRouteConstraints[0].ConstraintId,
		FLayoutId(TEXT("RootProofContractRoute")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofJobPlanTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.ExecutionSettings.MaxConcurrentChildProofJobs = 4;
	RootRequest.RootPlacementPolicyId = TEXT("RootPlacementPolicy");
	RootRequest.RootCandidateId = TEXT("RootCandidate");
	RootRequest.RootSolveId = TEXT("RootSolve");
	PopulateRewriteSteppedTerrainSupportMap(RootRequest.SteppedTerrainSupportMap);
	{
		FLayoutForcedPlacementBundleInsertion& ForcedInsertion =
			RootRequest.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
		ForcedInsertion.BundleId = TEXT("SteppedSupportBundle");
		ForcedInsertion.AnchorCell = FIntVector(1, 0, 0);
		ForcedInsertion.ProvingCell = FIntVector(0, 0, 0);
	}
	PopulateRewriteRouteConstraint(
		RootRequest.RequiredRouteConstraints.AddDefaulted_GetRef(),
		TEXT("RootSupportRoute"),
		FIntVector(0, 0, 0),
		ELayoutCellIntent::Interior);

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	FTerrainSteppedPreparedSolveContract RootTerrainSteppedPreparedSolveContract;
	RootTerrainSteppedPreparedSolveContract.bRootCanSatisfyTerrainSteppedTransitions = false;
	RootTerrainSteppedPreparedSolveContract.RejectionSummary.bHasUnsupportedTransition = true;
	RootTerrainSteppedPreparedSolveContract.RejectionSummary.FailureReason =
		TEXT("Root stepped-terrain solve cannot satisfy the prepared root support contract.");
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("ChildB"), 7002),
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("ChildA"), 7003)
		},
		&RootTerrainSteppedPreparedSolveContract);
	const FProofJobPlan ProofJobPlan = BuildProofJobPlan(ProofContract);

	TestFalse(
		TEXT("Proof contract preserves the prepared stepped root support status"),
		ProofContract.RootTerrainSteppedPreparedSolveContract.bRootCanSatisfyTerrainSteppedTransitions);
	TestTrue(
		TEXT("Proof contract preserves the prepared stepped rejection breadcrumb"),
		ProofContract.RootTerrainSteppedPreparedSolveContract.RejectionSummary.FailureReason.Contains(TEXT("prepared root support contract")));
	TestTrue(
		TEXT("Proof contract preserves the root stepped terrain support map on the parent request"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			ProofContract.ParentRequest.SteppedTerrainSupportMap,
			RootRequest.SteppedTerrainSupportMap));
	TestEqual(
		TEXT("Proof contract preserves one anchored forced bundle insertion on the parent request"),
		ProofContract.ParentRequest.ForcedPlacementBundleInsertions.Num(),
		1);
	TestEqual(
		TEXT("Proof contract preserves the forced bundle id on the parent request"),
		ProofContract.ParentRequest.ForcedPlacementBundleInsertions[0].BundleId,
		FLayoutId(TEXT("SteppedSupportBundle")));
	TestEqual(
		TEXT("Proof contract preserves the forced bundle anchor cell on the parent request"),
		ProofContract.ParentRequest.ForcedPlacementBundleInsertions[0].AnchorCell,
		FIntVector(1, 0, 0));
	TestEqual(
		TEXT("Proof contract preserves the forced bundle proving cell on the parent request"),
		ProofContract.ParentRequest.ForcedPlacementBundleInsertions[0].ProvingCell,
		FIntVector(0, 0, 0));
	TestEqual(
		TEXT("Proof contract preserves one root required route constraint on the parent request"),
		ProofContract.ParentRequest.RequiredRouteConstraints.Num(),
		1);
	TestEqual(
		TEXT("Proof contract preserves the root required route constraint id on the parent request"),
		ProofContract.ParentRequest.RequiredRouteConstraints[0].ConstraintId,
		FLayoutId(TEXT("RootSupportRoute")));
	TestEqual(
		TEXT("Proof contract preserves the root required route constraint cell on the parent request"),
		ProofContract.ParentRequest.RequiredRouteConstraints[0].Cell,
		FIntVector(0, 0, 0));
	TestTrue(
		TEXT("Proof contract preserves the root required route traversal channel on the parent request"),
		ProofContract.ParentRequest.RequiredRouteConstraints[0].FaceRequirements[0].TraversalChannel
			== LayoutGameplayTags::TraversalPrimary);
	TestEqual(TEXT("Proof job plan keeps the request-owned concurrency limit"), ProofJobPlan.MaxConcurrentChildProofJobs, 4);
	TestEqual(TEXT("Proof job plan keeps both child requests"), ProofJobPlan.ChildRequests.Num(), 2);
	TestEqual(TEXT("Proof job plan carries both parent branch certificates"), ProofJobPlan.ParentBranchCertificates.Num(), 2);
	TestEqual(TEXT("Proof job plan sorts first parent branch certificate by child id"), ProofJobPlan.ParentBranchCertificates[0].StableChildId, TEXT("ChildA"));
	TestEqual(TEXT("Proof job plan preserves the root solve id on publication metadata"), ProofJobPlan.PublicationMetadata.RootSolveId, FLayoutId(TEXT("RootSolve")));
	TestEqual(TEXT("Proof job plan preserves the root candidate id on publication metadata"), ProofJobPlan.PublicationMetadata.RootCandidateId, FLayoutId(TEXT("RootCandidate")));
	TestEqual(TEXT("Proof job plan preserves the root placement policy id on publication metadata"), ProofJobPlan.PublicationMetadata.RootPlacementPolicyId, FLayoutId(TEXT("RootPlacementPolicy")));
	TestFalse(
		TEXT("Proof job plan preserves the prepared stepped root support status"),
		ProofJobPlan.RootTerrainSteppedPreparedSolveContract.bRootCanSatisfyTerrainSteppedTransitions);
	TestEqual(
		TEXT("Proof job plan preserves the prepared stepped rejection breadcrumb"),
		ProofJobPlan.RootTerrainSteppedPreparedSolveContract.RejectionSummary.FailureReason,
		RootTerrainSteppedPreparedSolveContract.RejectionSummary.FailureReason);
	TestTrue(
		TEXT("Proof job plan preserves the root stepped terrain support map on the parent request"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			ProofJobPlan.ParentRequest.SteppedTerrainSupportMap,
			RootRequest.SteppedTerrainSupportMap));
	TestEqual(
		TEXT("Proof job plan preserves one anchored forced bundle insertion on the parent request"),
		ProofJobPlan.ParentRequest.ForcedPlacementBundleInsertions.Num(),
		1);
	TestEqual(
		TEXT("Proof job plan preserves the forced bundle id on the parent request"),
		ProofJobPlan.ParentRequest.ForcedPlacementBundleInsertions[0].BundleId,
		FLayoutId(TEXT("SteppedSupportBundle")));
	TestEqual(
		TEXT("Proof job plan preserves the forced bundle anchor cell on the parent request"),
		ProofJobPlan.ParentRequest.ForcedPlacementBundleInsertions[0].AnchorCell,
		FIntVector(1, 0, 0));
	TestEqual(
		TEXT("Proof job plan preserves the forced bundle proving cell on the parent request"),
		ProofJobPlan.ParentRequest.ForcedPlacementBundleInsertions[0].ProvingCell,
		FIntVector(0, 0, 0));
	TestEqual(
		TEXT("Proof job plan preserves one root required route constraint on the parent request"),
		ProofJobPlan.ParentRequest.RequiredRouteConstraints.Num(),
		1);
	TestEqual(
		TEXT("Proof job plan preserves the root required route constraint id on the parent request"),
		ProofJobPlan.ParentRequest.RequiredRouteConstraints[0].ConstraintId,
		FLayoutId(TEXT("RootSupportRoute")));
	TestEqual(
		TEXT("Proof job plan preserves the root required route constraint cell on the parent request"),
		ProofJobPlan.ParentRequest.RequiredRouteConstraints[0].Cell,
		FIntVector(0, 0, 0));
	TestTrue(
		TEXT("Proof job plan preserves the root required route traversal channel on the parent request"),
		ProofJobPlan.ParentRequest.RequiredRouteConstraints[0].FaceRequirements[0].TraversalChannel
			== LayoutGameplayTags::TraversalPrimary);
	TestEqual(TEXT("Proof job plan keeps the root solve id on the first child request"), ProofJobPlan.ChildRequests[0].RootSolveId, FLayoutId(TEXT("RootSolve")));
	TestEqual(TEXT("Proof job plan keeps the root candidate id on the first child request"), ProofJobPlan.ChildRequests[0].RootCandidateId, FLayoutId(TEXT("RootCandidate")));
	TestEqual(TEXT("Proof job plan keeps the root placement policy id on the first child request"), ProofJobPlan.ChildRequests[0].RootPlacementPolicyId, FLayoutId(TEXT("RootPlacementPolicy")));
	TestEqual(TEXT("Proof job plan keeps the root solve id on the second child request"), ProofJobPlan.ChildRequests[1].RootSolveId, FLayoutId(TEXT("RootSolve")));
	TestEqual(TEXT("Proof job plan keeps the root candidate id on the second child request"), ProofJobPlan.ChildRequests[1].RootCandidateId, FLayoutId(TEXT("RootCandidate")));
	TestEqual(TEXT("Proof job plan keeps the root placement policy id on the second child request"), ProofJobPlan.ChildRequests[1].RootPlacementPolicyId, FLayoutId(TEXT("RootPlacementPolicy")));
	TestEqual(TEXT("Proof job plan sorts child merge order deterministically"), ProofJobPlan.StableMergeOrder[0], TEXT("ChildA"));
	TestEqual(TEXT("Proof job plan sorts child merge order deterministically"), ProofJobPlan.StableMergeOrder[1], TEXT("ChildB"));
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofJobPlanPreservesSupportableMixedRootSteppedCarriersTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	RootRequest.ExecutionSettings.MaxConcurrentChildProofJobs = 3;
	RootRequest.RootPlacementPolicyId = TEXT("RootPlacementPolicy");
	RootRequest.RootCandidateId = TEXT("RootCandidate");
	RootRequest.RootSolveId = TEXT("RootSolve");
	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	PopulateRewriteRouteConstraint(
		RootRequest.RequiredRouteConstraints.AddDefaulted_GetRef(),
		TEXT("RootJobPlanRoute"),
		FIntVector(1, 0, 0),
		ELayoutCellIntent::Interior);
	FLayoutSteppedTerrainSupportSample& FirstRootSupportSample =
		RootRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
	FirstRootSupportSample.LocalCell = FIntVector(0, 0, 0);
	FirstRootSupportSample.SupportSurfaceZ = 64;
	FLayoutSteppedTerrainSupportSample& SecondRootSupportSample =
		RootRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
	SecondRootSupportSample.LocalCell = FIntVector(1, 0, 0);
	SecondRootSupportSample.SupportSurfaceZ = 80;
	FLayoutSteppedTerrainAdjacencyStep& RootAdjacencyStep =
		RootRequest.SteppedTerrainSupportMap.AdjacencySteps.AddDefaulted_GetRef();
	RootAdjacencyStep.FromCell = FIntVector(0, 0, 0);
	RootAdjacencyStep.ToCell = FIntVector(1, 0, 0);
	RootAdjacencyStep.StepHeightBlocks = 16;
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 16;
	FLayoutForcedPlacementBundleInsertion& RootInsertion =
		RootRequest.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
	RootInsertion.BundleId = BuildRewriteRequestPlacementBundleId(RootRequest, 0);
	RootInsertion.AnchorCell = FIntVector(0, 0, 0);
	RootInsertion.ProvingCell = FIntVector(0, 0, 0);

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Supportable mixed proof-job-plan fixture executes successfully"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	const FProofJobPlan ProofJobPlan =
		BuildProofJobPlan(CoordinatorResult.ProofContract);
	if (!TestEqual(
		TEXT("Supportable mixed proof job plan keeps one parent plus two child proof requests"),
		ProofJobPlan.ChildRequests.Num(),
		2))
	{
		return false;
	}

	TestEqual(
		TEXT("Supportable mixed proof job plan keeps the request-owned concurrency limit"),
		ProofJobPlan.MaxConcurrentChildProofJobs,
		3);
	TestEqual(
		TEXT("Supportable mixed proof job plan preserves the root solve id on publication metadata"),
		ProofJobPlan.PublicationMetadata.RootSolveId,
		FLayoutId(TEXT("RootSolve")));
	TestEqual(
		TEXT("Supportable mixed proof job plan preserves the root candidate id on publication metadata"),
		ProofJobPlan.PublicationMetadata.RootCandidateId,
		FLayoutId(TEXT("RootCandidate")));
	TestEqual(
		TEXT("Supportable mixed proof job plan preserves the root placement policy id on publication metadata"),
		ProofJobPlan.PublicationMetadata.RootPlacementPolicyId,
		FLayoutId(TEXT("RootPlacementPolicy")));
	TestTrue(
		TEXT("Supportable mixed proof job plan keeps the same root stepped terrain support map on the parent request"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			ProofJobPlan.ParentRequest.SteppedTerrainSupportMap,
			CoordinatorResult.ProofContract.ParentRequest.SteppedTerrainSupportMap));
	TestTrue(
		TEXT("Supportable mixed proof job plan keeps the same root forced placement bundle insertions on the parent request"),
		AreRewriteForcedPlacementBundleInsertionsEquivalent(
			ProofJobPlan.ParentRequest.ForcedPlacementBundleInsertions,
			CoordinatorResult.ProofContract.ParentRequest.ForcedPlacementBundleInsertions));
	TestTrue(
		TEXT("Supportable mixed proof job plan keeps the same root required route constraints on the parent request"),
		AreRewriteRouteConstraintArraysEquivalent(
			ProofJobPlan.ParentRequest.RequiredRouteConstraints,
			CoordinatorResult.ProofContract.ParentRequest.RequiredRouteConstraints));
	TestTrue(
		TEXT("Supportable mixed proof job plan keeps the root stepped terrain support map on the parent request"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			ProofJobPlan.ParentRequest.SteppedTerrainSupportMap,
			RootRequest.SteppedTerrainSupportMap));
	TestEqual(
		TEXT("Supportable mixed proof job plan keeps one root forced placement bundle insertion on the parent request"),
		ProofJobPlan.ParentRequest.ForcedPlacementBundleInsertions.Num(),
		1);
	TestEqual(
		TEXT("Supportable mixed proof job plan keeps the root forced placement bundle id on the parent request"),
		ProofJobPlan.ParentRequest.ForcedPlacementBundleInsertions[0].BundleId,
		RootInsertion.BundleId);
	TestEqual(
		TEXT("Supportable mixed proof job plan keeps one root required route constraint on the parent request"),
		ProofJobPlan.ParentRequest.RequiredRouteConstraints.Num(),
		1);
	TestEqual(
		TEXT("Supportable mixed proof job plan keeps the root required route constraint id on the parent request"),
		ProofJobPlan.ParentRequest.RequiredRouteConstraints[0].ConstraintId,
		FLayoutId(TEXT("RootJobPlanRoute")));
	TestEqual(
		TEXT("Supportable mixed proof job plan sorts child merge order deterministically"),
		ProofJobPlan.StableMergeOrder[0],
		TEXT("SupportableMixedProofFixture/ChildRoom"));
	TestEqual(
		TEXT("Supportable mixed proof job plan sorts child merge order deterministically"),
		ProofJobPlan.StableMergeOrder[1],
		TEXT("SupportableMixedProofFixture/IndependentChild"));
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofJobPlanPreservesDistinctMultiChildProofInputsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.ExecutionSettings.MaxConcurrentChildProofJobs = 4;
	RootRequest.RootPlacementPolicyId = TEXT("RootPlacementPolicy");
	RootRequest.RootCandidateId = TEXT("RootCandidate");
	RootRequest.RootSolveId = TEXT("RootSolve");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("ChildB"), 7271),
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("ChildA"), 7272)
		});

	if (!TestEqual(TEXT("Distinct multi-child proof job-plan fixture keeps two child proof requests"), ProofContract.ChildRequests.Num(), 2))
	{
		return false;
	}

	FLayoutRegionSolveRequest* ChildARequest = ProofContract.ChildRequests.FindByPredicate(
		[](const FLayoutRegionSolveRequest& Request)
		{
			return Request.RegionDebugPath == TEXT("ChildA");
		});
	FLayoutRegionSolveRequest* ChildBRequest = ProofContract.ChildRequests.FindByPredicate(
		[](const FLayoutRegionSolveRequest& Request)
		{
			return Request.RegionDebugPath == TEXT("ChildB");
		});
	if (!TestNotNull(TEXT("Distinct multi-child proof job-plan fixture keeps ChildA request"), ChildARequest)
		|| !TestNotNull(TEXT("Distinct multi-child proof job-plan fixture keeps ChildB request"), ChildBRequest))
	{
		return false;
	}

	ChildARequest->SourceParentRegionDebugPath = TEXT("Root");
	ChildARequest->SourceContentEntryId = TEXT("ChildAEntry");
	ChildARequest->FootprintSize = FIntPoint(2, 1);
	ChildARequest->PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Boundary)
	};
	ChildARequest->ProtectedStructuralCells = {FIntVector(4, 0, 0)};
	ChildARequest->CommittedTraversalAnchors = {MakeTraversalAnchor(FIntVector(5, 0, 0))};
	PopulateRewriteSteppedTerrainSupportMap(ChildARequest->SteppedTerrainSupportMap);
	FLayoutForcedPlacementBundleInsertion& ChildAForcedInsertion =
		ChildARequest->ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
	ChildAForcedInsertion.BundleId = TEXT("ChildAForcedBundle");
	ChildAForcedInsertion.AnchorCell = FIntVector(0, 0, 0);
	ChildAForcedInsertion.ProvingCell = FIntVector(1, 0, 0);
	PopulateRewriteRouteConstraint(
		ChildARequest->RequiredRouteConstraints.AddDefaulted_GetRef(),
		TEXT("ChildAJobPlanRoute"),
		FIntVector(1, 0, 0),
		ELayoutCellIntent::Interior);
	ChildARequest->bUseSuppliedChildCapabilityEnvelope = true;
	ChildARequest->SuppliedChildCapabilityEnvelope.RegionDebugPath = TEXT("ChildA");
	ChildARequest->SuppliedChildCapabilityEnvelope.SnapshotId = TEXT("ChildAEnvelopeSnapshot");
	ChildARequest->SuppliedChildCapabilityEnvelope.ProofRecords = {
		MakeRewriteProofRecord(TEXT("ChildAEnvelopeProof"), TEXT("ChildAEnvelopeTarget"))
	};
	ChildARequest->SuppliedChildCapabilityEnvelope.ValidationAssertions = {
		MakeRewriteValidationAssertion(TEXT("ChildAEnvelopeAssertion"), true)
	};
	ChildARequest->ProofRecords = {
		MakeRewriteProofRecord(TEXT("ChildAProof"), TEXT("ChildATarget"))
	};
	ChildARequest->ValidationAssertions = {
		MakeRewriteValidationAssertion(TEXT("ChildAAssertion"), true)
	};

	ChildBRequest->SourceParentRegionDebugPath = TEXT("Root");
	ChildBRequest->SourceContentEntryId = TEXT("ChildBEntry");
	ChildBRequest->FootprintSize = FIntPoint(1, 2);
	ChildBRequest->PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Boundary),
		MakePlannedCell(FIntVector(0, 1, 0), ELayoutCellIntent::Interior)
	};
	ChildBRequest->ProtectedStructuralCells = {FIntVector(6, 0, 0)};
	ChildBRequest->CommittedTraversalAnchors = {MakeTraversalAnchor(FIntVector(7, 0, 0))};
	PopulateRewriteSteppedTerrainSupportMap(ChildBRequest->SteppedTerrainSupportMap);
	ChildBRequest->SteppedTerrainSupportMap.SupportSamples[0].SupportSurfaceZ = 48;
	ChildBRequest->SteppedTerrainSupportMap.SupportSamples[1].SupportSurfaceZ = 64;
	ChildBRequest->SteppedTerrainSupportMap.AdjacencySteps[0].StepHeightBlocks = 8;
	ChildBRequest->SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 2;
	ChildBRequest->ProofRecords = {
		MakeRewriteProofRecord(TEXT("ChildBProof"), TEXT("ChildBTarget"))
	};
	ChildBRequest->ValidationAssertions = {
		MakeRewriteValidationAssertion(TEXT("ChildBAssertion"), false)
	};

	const FProofJobPlan ProofJobPlan = BuildProofJobPlan(ProofContract);
	if (!TestEqual(TEXT("Distinct multi-child proof job plan keeps two child proof requests"), ProofJobPlan.ChildRequests.Num(), 2))
	{
		return false;
	}

	const FLayoutRegionSolveRequest& ChildAJobPlanRequest = ProofJobPlan.ChildRequests[0];
	const FLayoutRegionSolveRequest& ChildBJobPlanRequest = ProofJobPlan.ChildRequests[1];

	TestEqual(TEXT("Distinct multi-child proof job plan keeps ChildA first by stable merge order"), ChildAJobPlanRequest.RegionDebugPath, FString(TEXT("ChildA")));
	TestEqual(TEXT("Distinct multi-child proof job plan keeps ChildB second by stable merge order"), ChildBJobPlanRequest.RegionDebugPath, FString(TEXT("ChildB")));

	TestEqual(TEXT("Distinct multi-child proof job plan keeps ChildA source content entry id"), ChildAJobPlanRequest.SourceContentEntryId, FName(TEXT("ChildAEntry")));
	TestEqual(TEXT("Distinct multi-child proof job plan keeps ChildA protected structural cell"), ChildAJobPlanRequest.ProtectedStructuralCells[0], FIntVector(4, 0, 0));
	TestEqual(TEXT("Distinct multi-child proof job plan keeps ChildA committed traversal anchor"), ChildAJobPlanRequest.CommittedTraversalAnchors[0].Cell, FIntVector(5, 0, 0));
	TestTrue(
		TEXT("Distinct multi-child proof job plan keeps ChildA stepped terrain support map"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			ChildAJobPlanRequest.SteppedTerrainSupportMap,
			ChildARequest->SteppedTerrainSupportMap));
	TestEqual(TEXT("Distinct multi-child proof job plan keeps one ChildA forced placement bundle insertion"), ChildAJobPlanRequest.ForcedPlacementBundleInsertions.Num(), 1);
	TestEqual(TEXT("Distinct multi-child proof job plan keeps the ChildA forced placement bundle id"), ChildAJobPlanRequest.ForcedPlacementBundleInsertions[0].BundleId, FLayoutId(TEXT("ChildAForcedBundle")));
	TestEqual(TEXT("Distinct multi-child proof job plan keeps one ChildA required route constraint"), ChildAJobPlanRequest.RequiredRouteConstraints.Num(), 1);
	TestEqual(TEXT("Distinct multi-child proof job plan keeps the ChildA required route constraint id"), ChildAJobPlanRequest.RequiredRouteConstraints[0].ConstraintId, FLayoutId(TEXT("ChildAJobPlanRoute")));
	TestTrue(TEXT("Distinct multi-child proof job plan keeps the ChildA supplied capability-envelope flag"), ChildAJobPlanRequest.bUseSuppliedChildCapabilityEnvelope);
	TestEqual(TEXT("Distinct multi-child proof job plan keeps the ChildA supplied capability-envelope snapshot id"), ChildAJobPlanRequest.SuppliedChildCapabilityEnvelope.SnapshotId, FLayoutId(TEXT("ChildAEnvelopeSnapshot")));
	TestEqual(TEXT("Distinct multi-child proof job plan keeps one ChildA proof record"), ChildAJobPlanRequest.ProofRecords.Num(), 1);
	TestEqual(TEXT("Distinct multi-child proof job plan keeps the ChildA proof record id"), ChildAJobPlanRequest.ProofRecords[0].ProofId, FLayoutId(TEXT("ChildAProof")));
	TestEqual(TEXT("Distinct multi-child proof job plan keeps one ChildA validation assertion"), ChildAJobPlanRequest.ValidationAssertions.Num(), 1);
	TestEqual(TEXT("Distinct multi-child proof job plan keeps the ChildA validation assertion id"), ChildAJobPlanRequest.ValidationAssertions[0].AssertionId, FLayoutId(TEXT("ChildAAssertion")));

	TestEqual(TEXT("Distinct multi-child proof job plan keeps ChildB source content entry id"), ChildBJobPlanRequest.SourceContentEntryId, FName(TEXT("ChildBEntry")));
	TestEqual(TEXT("Distinct multi-child proof job plan keeps ChildB protected structural cell"), ChildBJobPlanRequest.ProtectedStructuralCells[0], FIntVector(6, 0, 0));
	TestEqual(TEXT("Distinct multi-child proof job plan keeps ChildB committed traversal anchor"), ChildBJobPlanRequest.CommittedTraversalAnchors[0].Cell, FIntVector(7, 0, 0));
	TestTrue(
		TEXT("Distinct multi-child proof job plan keeps ChildB stepped terrain support map"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			ChildBJobPlanRequest.SteppedTerrainSupportMap,
			ChildBRequest->SteppedTerrainSupportMap));
	TestEqual(TEXT("Distinct multi-child proof job plan keeps no ChildB forced placement bundle insertions"), ChildBJobPlanRequest.ForcedPlacementBundleInsertions.Num(), 0);
	TestEqual(TEXT("Distinct multi-child proof job plan keeps no ChildB required route constraints"), ChildBJobPlanRequest.RequiredRouteConstraints.Num(), 0);
	TestFalse(TEXT("Distinct multi-child proof job plan keeps no ChildB supplied capability-envelope flag"), ChildBJobPlanRequest.bUseSuppliedChildCapabilityEnvelope);
	TestEqual(TEXT("Distinct multi-child proof job plan keeps one ChildB proof record"), ChildBJobPlanRequest.ProofRecords.Num(), 1);
	TestEqual(TEXT("Distinct multi-child proof job plan keeps the ChildB proof record id"), ChildBJobPlanRequest.ProofRecords[0].ProofId, FLayoutId(TEXT("ChildBProof")));
	TestEqual(TEXT("Distinct multi-child proof job plan keeps one ChildB validation assertion"), ChildBJobPlanRequest.ValidationAssertions.Num(), 1);
	TestEqual(TEXT("Distinct multi-child proof job plan keeps the ChildB validation assertion id"), ChildBJobPlanRequest.ValidationAssertions[0].AssertionId, FLayoutId(TEXT("ChildBAssertion")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofJobPlanMixedContractSnapshotOnlyChildProofsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	TArray<FNegotiatedDemandResult> DemandResults;
	if (!BuildExecutableMixedProofFixture(*this, RootRequest, DemandResults))
	{
		return false;
	}
	PopulateRewriteSteppedTerrainSupportMap(DemandResults[0].ChildRequest.SteppedTerrainSupportMap);
	FLayoutForcedPlacementBundleInsertion& CommittedChildForcedInsertion =
		DemandResults[0].ChildRequest.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
	CommittedChildForcedInsertion.BundleId = BuildRewriteRequestPlacementBundleId(
		DemandResults[0].ChildRequest,
		0);
	CommittedChildForcedInsertion.AnchorCell = FIntVector(0, 0, 0);
	CommittedChildForcedInsertion.ProvingCell = FIntVector(0, 0, 0);
	PopulateRewriteRouteConstraint(
		DemandResults[0].ChildRequest.RequiredRouteConstraints.AddDefaulted_GetRef(),
		TEXT("MixedCommittedContactChildRoute"),
		FIntVector(0, 0, 0),
		ELayoutCellIntent::Boundary);
	PopulateRewriteSteppedTerrainSupportMap(DemandResults[1].ChildRequest.SteppedTerrainSupportMap);

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract =
		BuildNegotiatedProofScheduleContract(SolveContext, DemandResults);
	const FProofJobPlan ProofJobPlan = BuildProofJobPlan(ProofContract);
	if (!TestEqual(TEXT("Mixed proof job-plan fixture keeps two child proof requests"), ProofJobPlan.ChildRequests.Num(), 2))
	{
		return false;
	}

	const FLayoutRegionSolveRequest* SourceCommittedContactChildRequest =
		ProofJobPlan.ChildRequests.FindByPredicate(
			[&DemandResults](const FLayoutRegionSolveRequest& Request)
			{
				return Request.RegionDebugPath == DemandResults[0].ChildRegionDebugPath;
			});
	const FLayoutRegionSolveRequest* SourceZeroEndpointChildRequest =
		ProofJobPlan.ChildRequests.FindByPredicate(
			[&DemandResults](const FLayoutRegionSolveRequest& Request)
			{
				return Request.RegionDebugPath == DemandResults[1].ChildRegionDebugPath;
			});
	if (!TestNotNull(TEXT("Mixed proof job-plan fixture keeps the committed-contact child request"), SourceCommittedContactChildRequest)
		|| !TestNotNull(TEXT("Mixed proof job-plan fixture keeps the zero-endpoint child request"), SourceZeroEndpointChildRequest))
	{
		return false;
	}

	FLayoutRegionSolveResult SourceCommittedContactChildProofResult;
	FString SourceCommittedContactChildProofFailureReason;
	if (!TestTrue(
		TEXT("Mixed proof job-plan fixture source-backed committed-contact child proof succeeds"),
		LayoutProfileSolverInternal::BuildIndependentChildProofResult(
			*SourceCommittedContactChildRequest,
			SourceCommittedContactChildProofResult,
			SourceCommittedContactChildProofFailureReason)))
	{
		AddError(SourceCommittedContactChildProofFailureReason);
		return false;
	}

	FLayoutRegionSolveResult SourceZeroEndpointChildProofResult;
	FString SourceZeroEndpointChildProofFailureReason;
	if (!TestTrue(
		TEXT("Mixed proof job-plan fixture source-backed zero-endpoint child proof succeeds"),
		LayoutProfileSolverInternal::BuildIndependentChildProofResult(
			*SourceZeroEndpointChildRequest,
			SourceZeroEndpointChildProofResult,
			SourceZeroEndpointChildProofFailureReason)))
	{
		AddError(SourceZeroEndpointChildProofFailureReason);
		return false;
	}

	FLayoutRegionSolveRequest SnapshotOnlyCommittedContactChildRequest =
		*SourceCommittedContactChildRequest;
	StripRewriteLiveSourcesFromRequest(SnapshotOnlyCommittedContactChildRequest);
	FLayoutRegionSolveResult SnapshotOnlyCommittedContactChildProofResult;
	FString SnapshotOnlyCommittedContactChildProofFailureReason;
	if (!TestTrue(
		TEXT("Mixed proof job-plan fixture snapshot-only committed-contact child proof succeeds"),
		LayoutProfileSolverInternal::BuildIndependentChildProofResult(
			SnapshotOnlyCommittedContactChildRequest,
			SnapshotOnlyCommittedContactChildProofResult,
			SnapshotOnlyCommittedContactChildProofFailureReason)))
	{
		AddError(SnapshotOnlyCommittedContactChildProofFailureReason);
		return false;
	}

	FLayoutRegionSolveRequest SnapshotOnlyZeroEndpointChildRequest =
		*SourceZeroEndpointChildRequest;
	StripRewriteLiveSourcesFromRequest(SnapshotOnlyZeroEndpointChildRequest);
	FLayoutRegionSolveResult SnapshotOnlyZeroEndpointChildProofResult;
	FString SnapshotOnlyZeroEndpointChildProofFailureReason;
	if (!TestTrue(
		TEXT("Mixed proof job-plan fixture snapshot-only zero-endpoint child proof succeeds"),
		LayoutProfileSolverInternal::BuildIndependentChildProofResult(
			SnapshotOnlyZeroEndpointChildRequest,
			SnapshotOnlyZeroEndpointChildProofResult,
			SnapshotOnlyZeroEndpointChildProofFailureReason)))
	{
		AddError(SnapshotOnlyZeroEndpointChildProofFailureReason);
		return false;
	}

	TestTrue(
		TEXT("Mixed proof job-plan fixture snapshot-only committed-contact child proof keeps the same placements"),
		AreRewritePlacedModuleArraysEquivalent(
			SnapshotOnlyCommittedContactChildProofResult.SolveResult.Placements,
			SourceCommittedContactChildProofResult.SolveResult.Placements));
	TestTrue(
		TEXT("Mixed proof job-plan fixture snapshot-only committed-contact child proof keeps the same stepped terrain support map"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			SnapshotOnlyCommittedContactChildProofResult.SteppedTerrainSupportMap,
			SourceCommittedContactChildProofResult.SteppedTerrainSupportMap));
	TestEqual(
		TEXT("Mixed proof job-plan fixture snapshot-only committed-contact child proof keeps the same forced placement bundle insertion count"),
		SnapshotOnlyCommittedContactChildProofResult.ForcedPlacementBundleInsertions.Num(),
		SourceCommittedContactChildProofResult.ForcedPlacementBundleInsertions.Num());
	TestEqual(
		TEXT("Mixed proof job-plan fixture snapshot-only committed-contact child proof keeps the same forced placement bundle id"),
		SnapshotOnlyCommittedContactChildProofResult.ForcedPlacementBundleInsertions[0].BundleId,
		SourceCommittedContactChildProofResult.ForcedPlacementBundleInsertions[0].BundleId);
	TestEqual(
		TEXT("Mixed proof job-plan fixture snapshot-only committed-contact child proof keeps the same required route constraint count"),
		SnapshotOnlyCommittedContactChildProofResult.RequiredRouteConstraints.Num(),
		SourceCommittedContactChildProofResult.RequiredRouteConstraints.Num());
	TestEqual(
		TEXT("Mixed proof job-plan fixture snapshot-only committed-contact child proof keeps the same required route constraint id"),
		SnapshotOnlyCommittedContactChildProofResult.RequiredRouteConstraints[0].ConstraintId,
		SourceCommittedContactChildProofResult.RequiredRouteConstraints[0].ConstraintId);
	TestTrue(
		TEXT("Mixed proof job-plan fixture snapshot-only committed-contact child proof keeps the committed child stepped terrain support map"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			SnapshotOnlyCommittedContactChildProofResult.SteppedTerrainSupportMap,
			DemandResults[0].ChildRequest.SteppedTerrainSupportMap));
	TestEqual(
		TEXT("Mixed proof job-plan fixture snapshot-only committed-contact child proof keeps one committed child forced placement bundle insertion"),
		SnapshotOnlyCommittedContactChildProofResult.ForcedPlacementBundleInsertions.Num(),
		1);
	TestEqual(
		TEXT("Mixed proof job-plan fixture snapshot-only committed-contact child proof keeps the committed child forced placement bundle id"),
		SnapshotOnlyCommittedContactChildProofResult.ForcedPlacementBundleInsertions[0].BundleId,
		CommittedChildForcedInsertion.BundleId);
	TestEqual(
		TEXT("Mixed proof job-plan fixture snapshot-only committed-contact child proof keeps one committed child required route constraint"),
		SnapshotOnlyCommittedContactChildProofResult.RequiredRouteConstraints.Num(),
		1);
	TestEqual(
		TEXT("Mixed proof job-plan fixture snapshot-only committed-contact child proof keeps the committed child required route constraint id"),
		SnapshotOnlyCommittedContactChildProofResult.RequiredRouteConstraints[0].ConstraintId,
		FLayoutId(TEXT("MixedCommittedContactChildRoute")));

	TestTrue(
		TEXT("Mixed proof job-plan fixture snapshot-only zero-endpoint child proof keeps the same placements"),
		AreRewritePlacedModuleArraysEquivalent(
			SnapshotOnlyZeroEndpointChildProofResult.SolveResult.Placements,
			SourceZeroEndpointChildProofResult.SolveResult.Placements));
	TestTrue(
		TEXT("Mixed proof job-plan fixture snapshot-only zero-endpoint child proof keeps the same stepped terrain support map"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			SnapshotOnlyZeroEndpointChildProofResult.SteppedTerrainSupportMap,
			SourceZeroEndpointChildProofResult.SteppedTerrainSupportMap));
	TestTrue(
		TEXT("Mixed proof job-plan fixture snapshot-only zero-endpoint child proof keeps the zero-endpoint child stepped terrain support map"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			SnapshotOnlyZeroEndpointChildProofResult.SteppedTerrainSupportMap,
			DemandResults[1].ChildRequest.SteppedTerrainSupportMap));
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofContractPreservesBoundedParentProofEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("ChildB"), 7101),
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("ChildA"), 7102)
		});

	if (!TestEqual(TEXT("Proof contract keeps one bounded parent-proof evidence record per successful negotiated child demand"), ProofContract.BoundedParentProofEvidenceRecords.Num(), 2))
	{
		return false;
	}
	if (!TestEqual(TEXT("First bounded parent-proof evidence record keeps one reachable root external endpoint commitment id"), ProofContract.BoundedParentProofEvidenceRecords[0].PreservedRootExternalEndpointCommitmentIds.Num(), 1)
		|| !TestEqual(TEXT("First bounded parent-proof evidence record keeps one required child-bundle support cell"), ProofContract.BoundedParentProofEvidenceRecords[0].ConfirmedRequiredChildBundleSupportCells.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Bounded parent-proof evidence records sort deterministically by child region path"), ProofContract.BoundedParentProofEvidenceRecords[0].ChildRegionDebugPath, FString(TEXT("ChildA")));
	TestEqual(TEXT("Bounded parent-proof evidence keeps the preserved root external endpoint commitment id"), ProofContract.BoundedParentProofEvidenceRecords[0].PreservedRootExternalEndpointCommitmentIds[0], FLayoutId(TEXT("RootDoor")));
	TestEqual(TEXT("Bounded parent-proof evidence keeps the required child-bundle support cell"), ProofContract.BoundedParentProofEvidenceRecords[0].ConfirmedRequiredChildBundleSupportCells[0], FIntVector(4, 0, 0));
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofContractPreservesRecursiveVerticalAccessSummaryTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	FNegotiatedDemandResult CountedResult =
		MakeSuccessfulDemandResult(TEXT("Root"), TEXT("CountedChild"), 7111);

	FNegotiatedDemandResult ExtraResult =
		MakeSuccessfulDemandResult(TEXT("Root"), TEXT("ExtraChild"), 7112);
	ExtraResult.ResponsibilitySet.ResponsibilityContract.CountedChildProviderRegionDebugPaths.Reset();

	FNegotiatedDemandResult LocalOnlyResult =
		MakeSuccessfulDemandResult(TEXT("Root"), TEXT("LocalOnlyChild"), 7113);
	LocalOnlyResult.ResponsibilitySet.ResponsibilityContract.CountedChildProviderRegionDebugPaths.Reset();
	LocalOnlyResult.ChildVerticalAccessProvider.bContributesHostVerticalAccess = false;

	FNegotiatedDemandResult UnusableResult =
		MakeSuccessfulDemandResult(TEXT("Root"), TEXT("UnusableChild"), 7114);
	UnusableResult.ResponsibilitySet.ResponsibilityContract.CountedChildProviderRegionDebugPaths.Reset();
	UnusableResult.ChildVerticalAccessProvider.bSupportsVerticalAccess = false;
	UnusableResult.ChildVerticalAccessProvider.VerticalAccessLocalCells.Reset();

	const FNegotiatedProofScheduleContract ProofContract =
		BuildNegotiatedProofScheduleContract(
			SolveContext,
			{CountedResult, ExtraResult, LocalOnlyResult, UnusableResult});

	const FLayoutRecursiveVerticalAccessSummary& Summary =
		ProofContract.PlannedRecursiveVerticalAccessSummary;

	TestEqual(TEXT("Proof contract preserves the required host-provider count"), Summary.RequiredHostProviderCount, 1);
	TestEqual(TEXT("Proof contract preserves the counted parent-provider count"), Summary.CountedParentProviderCount, 0);
	TestEqual(TEXT("Proof contract preserves one counted child provider"), Summary.CountedChildRegionDebugPaths.Num(), 1);
	if (Summary.CountedChildRegionDebugPaths.Num() == 1)
	{
		TestEqual(TEXT("Proof contract preserves the counted child provider path"), Summary.CountedChildRegionDebugPaths[0], FString(TEXT("CountedChild")));
	}
	TestEqual(TEXT("Proof contract no longer publishes local-only child classifications in the planned recursive vertical-access summary"), Summary.LocalOnlyChildRegionDebugPaths.Num(), 0);
	TestEqual(TEXT("Proof contract no longer publishes unusable contributing child classifications in the planned recursive vertical-access summary"), Summary.UnusableContributingChildRegionDebugPaths.Num(), 0);
	TestEqual(TEXT("Proof contract no longer publishes extra contributing child classifications in the planned recursive vertical-access summary"), Summary.ExtraContributingChildRegionDebugPaths.Num(), 0);
	TestTrue(TEXT("Proof contract keeps the planned recursive vertical-access summary failure reason empty on successful negotiation"), Summary.FailureReason.IsEmpty());
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofContractPreservesPublicationMetadataTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.RootSolveId = TEXT("RootSolve");
	RootRequest.RootCandidateId = TEXT("RootCandidate");
	RootRequest.RootPlacementPolicyId = TEXT("RootPlacementPolicy");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("ChildA"), 7141)
		});

	if (!TestEqual(TEXT("Proof contract preserves one successful child request"), ProofContract.ChildRequests.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Proof contract preserves the root solve id on publication metadata"), ProofContract.PublicationMetadata.RootSolveId, FLayoutId(TEXT("RootSolve")));
	TestEqual(TEXT("Proof contract preserves the root candidate id on publication metadata"), ProofContract.PublicationMetadata.RootCandidateId, FLayoutId(TEXT("RootCandidate")));
	TestEqual(TEXT("Proof contract preserves the root placement policy id on publication metadata"), ProofContract.PublicationMetadata.RootPlacementPolicyId, FLayoutId(TEXT("RootPlacementPolicy")));
	TestEqual(TEXT("Proof contract keeps the root solve id on the child request"), ProofContract.ChildRequests[0].RootSolveId, FLayoutId(TEXT("RootSolve")));
	TestEqual(TEXT("Proof contract keeps the root candidate id on the child request"), ProofContract.ChildRequests[0].RootCandidateId, FLayoutId(TEXT("RootCandidate")));
	TestEqual(TEXT("Proof contract keeps the root placement policy id on the child request"), ProofContract.ChildRequests[0].RootPlacementPolicyId, FLayoutId(TEXT("RootPlacementPolicy")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofContractFailedChildCommitmentAssertionKeepsFrozenBreadcrumbsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.ProfileSnapshot.SnapshotId = TEXT("RootProfile");
	RootRequest.ProfileSnapshot.DebugName = TEXT("RootProfileDebug");
	RootRequest.ContentSetSnapshot.SnapshotId = TEXT("RootContent");
	RootRequest.ContentSetSnapshot.DebugName = TEXT("RootContentDebug");
	RootRequest.ModuleCatalog.SnapshotId = TEXT("RootModule");
	RootRequest.ModuleCatalog.DebugName = TEXT("RootModuleDebug");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 7142)
		});

	InvalidateRewriteNegotiatedChildCommitmentCarrier(ProofContract);
	AppendNegotiatedContractProofDiagnosticsToRequest(ProofContract.ParentRequest);

	const FLayoutId ExpectedAssertionId(TEXT("Root.NegotiatedChildResponsibility.Child"));
	const FLayoutValidationAssertionRecord* FailedAssertion =
		FindRewriteAssertion(ProofContract.ParentRequest.ValidationAssertions, ExpectedAssertionId);
	if (!TestNotNull(TEXT("Proof contract refresh keeps the negotiated child responsibility assertion"), FailedAssertion))
	{
		return false;
	}

	TestFalse(TEXT("Proof contract refresh rewrites the negotiated child responsibility assertion as failed"), FailedAssertion->bPassed);
	TestTrue(TEXT("Proof contract failure names the request path"), FailedAssertion->FailureReason.Contains(TEXT("Request 'Root' (RootSnapshot)")));
	TestTrue(TEXT("Proof contract failure keeps the frozen profile snapshot id"), FailedAssertion->FailureReason.Contains(TEXT("RootProfile")));
	TestTrue(TEXT("Proof contract failure keeps the frozen content-set snapshot id"), FailedAssertion->FailureReason.Contains(TEXT("RootContent")));
	TestTrue(TEXT("Proof contract failure keeps the frozen module catalog id"), FailedAssertion->FailureReason.Contains(TEXT("RootModule")));
	TestTrue(TEXT("Proof contract failure keeps the child region breadcrumb"), FailedAssertion->FailureReason.Contains(TEXT("Child")));
	TestTrue(TEXT("Proof contract failure related ids keep the root effective snapshot id"), FailedAssertion->RelatedIds.Contains(TEXT("RootSnapshot")));
	TestTrue(TEXT("Proof contract failure related ids keep the root profile snapshot id"), FailedAssertion->RelatedIds.Contains(TEXT("RootProfile")));
	TestTrue(TEXT("Proof contract failure related ids keep the root content-set snapshot id"), FailedAssertion->RelatedIds.Contains(TEXT("RootContent")));
	TestTrue(TEXT("Proof contract failure related ids keep the root module catalog id"), FailedAssertion->RelatedIds.Contains(TEXT("RootModule")));
	TestFalse(
		TEXT("Proof contract refresh removes the stale negotiated child responsibility proof record after failure"),
		ProofContract.ParentRequest.ProofRecords.ContainsByPredicate(
			[ExpectedAssertionId](const FLayoutProofRecord& ProofRecord)
			{
				return ProofRecord.ProofId == ExpectedAssertionId;
			}));
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofExecutionCompatibilityRequestCarriesSeamJunctionRequirementsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("ChildB"), 7151)
		});
	AppendProofContractJunctionCarrier(ProofContract);

	const FLayoutRegionSolveScheduleRequest CompatibilityRequest =
		BuildProofExecutionCompatibilityScheduleRequest(ProofContract);

	if (!TestEqual(TEXT("Compatibility request keeps one planned seam"), CompatibilityRequest.PlannedPartitionSeams.Num(), 1))
	{
		return false;
	}
	if (!TestEqual(TEXT("Compatibility request keeps one planned owner-side junction requirement"), CompatibilityRequest.PlannedJunctionRequirements.Num(), 1))
	{
		return false;
	}
	TestTrue(
		TEXT("Compatibility request keeps planned seam authority"),
		CompatibilityRequest.bPlannedPartitionSeamsAreAuthoritative);
	TestEqual(
		TEXT("Compatibility request keeps the planned seam id"),
		CompatibilityRequest.PlannedPartitionSeams[0].SeamId,
		ProofContract.SeamPlan.PlannedPartitionSeams[0].SeamId);

	const FLayoutOwnedSeamJunctionRequirement& PublicRequirement =
		CompatibilityRequest.PlannedJunctionRequirements[0];
	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		ProofContract.SeamPlan.JunctionRequirements[0];
	TestEqual(
		TEXT("Compatibility request keeps the junction requirement id"),
		PublicRequirement.JunctionRequirementId,
		JunctionRequirement.JunctionRequirementId);
	TestEqual(
		TEXT("Compatibility request keeps the owner region"),
		PublicRequirement.OwnerRegionDebugPath,
		JunctionRequirement.OwnerRegionDebugPath);
	TestEqual(
		TEXT("Compatibility request keeps the passive region"),
		PublicRequirement.PassiveRegionDebugPath,
		JunctionRequirement.PassiveRegionDebugPath);
	TestEqual(
		TEXT("Compatibility request keeps the adjacency class"),
		PublicRequirement.AdjacencyClassId,
		JunctionRequirement.AdjacencyClassId);
	TestEqual(
		TEXT("Compatibility request keeps the junction cell"),
		PublicRequirement.JunctionCell,
		JunctionRequirement.JunctionCell);
	TestEqual(
		TEXT("Compatibility request keeps the continuing seam id"),
		PublicRequirement.ContinuingSeamId,
		JunctionRequirement.ContinuingSeamId);
	TestEqual(
		TEXT("Compatibility request keeps the branch seam id"),
		PublicRequirement.BranchSeamId,
		JunctionRequirement.BranchSeamId);
	TestEqual(
		TEXT("Compatibility request keeps the continuing passive region"),
		PublicRequirement.ContinuingPassiveRegionDebugPath,
		JunctionRequirement.ContinuingPassiveRegionDebugPath);
	TestTrue(
		TEXT("Compatibility request keeps the interface family"),
		PublicRequirement.InterfaceFamily.MatchesTagExact(JunctionRequirement.InterfaceFamily));
	TestEqual(
		TEXT("Compatibility request keeps the continuing owner-face direction"),
		PublicRequirement.ContinuingOwnerFaceDirection,
		JunctionRequirement.ContinuingOwnerFaceDirection);
	TestEqual(
		TEXT("Compatibility request keeps the branch owner-face direction"),
		PublicRequirement.BranchOwnerFaceDirection,
		JunctionRequirement.BranchOwnerFaceDirection);
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofExecutionCompatibilityRequestUsesStableParentChildOrderTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("ChildB"), 7201),
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("ChildA"), 7202)
		});

	const FLayoutRegionSolveScheduleRequest CompatibilityRequest =
		BuildProofExecutionCompatibilityScheduleRequest(ProofContract);

	if (!TestEqual(TEXT("Compatibility request keeps one parent plus two child proof requests"), CompatibilityRequest.RegionRequests.Num(), 3))
	{
		return false;
	}
	if (!TestEqual(TEXT("Compatibility request keeps one dependency edge per child request"), CompatibilityRequest.Dependencies.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("Compatibility request keeps the parent request first"), CompatibilityRequest.RegionRequests[0].RegionDebugPath, FString(TEXT("Root")));
	TestEqual(TEXT("Compatibility request orders child proof requests by the stable merge order"), CompatibilityRequest.RegionRequests[1].RegionDebugPath, FString(TEXT("ChildA")));
	TestEqual(TEXT("Compatibility request orders child proof requests by the stable merge order"), CompatibilityRequest.RegionRequests[2].RegionDebugPath, FString(TEXT("ChildB")));
	TestEqual(TEXT("First dependency starts from the parent request"), CompatibilityRequest.Dependencies[0].PrerequisiteRegionDebugPath, FString(TEXT("Root")));
	TestEqual(TEXT("First dependency targets the first stable child"), CompatibilityRequest.Dependencies[0].DependentRegionDebugPath, FString(TEXT("ChildA")));
	TestEqual(TEXT("Second dependency targets the second stable child"), CompatibilityRequest.Dependencies[1].DependentRegionDebugPath, FString(TEXT("ChildB")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofExecutionCompatibilityRequestPreservesDistinctMultiChildProofInputsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("ChildB"), 7261),
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("ChildA"), 7262)
		});

	if (!TestEqual(TEXT("Distinct multi-child compatibility fixture keeps two child proof requests"), ProofContract.ChildRequests.Num(), 2))
	{
		return false;
	}

	FLayoutRegionSolveRequest* ChildARequest = ProofContract.ChildRequests.FindByPredicate(
		[](const FLayoutRegionSolveRequest& Request)
		{
			return Request.RegionDebugPath == TEXT("ChildA");
		});
	FLayoutRegionSolveRequest* ChildBRequest = ProofContract.ChildRequests.FindByPredicate(
		[](const FLayoutRegionSolveRequest& Request)
		{
			return Request.RegionDebugPath == TEXT("ChildB");
		});
	if (!TestNotNull(TEXT("Distinct multi-child compatibility fixture keeps ChildA request"), ChildARequest)
		|| !TestNotNull(TEXT("Distinct multi-child compatibility fixture keeps ChildB request"), ChildBRequest))
	{
		return false;
	}

	ChildARequest->SourceParentRegionDebugPath = TEXT("Root");
	ChildARequest->SourceContentEntryId = TEXT("ChildAEntry");
	ChildARequest->FootprintSize = FIntPoint(2, 1);
	ChildARequest->PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Boundary)
	};
	ChildARequest->ProtectedStructuralCells = {FIntVector(4, 0, 0)};
	ChildARequest->CommittedTraversalAnchors = {MakeTraversalAnchor(FIntVector(5, 0, 0))};
	PopulateRewriteSteppedTerrainSupportMap(ChildARequest->SteppedTerrainSupportMap);
	FLayoutForcedPlacementBundleInsertion& ChildAForcedInsertion =
		ChildARequest->ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
	ChildAForcedInsertion.BundleId = TEXT("ChildAForcedBundle");
	ChildAForcedInsertion.AnchorCell = FIntVector(0, 0, 0);
	ChildAForcedInsertion.ProvingCell = FIntVector(1, 0, 0);
	PopulateRewriteRouteConstraint(
		ChildARequest->RequiredRouteConstraints.AddDefaulted_GetRef(),
		TEXT("ChildACompatibilityRoute"),
		FIntVector(1, 0, 0),
		ELayoutCellIntent::Interior);
	ChildARequest->ProofRecords = {
		MakeRewriteProofRecord(TEXT("ChildAProof"), TEXT("ChildATarget"))
	};
	ChildARequest->ValidationAssertions = {
		MakeRewriteValidationAssertion(TEXT("ChildAAssertion"), true)
	};

	ChildBRequest->SourceParentRegionDebugPath = TEXT("Root");
	ChildBRequest->SourceContentEntryId = TEXT("ChildBEntry");
	ChildBRequest->FootprintSize = FIntPoint(1, 2);
	ChildBRequest->PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Boundary),
		MakePlannedCell(FIntVector(0, 1, 0), ELayoutCellIntent::Interior)
	};
	ChildBRequest->ProtectedStructuralCells = {FIntVector(6, 0, 0)};
	ChildBRequest->CommittedTraversalAnchors = {MakeTraversalAnchor(FIntVector(7, 0, 0))};
	PopulateRewriteSteppedTerrainSupportMap(ChildBRequest->SteppedTerrainSupportMap);
	ChildBRequest->SteppedTerrainSupportMap.SupportSamples[0].SupportSurfaceZ = 48;
	ChildBRequest->SteppedTerrainSupportMap.SupportSamples[1].SupportSurfaceZ = 64;
	ChildBRequest->SteppedTerrainSupportMap.AdjacencySteps[0].StepHeightBlocks = 8;
	ChildBRequest->SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 2;
	ChildBRequest->ProofRecords = {
		MakeRewriteProofRecord(TEXT("ChildBProof"), TEXT("ChildBTarget"))
	};
	ChildBRequest->ValidationAssertions = {
		MakeRewriteValidationAssertion(TEXT("ChildBAssertion"), false)
	};

	const FLayoutRegionSolveScheduleRequest CompatibilityRequest =
		BuildProofExecutionCompatibilityScheduleRequest(ProofContract);

	if (!TestEqual(TEXT("Distinct multi-child compatibility request keeps one parent plus two child proof requests"), CompatibilityRequest.RegionRequests.Num(), 3))
	{
		return false;
	}

	const FLayoutRegionSolveRequest& ChildACompatibilityRequest = CompatibilityRequest.RegionRequests[1];
	const FLayoutRegionSolveRequest& ChildBCompatibilityRequest = CompatibilityRequest.RegionRequests[2];

	TestEqual(TEXT("Distinct multi-child compatibility request keeps ChildA first by stable merge order"), ChildACompatibilityRequest.RegionDebugPath, FString(TEXT("ChildA")));
	TestEqual(TEXT("Distinct multi-child compatibility request keeps ChildB second by stable merge order"), ChildBCompatibilityRequest.RegionDebugPath, FString(TEXT("ChildB")));

	TestEqual(TEXT("Distinct multi-child compatibility request keeps ChildA source content entry id"), ChildACompatibilityRequest.SourceContentEntryId, FName(TEXT("ChildAEntry")));
	TestEqual(TEXT("Distinct multi-child compatibility request keeps ChildA protected structural cell"), ChildACompatibilityRequest.ProtectedStructuralCells[0], FIntVector(4, 0, 0));
	TestEqual(TEXT("Distinct multi-child compatibility request keeps ChildA committed traversal anchor"), ChildACompatibilityRequest.CommittedTraversalAnchors[0].Cell, FIntVector(5, 0, 0));
	TestTrue(
		TEXT("Distinct multi-child compatibility request keeps ChildA stepped terrain support map"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			ChildACompatibilityRequest.SteppedTerrainSupportMap,
			ChildARequest->SteppedTerrainSupportMap));
	TestEqual(TEXT("Distinct multi-child compatibility request keeps one ChildA forced placement bundle insertion"), ChildACompatibilityRequest.ForcedPlacementBundleInsertions.Num(), 1);
	TestEqual(TEXT("Distinct multi-child compatibility request keeps the ChildA forced placement bundle id"), ChildACompatibilityRequest.ForcedPlacementBundleInsertions[0].BundleId, FLayoutId(TEXT("ChildAForcedBundle")));
	TestEqual(TEXT("Distinct multi-child compatibility request keeps one ChildA required route constraint"), ChildACompatibilityRequest.RequiredRouteConstraints.Num(), 1);
	TestEqual(TEXT("Distinct multi-child compatibility request keeps the ChildA required route constraint id"), ChildACompatibilityRequest.RequiredRouteConstraints[0].ConstraintId, FLayoutId(TEXT("ChildACompatibilityRoute")));
	TestEqual(TEXT("Distinct multi-child compatibility request keeps one ChildA proof record"), ChildACompatibilityRequest.ProofRecords.Num(), 1);
	TestEqual(TEXT("Distinct multi-child compatibility request keeps the ChildA proof record id"), ChildACompatibilityRequest.ProofRecords[0].ProofId, FLayoutId(TEXT("ChildAProof")));
	TestEqual(TEXT("Distinct multi-child compatibility request keeps one ChildA validation assertion"), ChildACompatibilityRequest.ValidationAssertions.Num(), 1);
	TestEqual(TEXT("Distinct multi-child compatibility request keeps the ChildA validation assertion id"), ChildACompatibilityRequest.ValidationAssertions[0].AssertionId, FLayoutId(TEXT("ChildAAssertion")));

	TestEqual(TEXT("Distinct multi-child compatibility request keeps ChildB source content entry id"), ChildBCompatibilityRequest.SourceContentEntryId, FName(TEXT("ChildBEntry")));
	TestEqual(TEXT("Distinct multi-child compatibility request keeps ChildB protected structural cell"), ChildBCompatibilityRequest.ProtectedStructuralCells[0], FIntVector(6, 0, 0));
	TestEqual(TEXT("Distinct multi-child compatibility request keeps ChildB committed traversal anchor"), ChildBCompatibilityRequest.CommittedTraversalAnchors[0].Cell, FIntVector(7, 0, 0));
	TestTrue(
		TEXT("Distinct multi-child compatibility request keeps ChildB stepped terrain support map"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			ChildBCompatibilityRequest.SteppedTerrainSupportMap,
			ChildBRequest->SteppedTerrainSupportMap));
	TestEqual(TEXT("Distinct multi-child compatibility request keeps no ChildB forced placement bundle insertions"), ChildBCompatibilityRequest.ForcedPlacementBundleInsertions.Num(), 0);
	TestEqual(TEXT("Distinct multi-child compatibility request keeps no ChildB required route constraints"), ChildBCompatibilityRequest.RequiredRouteConstraints.Num(), 0);
	TestEqual(TEXT("Distinct multi-child compatibility request keeps one ChildB proof record"), ChildBCompatibilityRequest.ProofRecords.Num(), 1);
	TestEqual(TEXT("Distinct multi-child compatibility request keeps the ChildB proof record id"), ChildBCompatibilityRequest.ProofRecords[0].ProofId, FLayoutId(TEXT("ChildBProof")));
	TestEqual(TEXT("Distinct multi-child compatibility request keeps one ChildB validation assertion"), ChildBCompatibilityRequest.ValidationAssertions.Num(), 1);
	TestEqual(TEXT("Distinct multi-child compatibility request keeps the ChildB validation assertion id"), ChildBCompatibilityRequest.ValidationAssertions[0].AssertionId, FLayoutId(TEXT("ChildBAssertion")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofExecutionCompatibilityRequestPreservesDirectChildCommitmentsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("ChildA"), 7241);
	DemandResult.ResponsibilitySet.ContactSet.bAllowsChildTraversalBridge = true;

	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{
			DemandResult
		});

	const FLayoutRegionSolveScheduleRequest CompatibilityRequest =
		BuildProofExecutionCompatibilityScheduleRequest(ProofContract);

	if (!TestEqual(TEXT("Compatibility request keeps one direct child commitment"), CompatibilityRequest.DirectChildCommitments.Num(), 1))
	{
		return false;
	}

	const FLayoutDirectChildRegionCommitment& PublicCommitment =
		CompatibilityRequest.DirectChildCommitments[0];
	const FLayoutDirectChildRegionCommitment& ExpectedCommitment =
		ProofContract.DirectChildCommitments[0];

	TestEqual(TEXT("Compatibility request keeps the direct child commitment parent region"), PublicCommitment.ParentRegionDebugPath, ExpectedCommitment.ParentRegionDebugPath);
	TestEqual(TEXT("Compatibility request keeps the direct child commitment child region"), PublicCommitment.ChildRegionDebugPath, ExpectedCommitment.ChildRegionDebugPath);
	TestEqual(TEXT("Compatibility request keeps both committed child anchors"), PublicCommitment.EndpointCommitments.Num(), ExpectedCommitment.EndpointCommitments.Num());
	TestEqual(TEXT("Compatibility request keeps the first committed child-anchor id"), PublicCommitment.EndpointCommitments[0].CommitmentId, ExpectedCommitment.EndpointCommitments[0].CommitmentId);
	TestEqual(TEXT("Compatibility request keeps the second committed child-anchor id"), PublicCommitment.EndpointCommitments[1].CommitmentId, ExpectedCommitment.EndpointCommitments[1].CommitmentId);
	TestEqual(TEXT("Compatibility request keeps the committed parent-ingress subset"), PublicCommitment.ParentTraversalIngressCommitments.Num(), ExpectedCommitment.ParentTraversalIngressCommitments.Num());
	TestEqual(TEXT("Compatibility request keeps the committed parent-ingress anchor cell"), PublicCommitment.ParentTraversalIngressCommitments[0].Cell, ExpectedCommitment.ParentTraversalIngressCommitments[0].Cell);
	TestTrue(TEXT("Compatibility request keeps the committed parent-ingress traversal channel"), PublicCommitment.ParentTraversalIngressCommitments[0].TraversalChannel.MatchesTagExact(ExpectedCommitment.ParentTraversalIngressCommitments[0].TraversalChannel));
	TestEqual(TEXT("Compatibility request keeps the child-bridge eligibility flag"), PublicCommitment.bAllowsChildTraversalBridgeForCommittedContacts, ExpectedCommitment.bAllowsChildTraversalBridgeForCommittedContacts);
	TestEqual(TEXT("Compatibility request keeps the negotiated responsibility-contract parent region"), PublicCommitment.NegotiatedResponsibilityContract.ParentRegionDebugPath, ExpectedCommitment.NegotiatedResponsibilityContract.ParentRegionDebugPath);
	TestEqual(TEXT("Compatibility request keeps the negotiated responsibility-contract child region"), PublicCommitment.NegotiatedResponsibilityContract.ChildRegionDebugPath, ExpectedCommitment.NegotiatedResponsibilityContract.ChildRegionDebugPath);
	TestEqual(TEXT("Compatibility request keeps the negotiated responsibility-contract host ownership"), PublicCommitment.NegotiatedResponsibilityContract.HostVerticalAccessResponsibility, ExpectedCommitment.NegotiatedResponsibilityContract.HostVerticalAccessResponsibility);
	TestEqual(TEXT("Compatibility request keeps the negotiated responsibility-contract required host-provider count"), PublicCommitment.NegotiatedResponsibilityContract.RequiredHostProviderCount, ExpectedCommitment.NegotiatedResponsibilityContract.RequiredHostProviderCount);
	TestEqual(TEXT("Compatibility request keeps the negotiated responsibility-contract counted child-provider region"), PublicCommitment.NegotiatedResponsibilityContract.CountedChildProviderRegionDebugPaths[0], ExpectedCommitment.NegotiatedResponsibilityContract.CountedChildProviderRegionDebugPaths[0]);
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofExecutionCompatibilityRequestPreservesRequestCarriedProofInputsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("ChildA"), 7251)
		});

	ProofContract.ParentRequest.TemplatePlacementZOffsetBlocks = 9;
	ProofContract.ParentRequest.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	ProofContract.ParentRequest.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 384;
	ProofContract.ParentRequest.RootSolveId = TEXT("RootSolve");
	ProofContract.ParentRequest.RootCandidateId = TEXT("RootCandidate");
	ProofContract.ParentRequest.RootPlacementPolicyId = TEXT("RootPlacementPolicy");
	ProofContract.ParentRequest.ProtectedStructuralCells = {FIntVector(7, 0, 0)};
	ProofContract.ParentRequest.CommittedTraversalAnchors = {MakeTraversalAnchor(FIntVector(6, 0, 0))};
	PopulateRewriteSteppedTerrainSupportMap(ProofContract.ParentRequest.SteppedTerrainSupportMap);
	FLayoutForcedPlacementBundleInsertion& ParentForcedInsertion =
		ProofContract.ParentRequest.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
	ParentForcedInsertion.BundleId = TEXT("ParentSupportBundle");
	ParentForcedInsertion.AnchorCell = FIntVector(4, 0, 0);
	ParentForcedInsertion.ProvingCell = FIntVector(5, 0, 0);
	PopulateRewriteRouteConstraint(
		ProofContract.ParentRequest.RequiredRouteConstraints.AddDefaulted_GetRef(),
		TEXT("ParentCompatibilityRoute"),
		FIntVector(5, 0, 0),
		ELayoutCellIntent::Interior);
	ProofContract.ParentRequest.bDeferClosureValidationToSchedule = true;
	ProofContract.ParentRequest.bDeferTraversalValidationToSchedule = true;
	ProofContract.ParentRequest.ProofRecords = {
		MakeRewriteProofRecord(TEXT("ParentProof"), TEXT("ParentTarget"))
	};
	ProofContract.ParentRequest.ValidationAssertions = {
		MakeRewriteValidationAssertion(TEXT("ParentAssertion"), true)
	};

	FLayoutRegionSolveRequest& ChildRequest = ProofContract.ChildRequests[0];
	ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	ChildRequest.SourceContentEntryId = TEXT("ChildEntry");
	ChildRequest.bSourceContentEntryOptional = true;
	/* !ChildRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	ChildRequest.FootprintSize = FIntPoint(2, 1);
	ChildRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior)
	};
	ChildRequest.ProtectedStructuralCells = {FIntVector(2, 0, 0)};
	ChildRequest.CommittedTraversalAnchors = {MakeTraversalAnchor(FIntVector(3, 0, 0))};
	PopulateRewriteSteppedTerrainSupportMap(ChildRequest.SteppedTerrainSupportMap);
	FLayoutForcedPlacementBundleInsertion& ChildForcedInsertion =
		ChildRequest.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
	ChildForcedInsertion.BundleId = TEXT("ChildSupportBundle");
	ChildForcedInsertion.AnchorCell = FIntVector(0, 0, 0);
	ChildForcedInsertion.ProvingCell = FIntVector(1, 0, 0);
	PopulateRewriteRouteConstraint(
		ChildRequest.RequiredRouteConstraints.AddDefaulted_GetRef(),
		TEXT("ChildCompatibilityRoute"),
		FIntVector(1, 0, 0),
		ELayoutCellIntent::Interior);
	ChildRequest.bUseSuppliedChildCapabilityEnvelope = true;
	ChildRequest.SuppliedChildCapabilityEnvelope.RegionDebugPath = TEXT("ChildA");
	ChildRequest.SuppliedChildCapabilityEnvelope.SnapshotId = TEXT("ChildEnvelopeSnapshot");
	ChildRequest.SuppliedChildCapabilityEnvelope.ProofRecords = {
		MakeRewriteProofRecord(TEXT("EnvelopeProof"), TEXT("EnvelopeTarget"))
	};
	ChildRequest.SuppliedChildCapabilityEnvelope.ValidationAssertions = {
		MakeRewriteValidationAssertion(TEXT("EnvelopeAssertion"), true)
	};
	ChildRequest.bDeferClosureValidationToSchedule = true;
	ChildRequest.bDeferTraversalValidationToSchedule = true;
	ChildRequest.ProofRecords = {
		MakeRewriteProofRecord(TEXT("ChildProof"), TEXT("ChildTarget"))
	};
	ChildRequest.ValidationAssertions = {
		MakeRewriteValidationAssertion(TEXT("ChildAssertion"), true)
	};
	ChildRequest.RootSolveId = TEXT("RootSolve");
	ChildRequest.RootCandidateId = TEXT("RootCandidate");
	ChildRequest.RootPlacementPolicyId = TEXT("RootPlacementPolicy");

	const FLayoutRegionSolveScheduleRequest CompatibilityRequest =
		BuildProofExecutionCompatibilityScheduleRequest(ProofContract);

	if (!TestEqual(TEXT("Compatibility request keeps one parent plus one child proof request"), CompatibilityRequest.RegionRequests.Num(), 2))
	{
		return false;
	}

	const FLayoutRegionSolveRequest& ParentCompatibilityRequest = CompatibilityRequest.RegionRequests[0];
	TestEqual(TEXT("Compatibility request keeps the parent template placement Z offset"), ParentCompatibilityRequest.TemplatePlacementZOffsetBlocks, 9);
	TestEqual(TEXT("Compatibility request keeps the parent world-binding placement kind"), ParentCompatibilityRequest.RootPlacementKind, ELayoutWorldBindingPlacementKind::OrdinaryRoot);
	TestEqual(TEXT("Compatibility request keeps the parent terrain search start"), ParentCompatibilityRequest.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ, 384);
	TestEqual(TEXT("Compatibility request keeps the parent root solve id"), ParentCompatibilityRequest.RootSolveId, FLayoutId(TEXT("RootSolve")));
	TestEqual(TEXT("Compatibility request keeps the parent root candidate id"), ParentCompatibilityRequest.RootCandidateId, FLayoutId(TEXT("RootCandidate")));
	TestEqual(TEXT("Compatibility request keeps the parent root placement policy id"), ParentCompatibilityRequest.RootPlacementPolicyId, FLayoutId(TEXT("RootPlacementPolicy")));
	TestEqual(TEXT("Compatibility request keeps one parent protected structural cell"), ParentCompatibilityRequest.ProtectedStructuralCells.Num(), 1);
	TestEqual(TEXT("Compatibility request keeps the parent protected structural cell"), ParentCompatibilityRequest.ProtectedStructuralCells[0], FIntVector(7, 0, 0));
	TestEqual(TEXT("Compatibility request keeps one parent committed traversal anchor"), ParentCompatibilityRequest.CommittedTraversalAnchors.Num(), 1);
	TestEqual(TEXT("Compatibility request keeps the parent committed traversal anchor cell"), ParentCompatibilityRequest.CommittedTraversalAnchors[0].Cell, FIntVector(6, 0, 0));
	TestTrue(
		TEXT("Compatibility request keeps the parent stepped terrain support map"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			ParentCompatibilityRequest.SteppedTerrainSupportMap,
			ProofContract.ParentRequest.SteppedTerrainSupportMap));
	TestEqual(TEXT("Compatibility request keeps one parent forced placement bundle insertion"), ParentCompatibilityRequest.ForcedPlacementBundleInsertions.Num(), 1);
	TestEqual(TEXT("Compatibility request keeps the parent forced placement bundle id"), ParentCompatibilityRequest.ForcedPlacementBundleInsertions[0].BundleId, FLayoutId(TEXT("ParentSupportBundle")));
	TestEqual(TEXT("Compatibility request keeps the parent forced placement anchor cell"), ParentCompatibilityRequest.ForcedPlacementBundleInsertions[0].AnchorCell, FIntVector(4, 0, 0));
	TestEqual(TEXT("Compatibility request keeps the parent forced placement proving cell"), ParentCompatibilityRequest.ForcedPlacementBundleInsertions[0].ProvingCell, FIntVector(5, 0, 0));
	TestEqual(TEXT("Compatibility request keeps one parent required route constraint"), ParentCompatibilityRequest.RequiredRouteConstraints.Num(), 1);
	TestEqual(TEXT("Compatibility request keeps the parent required route constraint id"), ParentCompatibilityRequest.RequiredRouteConstraints[0].ConstraintId, FLayoutId(TEXT("ParentCompatibilityRoute")));
	TestEqual(TEXT("Compatibility request keeps the parent required route constraint cell"), ParentCompatibilityRequest.RequiredRouteConstraints[0].Cell, FIntVector(5, 0, 0));
	TestTrue(TEXT("Compatibility request keeps the parent required route traversal channel"), ParentCompatibilityRequest.RequiredRouteConstraints[0].FaceRequirements[0].TraversalChannel == LayoutGameplayTags::TraversalPrimary);
	TestTrue(TEXT("Compatibility request keeps the parent deferred closure flag"), ParentCompatibilityRequest.bDeferClosureValidationToSchedule);
	TestTrue(TEXT("Compatibility request keeps the parent deferred traversal flag"), ParentCompatibilityRequest.bDeferTraversalValidationToSchedule);
	TestEqual(TEXT("Compatibility request keeps one parent proof record"), ParentCompatibilityRequest.ProofRecords.Num(), 1);
	TestEqual(TEXT("Compatibility request keeps the parent proof record id"), ParentCompatibilityRequest.ProofRecords[0].ProofId, FLayoutId(TEXT("ParentProof")));
	TestEqual(TEXT("Compatibility request keeps one parent validation assertion"), ParentCompatibilityRequest.ValidationAssertions.Num(), 1);
	TestEqual(TEXT("Compatibility request keeps the parent validation assertion id"), ParentCompatibilityRequest.ValidationAssertions[0].AssertionId, FLayoutId(TEXT("ParentAssertion")));

	const FLayoutRegionSolveRequest& ChildCompatibilityRequest = CompatibilityRequest.RegionRequests[1];
	TestEqual(TEXT("Compatibility request keeps the child source parent region path"), ChildCompatibilityRequest.SourceParentRegionDebugPath, FString(TEXT("Root")));
	TestEqual(TEXT("Compatibility request keeps the child source content entry id"), ChildCompatibilityRequest.SourceContentEntryId, FName(TEXT("ChildEntry")));
	TestTrue(TEXT("Compatibility request keeps the child optional flag"), ChildCompatibilityRequest.bSourceContentEntryOptional);
	TestTrue(TEXT("Compatibility request keeps the child supplied planned-cells flag"), !ChildCompatibilityRequest.PlannedCells.IsEmpty());
	TestEqual(TEXT("Compatibility request keeps the child footprint size"), ChildCompatibilityRequest.FootprintSize, FIntPoint(2, 1));
	TestEqual(TEXT("Compatibility request keeps both child planned cells"), ChildCompatibilityRequest.PlannedCells.Num(), 2);
	TestEqual(TEXT("Compatibility request keeps one child protected structural cell"), ChildCompatibilityRequest.ProtectedStructuralCells.Num(), 1);
	TestEqual(TEXT("Compatibility request keeps the child protected structural cell"), ChildCompatibilityRequest.ProtectedStructuralCells[0], FIntVector(2, 0, 0));
	TestEqual(TEXT("Compatibility request keeps one child committed traversal anchor"), ChildCompatibilityRequest.CommittedTraversalAnchors.Num(), 1);
	TestEqual(TEXT("Compatibility request keeps the child committed traversal anchor cell"), ChildCompatibilityRequest.CommittedTraversalAnchors[0].Cell, FIntVector(3, 0, 0));
	TestEqual(TEXT("Compatibility request keeps the child stepped support shared cell height"), ChildCompatibilityRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks, 16);
	TestEqual(TEXT("Compatibility request keeps both child stepped support samples"), ChildCompatibilityRequest.SteppedTerrainSupportMap.SupportSamples.Num(), 2);
	TestEqual(TEXT("Compatibility request keeps the child stepped support adjacency count"), ChildCompatibilityRequest.SteppedTerrainSupportMap.AdjacencySteps.Num(), 1);
	TestEqual(TEXT("Compatibility request keeps the child stepped support max neighbor delta"), ChildCompatibilityRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta, 3);
	TestEqual(TEXT("Compatibility request keeps one child forced placement bundle insertion"), ChildCompatibilityRequest.ForcedPlacementBundleInsertions.Num(), 1);
	TestEqual(TEXT("Compatibility request keeps the child forced placement bundle id"), ChildCompatibilityRequest.ForcedPlacementBundleInsertions[0].BundleId, FLayoutId(TEXT("ChildSupportBundle")));
	TestEqual(TEXT("Compatibility request keeps the child forced placement anchor cell"), ChildCompatibilityRequest.ForcedPlacementBundleInsertions[0].AnchorCell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Compatibility request keeps the child forced placement proving cell"), ChildCompatibilityRequest.ForcedPlacementBundleInsertions[0].ProvingCell, FIntVector(1, 0, 0));
	TestEqual(TEXT("Compatibility request keeps one child required route constraint"), ChildCompatibilityRequest.RequiredRouteConstraints.Num(), 1);
	TestEqual(TEXT("Compatibility request keeps the child required route constraint id"), ChildCompatibilityRequest.RequiredRouteConstraints[0].ConstraintId, FLayoutId(TEXT("ChildCompatibilityRoute")));
	TestEqual(TEXT("Compatibility request keeps the child required route constraint cell"), ChildCompatibilityRequest.RequiredRouteConstraints[0].Cell, FIntVector(1, 0, 0));
	TestTrue(TEXT("Compatibility request keeps the child required route traversal channel"), ChildCompatibilityRequest.RequiredRouteConstraints[0].FaceRequirements[0].TraversalChannel == LayoutGameplayTags::TraversalPrimary);
	TestTrue(TEXT("Compatibility request keeps the child supplied capability-envelope flag"), ChildCompatibilityRequest.bUseSuppliedChildCapabilityEnvelope);
	TestEqual(TEXT("Compatibility request keeps the child supplied capability-envelope region"), ChildCompatibilityRequest.SuppliedChildCapabilityEnvelope.RegionDebugPath, FString(TEXT("ChildA")));
	TestEqual(TEXT("Compatibility request keeps the child supplied capability-envelope snapshot id"), ChildCompatibilityRequest.SuppliedChildCapabilityEnvelope.SnapshotId, FLayoutId(TEXT("ChildEnvelopeSnapshot")));
	TestEqual(TEXT("Compatibility request keeps one child supplied capability-envelope proof record"), ChildCompatibilityRequest.SuppliedChildCapabilityEnvelope.ProofRecords.Num(), 1);
	TestEqual(TEXT("Compatibility request keeps the child supplied capability-envelope proof record id"), ChildCompatibilityRequest.SuppliedChildCapabilityEnvelope.ProofRecords[0].ProofId, FLayoutId(TEXT("EnvelopeProof")));
	TestEqual(TEXT("Compatibility request keeps one child supplied capability-envelope validation assertion"), ChildCompatibilityRequest.SuppliedChildCapabilityEnvelope.ValidationAssertions.Num(), 1);
	TestEqual(TEXT("Compatibility request keeps the child supplied capability-envelope validation assertion id"), ChildCompatibilityRequest.SuppliedChildCapabilityEnvelope.ValidationAssertions[0].AssertionId, FLayoutId(TEXT("EnvelopeAssertion")));
	TestTrue(TEXT("Compatibility request keeps the child deferred closure flag"), ChildCompatibilityRequest.bDeferClosureValidationToSchedule);
	TestTrue(TEXT("Compatibility request keeps the child deferred traversal flag"), ChildCompatibilityRequest.bDeferTraversalValidationToSchedule);
	TestEqual(TEXT("Compatibility request keeps one child proof record"), ChildCompatibilityRequest.ProofRecords.Num(), 1);
	TestEqual(TEXT("Compatibility request keeps the child proof record id"), ChildCompatibilityRequest.ProofRecords[0].ProofId, FLayoutId(TEXT("ChildProof")));
	TestEqual(TEXT("Compatibility request keeps one child validation assertion"), ChildCompatibilityRequest.ValidationAssertions.Num(), 1);
	TestEqual(TEXT("Compatibility request keeps the child validation assertion id"), ChildCompatibilityRequest.ValidationAssertions[0].AssertionId, FLayoutId(TEXT("ChildAssertion")));
	TestEqual(TEXT("Compatibility request keeps the child root solve id"), ChildCompatibilityRequest.RootSolveId, FLayoutId(TEXT("RootSolve")));
	TestEqual(TEXT("Compatibility request keeps the child root candidate id"), ChildCompatibilityRequest.RootCandidateId, FLayoutId(TEXT("RootCandidate")));
	TestEqual(TEXT("Compatibility request keeps the child root placement policy id"), ChildCompatibilityRequest.RootPlacementPolicyId, FLayoutId(TEXT("RootPlacementPolicy")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofExecutionCompatibilityRequestPreservesSupportableMixedRootSteppedCarriersTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	PopulateRewriteRouteConstraint(
		RootRequest.RequiredRouteConstraints.AddDefaulted_GetRef(),
		TEXT("RootCompatibilityRoute"),
		FIntVector(1, 0, 0),
		ELayoutCellIntent::Interior);
	FLayoutSteppedTerrainSupportSample& FirstRootSupportSample =
		RootRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
	FirstRootSupportSample.LocalCell = FIntVector(0, 0, 0);
	FirstRootSupportSample.SupportSurfaceZ = 64;
	FLayoutSteppedTerrainSupportSample& SecondRootSupportSample =
		RootRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
	SecondRootSupportSample.LocalCell = FIntVector(1, 0, 0);
	SecondRootSupportSample.SupportSurfaceZ = 80;
	FLayoutSteppedTerrainAdjacencyStep& RootAdjacencyStep =
		RootRequest.SteppedTerrainSupportMap.AdjacencySteps.AddDefaulted_GetRef();
	RootAdjacencyStep.FromCell = FIntVector(0, 0, 0);
	RootAdjacencyStep.ToCell = FIntVector(1, 0, 0);
	RootAdjacencyStep.StepHeightBlocks = 16;
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 16;
	FLayoutForcedPlacementBundleInsertion& RootInsertion =
		RootRequest.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
	RootInsertion.BundleId = BuildRewriteRequestPlacementBundleId(RootRequest, 0);
	RootInsertion.AnchorCell = FIntVector(0, 0, 0);
	RootInsertion.ProvingCell = FIntVector(0, 0, 0);

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Supportable mixed compatibility-request fixture executes successfully"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	const FLayoutRegionSolveScheduleRequest CompatibilityRequest =
		BuildProofExecutionCompatibilityScheduleRequest(
			CoordinatorResult.ProofContract);
	if (!TestEqual(
		TEXT("Supportable mixed compatibility-request fixture keeps one parent plus two child proof requests"),
		CompatibilityRequest.RegionRequests.Num(),
		3))
	{
		return false;
	}

	const FLayoutRegionSolveRequest& ParentCompatibilityRequest =
		CompatibilityRequest.RegionRequests[0];
	TestTrue(
		TEXT("Supportable mixed compatibility request keeps the same root stepped terrain support map"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			ParentCompatibilityRequest.SteppedTerrainSupportMap,
			CoordinatorResult.ProofContract.ParentRequest.SteppedTerrainSupportMap));
	TestTrue(
		TEXT("Supportable mixed compatibility request keeps the same root forced placement bundle insertions"),
		AreRewriteForcedPlacementBundleInsertionsEquivalent(
			ParentCompatibilityRequest.ForcedPlacementBundleInsertions,
			CoordinatorResult.ProofContract.ParentRequest.ForcedPlacementBundleInsertions));
	TestTrue(
		TEXT("Supportable mixed compatibility request keeps the same root required route constraints"),
		AreRewriteRouteConstraintArraysEquivalent(
			ParentCompatibilityRequest.RequiredRouteConstraints,
			CoordinatorResult.ProofContract.ParentRequest.RequiredRouteConstraints));
	TestTrue(
		TEXT("Supportable mixed compatibility request keeps the root stepped terrain support map on the parent request"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			ParentCompatibilityRequest.SteppedTerrainSupportMap,
			RootRequest.SteppedTerrainSupportMap));
	TestEqual(
		TEXT("Supportable mixed compatibility request keeps one root forced placement bundle insertion on the parent request"),
		ParentCompatibilityRequest.ForcedPlacementBundleInsertions.Num(),
		1);
	TestEqual(
		TEXT("Supportable mixed compatibility request keeps the root forced placement bundle id on the parent request"),
		ParentCompatibilityRequest.ForcedPlacementBundleInsertions[0].BundleId,
		RootInsertion.BundleId);
	TestEqual(
		TEXT("Supportable mixed compatibility request keeps one root required route constraint on the parent request"),
		ParentCompatibilityRequest.RequiredRouteConstraints.Num(),
		1);
	TestEqual(
		TEXT("Supportable mixed compatibility request keeps the root required route constraint id on the parent request"),
		ParentCompatibilityRequest.RequiredRouteConstraints[0].ConstraintId,
		FLayoutId(TEXT("RootCompatibilityRoute")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteExecuteNegotiatedProofScheduleUsesIndependentZeroEndpointSplitTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	FNegotiatedDemandResult DemandResult;
	if (!BuildExecutableZeroEndpointProofFixture(*this, RootRequest, DemandResult))
	{
		return false;
	}
	PopulateRewriteSteppedTerrainSupportMap(DemandResult.ChildRequest.SteppedTerrainSupportMap);

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract =
		BuildNegotiatedProofScheduleContract(SolveContext, {DemandResult});
	if (!TestEqual(TEXT("Zero-endpoint proof fixture emits no direct child commitments"), ProofContract.DirectChildCommitments.Num(), 0))
	{
		return false;
	}
	if (!TestEqual(TEXT("Zero-endpoint proof fixture keeps one child proof request"), ProofContract.ChildRequests.Num(), 1))
	{
		return false;
	}
	TestTrue(
		TEXT("Zero-endpoint proof fixture keeps the stepped terrain support map on the child proof request"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			ProofContract.ChildRequests[0].SteppedTerrainSupportMap,
			DemandResult.ChildRequest.SteppedTerrainSupportMap));

	FLayoutRegionSolveResult ExpectedChildResult;
	FString ExpectedChildProofFailureReason;
	if (!TestTrue(
		TEXT("Zero-endpoint proof fixture direct child proof succeeds"),
		LayoutProfileSolverInternal::BuildIndependentChildProofResult(
			ProofContract.ChildRequests[0],
			ExpectedChildResult,
			ExpectedChildProofFailureReason)))
	{
		AddError(ExpectedChildProofFailureReason);
		return false;
	}
	TestTrue(
		TEXT("Zero-endpoint proof fixture direct child proof keeps the stepped terrain support map"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			ExpectedChildResult.SteppedTerrainSupportMap,
			DemandResult.ChildRequest.SteppedTerrainSupportMap));
	FLayoutRegionSolveRequest SnapshotOnlyChildRequest = ProofContract.ChildRequests[0];
	StripRewriteLiveSourcesFromRequest(SnapshotOnlyChildRequest);
	FLayoutRegionSolveResult SnapshotOnlyChildProofResult;
	FString SnapshotOnlyChildProofFailureReason;
	if (!TestTrue(
		TEXT("Zero-endpoint proof fixture snapshot-only direct child proof succeeds"),
		LayoutProfileSolverInternal::BuildIndependentChildProofResult(
			SnapshotOnlyChildRequest,
			SnapshotOnlyChildProofResult,
			SnapshotOnlyChildProofFailureReason)))
	{
		AddError(SnapshotOnlyChildProofFailureReason);
		return false;
	}
	TestTrue(
		TEXT("Zero-endpoint proof fixture snapshot-only direct child proof keeps the same placements"),
		AreRewritePlacedModuleArraysEquivalent(
			SnapshotOnlyChildProofResult.SolveResult.Placements,
			ExpectedChildResult.SolveResult.Placements));
	TestTrue(
		TEXT("Zero-endpoint proof fixture snapshot-only direct child proof keeps the stepped terrain support map"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			SnapshotOnlyChildProofResult.SteppedTerrainSupportMap,
			DemandResult.ChildRequest.SteppedTerrainSupportMap));

	const FLayoutRegionSolveScheduleResult ScheduleResult =
		ExecuteNegotiatedProofSchedule(SolveContext, ProofContract);
	if (!TestTrue(TEXT("Zero-endpoint proof fixture executes successfully"), ScheduleResult.bSucceeded))
	{
		AddError(ScheduleResult.FailureReason);
		return false;
	}

	TestTrue(
		TEXT("Zero-endpoint proof fixture uses the independent split-proof path"),
		ScheduleResult.bUsedIndependentDeferredProofSplit);
	if (!TestEqual(TEXT("Zero-endpoint proof fixture keeps parent plus child region results"), ScheduleResult.RegionResults.Num(), 2))
	{
		return false;
	}

	TestEqual(
		TEXT("Zero-endpoint proof fixture keeps the parent result first"),
		ScheduleResult.RegionResults[0].RegionDebugPath,
		RootRequest.RegionDebugPath);
	TestEqual(
		TEXT("Zero-endpoint proof fixture keeps the child result second"),
		ScheduleResult.RegionResults[1].RegionDebugPath,
		DemandResult.ChildRegionDebugPath);
	TestEqual(
		TEXT("Zero-endpoint proof fixture preserves the child source content-entry id"),
		ScheduleResult.RegionResults[1].SourceContentEntryId,
		DemandResult.ChildRequest.SourceContentEntryId);
	TestTrue(
		TEXT("Zero-endpoint proof fixture keeps the direct child proof placements on the child result"),
		AreRewritePlacedModuleArraysEquivalent(
			ScheduleResult.RegionResults[1].SolveResult.Placements,
			ExpectedChildResult.SolveResult.Placements));
	TestTrue(
		TEXT("Zero-endpoint proof fixture keeps the stepped terrain support map on the child result"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			ScheduleResult.RegionResults[1].SteppedTerrainSupportMap,
			DemandResult.ChildRequest.SteppedTerrainSupportMap));
	return true;
}

// Focused feature test for Phase A: verifies the frozen child boundary handoff carries the
// parent placement's REAL face-rule data (ConnectionTag from the parent module face, not a
// synthesized boundary flag), clears bRequiresBoundaryFacing for region-to-region contacts so
// the child can consume the boundary on ordinary inward-facing faces, and marks the contact
// as a filled neighbor. Face-rule continuity is enforced via real ConnectionTag/
// AllowedConnectionTags compatibility carried on the frozen point, not a blanket
// BoundaryRequirement escape. The end-to-end 'child proof succeeds from the frozen
// handoff' outcome (CSP classifies parent-contact faces as IncomingBoundary, not Unplanned)
// is covered by RealizesDirectChildRegions; this test isolates the handoff contract itself.
bool FLayoutRecursiveScheduleRewriteFrozenHandoffCarriesParentFaceDataTest::RunTest(const FString& Parameters)
{
	UObject* FixtureOuter = CreateRewriteTestOuter(TEXT("FrozenHandoffFaceDataFixture"));

	// Parent module authored with FaceOpen on every face.
	ULayoutModuleAsset* ParentModule = CreateRewriteOpenFillModule(
		FixtureOuter,
		TEXT("FrozenHandoffParentShell"),
		{ELayoutCellIntent::Boundary},
		MakeTags({LayoutGameplayTags::TraversalPrimary}));

	FLayoutRegionContentEntry ParentEntry;
	ParentEntry.EntryId = TEXT("ParentShell");
	ParentEntry.ContentKind = ELayoutRegionContentKind::Module;
	ParentEntry.ModuleSettings.Module = ParentModule;

	ULayoutRegionContentSetAsset* ParentContentSet = CreateRegionContentSet(
		FixtureOuter,
		TEXT("FrozenHandoffParentContentSet"),
		{ParentEntry});
	ULayoutProfileAsset* ParentProfile = CreateProfile(
		FixtureOuter,
		TEXT("FrozenHandoffParentProfile"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	ParentProfile->ContentSet = ParentContentSet;

	// Solve the parent so we get a result with fully-initialized placements (face rules
	// carried on the module, not hand-constructed). The parent is 2x1x1; the solver will
	// place a module at each cell.
	const FLayoutRegionSolveRequest ParentRequest =
		FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ParentContentSet,
			ParentProfile,
			7001,
			TEXT("Parent"));
	const FLayoutRegionSolveResult ParentProofResult =
		FLayoutProfileSolver::SolveRegion(ParentRequest);
	if (!TestTrue(TEXT("Frozen-handoff parent proof succeeds"), ParentProofResult.SolveResult.bSucceeded))
	{
		AddError(ParentProofResult.SolveResult.FailureReason);
		return false;
	}

	FLayoutDirectChildRegionCommitment DirectChildCommitment;
	DirectChildCommitment.ParentRegionDebugPath = TEXT("Parent");
	DirectChildCommitment.ChildRegionDebugPath = TEXT("Child");

	// Place the child's single planned cell at (2,0,0). The parent is 2 cells: (0,0,0)
	// and (1,0,0). The child cell's NegX neighbor is (1,0,0), which has a parent
	// placement. The freeze should produce a filled-neighbor boundary point carrying the
	// parent's FaceOpen ConnectionTag.
	FLayoutRegionSolveRequest ChildProofRequest;
	ChildProofRequest.RegionDebugPath = TEXT("Child");
	ChildProofRequest.RegionCellOffset = FIntVector::ZeroValue;
	FLayoutPlannedCell& ChildPlannedCell = ChildProofRequest.PlannedCells.AddDefaulted_GetRef();
	ChildPlannedCell.Cell = FIntVector(2, 0, 0);
	ChildPlannedCell.Intent = ELayoutCellIntent::Boundary;

	const TArray<FLayoutSolveBoundaryPoint> FrozenBoundaryPoints =
		LayoutProfileSolverInternal::FreezeChildIncomingBoundaryPointsFromParentResult(
			ParentProofResult,
			DirectChildCommitment,
			ChildProofRequest);
	// One contact face (NegX touches parent cell (1,0,0)). The other 5 faces see exterior
	// (no parent placement).
	if (!TestEqual(TEXT("Frozen handoff emits one contact + five exterior boundary points"), FrozenBoundaryPoints.Num(), 6))
	{
		return false;
	}

	// Find the filled-neighbor boundary point (parent at (1,0,0), child NegX).
	const FLayoutSolveBoundaryPoint* ContactPoint = FrozenBoundaryPoints.FindByPredicate(
		[](const FLayoutSolveBoundaryPoint& P) { return P.bRepresentsFilledNeighbor; });
	TestNotNull(TEXT("Frozen handoff includes one filled-neighbor boundary point for the parent contact"), ContactPoint);
	if (ContactPoint != nullptr)
	{
		// Real face data: the parent placement's actual ConnectionTag (FaceOpen), not a synthesized flag.
		TestTrue(TEXT("Frozen boundary point carries the parent placement's real FaceOpen ConnectionTag"), ContactPoint->ConnectionTag == LayoutGameplayTags::FaceOpen);
		TestTrue(TEXT("Frozen boundary point carries the parent placement's real AllowedConnectionTags"), ContactPoint->AllowedConnectionTags.HasTagExact(LayoutGameplayTags::FaceOpen));
		// Region-to-region contacts clear bRequiresBoundaryFacing so the child consumes the boundary
		// on ordinary inward-facing faces (no BoundaryRequirement requirement).
		TestFalse(TEXT("Frozen region-to-region boundary point clears bRequiresBoundaryFacing"), ContactPoint->bRequiresBoundaryFacing);
		TestTrue(TEXT("No parent ascent preserves actual contact traversal"), ContactPoint->ConnectedTraversalChannels.HasTagExact(LayoutGameplayTags::TraversalPrimary));
	}
	DirectChildCommitment.NegotiatedResponsibilityContract.RequiredHostProviderCount = 1;
	const auto DeferredPoints = LayoutProfileSolverInternal::FreezeChildIncomingBoundaryPointsFromParentResult(
		ParentProofResult, DirectChildCommitment, ChildProofRequest);
	const auto* DeferredContact = DeferredPoints.FindByPredicate([](const auto& Point) { return Point.bRepresentsFilledNeighbor; });
	if (TestNotNull(TEXT("Parent-ascent handoff retains contact"), DeferredContact))
	{
		TestTrue(TEXT("Actual parent ascent retains traversal deferral"), DeferredContact->ConnectedTraversalChannels.IsEmpty());
	}
	return true;
}

bool FLayoutRecursiveScheduleRewriteExecuteNegotiatedProofScheduleMatchesSnapshotOnlyPseudoAsyncSupportableMixedRootSteppedCarriersTest::RunTest(
	const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	PopulateRewriteRouteConstraint(
		RootRequest.RequiredRouteConstraints.AddDefaulted_GetRef(),
		TEXT("RootSteppedSupportRoute"),
		FIntVector(1, 0, 0),
		ELayoutCellIntent::Interior);
	FLayoutSteppedTerrainSupportSample& FirstRootSupportSample =
		RootRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
	FirstRootSupportSample.LocalCell = FIntVector(0, 0, 0);
	FirstRootSupportSample.SupportSurfaceZ = 64;
	FLayoutSteppedTerrainSupportSample& SecondRootSupportSample =
		RootRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
	SecondRootSupportSample.LocalCell = FIntVector(1, 0, 0);
	SecondRootSupportSample.SupportSurfaceZ = 80;
	FLayoutSteppedTerrainAdjacencyStep& RootAdjacencyStep =
		RootRequest.SteppedTerrainSupportMap.AdjacencySteps.AddDefaulted_GetRef();
	RootAdjacencyStep.FromCell = FIntVector(0, 0, 0);
	RootAdjacencyStep.ToCell = FIntVector(1, 0, 0);
	RootAdjacencyStep.StepHeightBlocks = 16;
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 16;
	FLayoutForcedPlacementBundleInsertion& RootInsertion =
		RootRequest.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
	RootInsertion.BundleId = BuildRewriteRequestPlacementBundleId(RootRequest, 0);
	RootInsertion.AnchorCell = FIntVector(0, 0, 0);
	RootInsertion.ProvingCell = FIntVector(0, 0, 0);

	const FCapabilityBackedScheduleResult SourceBackedCoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Source-backed supportable mixed parent/root stepped-carrier fixture executes successfully"),
		SourceBackedCoordinatorResult.bSucceeded))
	{
		AddError(SourceBackedCoordinatorResult.FailureReason);
		return false;
	}

	FRecursiveScheduleSolveContext SnapshotOnlySolveContext =
		BuildSolveContext(RootRequest);
	FNegotiatedProofScheduleContract SnapshotOnlyProofContract =
		SourceBackedCoordinatorResult.ProofContract;
	StripRewriteLiveSourcesFromProofExecutionInputs(
		SnapshotOnlySolveContext,
		SnapshotOnlyProofContract);

	const FLayoutRegionSolveScheduleResult SnapshotOnlyScheduleResult =
		ExecuteNegotiatedProofSchedule(
			SnapshotOnlySolveContext,
			SnapshotOnlyProofContract);
	if (!TestTrue(
		TEXT("Snapshot-only supportable mixed parent/root stepped-carrier fixture executes successfully"),
		SnapshotOnlyScheduleResult.bSucceeded))
	{
		AddError(SnapshotOnlyScheduleResult.FailureReason);
		return false;
	}

	const FLayoutRegionSolveScheduleResult& SourceBackedScheduleResult =
		SourceBackedCoordinatorResult.ScheduleResult;
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the same region-result count"),
		SnapshotOnlyScheduleResult.RegionResults.Num(),
		SourceBackedScheduleResult.RegionResults.Num());
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the same split-proof flag"),
		SnapshotOnlyScheduleResult.bUsedIndependentDeferredProofSplit,
		SourceBackedScheduleResult.bUsedIndependentDeferredProofSplit);
	TestTrue(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the same merged placement set"),
		AreRewritePlacedModuleArraysEquivalent(
			BuildRewriteTranslatedPlacedModules(
				SnapshotOnlyScheduleResult.MergedSolveResult,
				FIntVector::ZeroValue),
			BuildRewriteTranslatedPlacedModules(
				SourceBackedScheduleResult.MergedSolveResult,
				FIntVector::ZeroValue)));

	const FLayoutRegionSolveResult* SourceParentResult =
		SourceBackedScheduleResult.RegionResults.FindByPredicate(
			[&RootRequest](const FLayoutRegionSolveResult& Result)
			{
				return Result.RegionDebugPath == RootRequest.RegionDebugPath;
			});
	const FLayoutRegionSolveResult* SnapshotParentResult =
		SnapshotOnlyScheduleResult.RegionResults.FindByPredicate(
			[&RootRequest](const FLayoutRegionSolveResult& Result)
			{
				return Result.RegionDebugPath == RootRequest.RegionDebugPath;
			});
	if (!TestNotNull(TEXT("Source-backed supportable mixed parent/root proof keeps the parent region result"), SourceParentResult)
		|| !TestNotNull(TEXT("Snapshot-only supportable mixed parent/root proof keeps the parent region result"), SnapshotParentResult))
	{
		return false;
	}

	TestTrue(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the same parent placements"),
		AreRewritePlacedModuleArraysEquivalent(
			SnapshotParentResult->SolveResult.Placements,
			SourceParentResult->SolveResult.Placements));
	TestTrue(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the same parent/root stepped terrain support map"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			SnapshotParentResult->SteppedTerrainSupportMap,
			SourceParentResult->SteppedTerrainSupportMap));
	TestTrue(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the same parent/root forced placement bundle insertions"),
		AreRewriteForcedPlacementBundleInsertionsEquivalent(
			SnapshotParentResult->ForcedPlacementBundleInsertions,
			SourceParentResult->ForcedPlacementBundleInsertions));
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the same parent/root required route constraint count"),
		SnapshotParentResult->RequiredRouteConstraints.Num(),
		SourceParentResult->RequiredRouteConstraints.Num());
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the same parent/root forced placement bundle insertion count"),
		SnapshotParentResult->ForcedPlacementBundleInsertions.Num(),
		SourceParentResult->ForcedPlacementBundleInsertions.Num());
	if (!TestTrue(
		TEXT("Source-backed supportable mixed parent/root proof keeps at least one parent/root forced placement bundle insertion"),
		SourceParentResult->ForcedPlacementBundleInsertions.Num() > 0)
		|| !TestTrue(
			TEXT("Snapshot-only supportable mixed parent/root proof keeps at least one parent/root forced placement bundle insertion"),
			SnapshotParentResult->ForcedPlacementBundleInsertions.Num() > 0))
	{
		return false;
	}
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the same parent/root forced placement bundle id"),
		SnapshotParentResult->ForcedPlacementBundleInsertions[0].BundleId,
		SourceParentResult->ForcedPlacementBundleInsertions[0].BundleId);
	if (!TestTrue(
		TEXT("Source-backed supportable mixed parent/root proof keeps at least one parent/root required route constraint"),
		SourceParentResult->RequiredRouteConstraints.Num() > 0)
		|| !TestTrue(
			TEXT("Snapshot-only supportable mixed parent/root proof keeps at least one parent/root required route constraint"),
			SnapshotParentResult->RequiredRouteConstraints.Num() > 0))
	{
		return false;
	}
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the same parent/root required route constraint id"),
		SnapshotParentResult->RequiredRouteConstraints[0].ConstraintId,
		SourceParentResult->RequiredRouteConstraints[0].ConstraintId);
	TestTrue(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the root stepped terrain support map on the parent/root result"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			SnapshotParentResult->SteppedTerrainSupportMap,
			RootRequest.SteppedTerrainSupportMap));
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps one root forced placement bundle insertion on the parent/root result"),
		SnapshotParentResult->ForcedPlacementBundleInsertions.Num(),
		1);
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the root forced placement bundle id on the parent/root result"),
		SnapshotParentResult->ForcedPlacementBundleInsertions[0].BundleId,
		RootInsertion.BundleId);
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps one root required route constraint on the parent/root result"),
		SnapshotParentResult->RequiredRouteConstraints.Num(),
		1);
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the root required route constraint id on the parent/root result"),
		SnapshotParentResult->RequiredRouteConstraints[0].ConstraintId,
		FLayoutId(TEXT("RootSteppedSupportRoute")));

	TestTrue(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the same merged root stepped terrain support map"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			SnapshotOnlyScheduleResult.MergedSolveResult.SteppedTerrainSupportMap,
			SourceBackedScheduleResult.MergedSolveResult.SteppedTerrainSupportMap));
	TestTrue(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the same merged root forced placement bundle insertions"),
		AreRewriteForcedPlacementBundleInsertionsEquivalent(
			SnapshotOnlyScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions,
			SourceBackedScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions));
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the same merged root required route constraint count"),
		SnapshotOnlyScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints.Num(),
		SourceBackedScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints.Num());
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the same merged root forced placement bundle insertion count"),
		SnapshotOnlyScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions.Num(),
		SourceBackedScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions.Num());
	if (!TestTrue(
		TEXT("Source-backed supportable mixed parent/root proof keeps at least one merged root forced placement bundle insertion"),
		SourceBackedScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions.Num() > 0)
		|| !TestTrue(
			TEXT("Snapshot-only supportable mixed parent/root proof keeps at least one merged root forced placement bundle insertion"),
			SnapshotOnlyScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions.Num() > 0))
	{
		return false;
	}
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the same merged root forced placement bundle id"),
		SnapshotOnlyScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions[0].BundleId,
		SourceBackedScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions[0].BundleId);
	if (!TestTrue(
		TEXT("Source-backed supportable mixed parent/root proof keeps at least one merged root required route constraint"),
		SourceBackedScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints.Num() > 0)
		|| !TestTrue(
			TEXT("Snapshot-only supportable mixed parent/root proof keeps at least one merged root required route constraint"),
			SnapshotOnlyScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints.Num() > 0))
	{
		return false;
	}
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the same merged root required route constraint id"),
		SnapshotOnlyScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints[0].ConstraintId,
		SourceBackedScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints[0].ConstraintId);
	TestTrue(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the root stepped terrain support map on the merged solve result"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			SnapshotOnlyScheduleResult.MergedSolveResult.SteppedTerrainSupportMap,
			RootRequest.SteppedTerrainSupportMap));
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps one root forced placement bundle insertion on the merged solve result"),
		SnapshotOnlyScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions.Num(),
		1);
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the root forced placement bundle id on the merged solve result"),
		SnapshotOnlyScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions[0].BundleId,
		RootInsertion.BundleId);
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps one root required route constraint on the merged solve result"),
		SnapshotOnlyScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints.Num(),
		1);
	TestEqual(
		TEXT("Snapshot-only supportable mixed parent/root proof keeps the root required route constraint id on the merged solve result"),
		SnapshotOnlyScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints[0].ConstraintId,
		FLayoutId(TEXT("RootSteppedSupportRoute")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteExecuteNegotiatedProofScheduleMatchesSnapshotOnlyPseudoAsyncOptionalDropContractTest::RunTest(
	const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	FNegotiatedDemandResult SuccessfulDemandResult;
	if (!BuildExecutableZeroEndpointProofFixture(
		*this,
		RootRequest,
		SuccessfulDemandResult))
	{
		return false;
	}
	PopulateRewriteSteppedTerrainSupportMap(SuccessfulDemandResult.ChildRequest.SteppedTerrainSupportMap);

	const FNegotiatedDemandResult FailedOptionalDemandResult =
		MakeFailedOptionalDemandResult(
			RootRequest.RegionDebugPath,
			TEXT("DroppedOptionalChild"),
			TEXT("DroppedOptionalEntry"),
			TEXT("DroppedOptionalSnapshot"),
			TEXT("Optional child could not satisfy the negotiated proof contract."));

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract =
		BuildNegotiatedProofScheduleContract(
			SolveContext,
			{FailedOptionalDemandResult, SuccessfulDemandResult});

	const FLayoutRegionSolveScheduleResult SourceBackedScheduleResult =
		ExecuteNegotiatedProofSchedule(SolveContext, ProofContract);
	if (!TestTrue(
		TEXT("Source-backed optional-drop proof fixture executes successfully"),
		SourceBackedScheduleResult.bSucceeded))
	{
		AddError(SourceBackedScheduleResult.FailureReason);
		return false;
	}

	FRecursiveScheduleSolveContext SnapshotOnlySolveContext = SolveContext;
	FNegotiatedProofScheduleContract SnapshotOnlyProofContract = ProofContract;
	StripRewriteLiveSourcesFromProofExecutionInputs(
		SnapshotOnlySolveContext,
		SnapshotOnlyProofContract);

	const FLayoutRegionSolveScheduleResult SnapshotOnlyScheduleResult =
		ExecuteNegotiatedProofSchedule(
			SnapshotOnlySolveContext,
			SnapshotOnlyProofContract);
	if (!TestTrue(
		TEXT("Snapshot-only optional-drop proof fixture executes successfully"),
		SnapshotOnlyScheduleResult.bSucceeded))
	{
		AddError(SnapshotOnlyScheduleResult.FailureReason);
		return false;
	}

	TestEqual(
		TEXT("Snapshot-only optional-drop pseudo-async proof keeps the same region-result count"),
		SnapshotOnlyScheduleResult.RegionResults.Num(),
		SourceBackedScheduleResult.RegionResults.Num());
	if (SnapshotOnlyScheduleResult.RegionResults.Num() < 2
		|| SourceBackedScheduleResult.RegionResults.Num() < 2)
	{
		AddError(TEXT("Optional-drop proof fixture should keep parent plus one proved child region result on both source-backed and snapshot-only execution paths."));
		return false;
	}
	TestEqual(
		TEXT("Snapshot-only optional-drop pseudo-async proof keeps the same split-proof flag"),
		SnapshotOnlyScheduleResult.bUsedIndependentDeferredProofSplit,
		SourceBackedScheduleResult.bUsedIndependentDeferredProofSplit);
	TestTrue(
		TEXT("Snapshot-only optional-drop pseudo-async proof keeps the same merged placement set"),
		AreRewritePlacedModuleArraysEquivalent(
			BuildRewriteTranslatedPlacedModules(
				SnapshotOnlyScheduleResult.MergedSolveResult,
				FIntVector::ZeroValue),
			BuildRewriteTranslatedPlacedModules(
				SourceBackedScheduleResult.MergedSolveResult,
				FIntVector::ZeroValue)));
	TestEqual(
		TEXT("Snapshot-only optional-drop pseudo-async proof keeps the same merged optional-drop record count"),
		SnapshotOnlyScheduleResult.MergedSolveResult.DroppedOptionalChildren.Num(),
		SourceBackedScheduleResult.MergedSolveResult.DroppedOptionalChildren.Num());
	if (SnapshotOnlyScheduleResult.MergedSolveResult.DroppedOptionalChildren.Num() < 1
		|| SourceBackedScheduleResult.MergedSolveResult.DroppedOptionalChildren.Num() < 1)
	{
		AddError(TEXT("Optional-drop proof fixture should keep one merged optional-drop record on both source-backed and snapshot-only execution paths."));
		return false;
	}
	TestEqual(
		TEXT("Snapshot-only optional-drop pseudo-async proof keeps the same optional-drop decision id"),
		SnapshotOnlyScheduleResult.MergedSolveResult.DroppedOptionalChildren[0].DropDecisionId,
		SourceBackedScheduleResult.MergedSolveResult.DroppedOptionalChildren[0].DropDecisionId);
	TestEqual(
		TEXT("Snapshot-only optional-drop pseudo-async proof keeps the same optional-drop child region path"),
		SnapshotOnlyScheduleResult.MergedSolveResult.DroppedOptionalChildren[0].ChildRegionDebugPath,
		SourceBackedScheduleResult.MergedSolveResult.DroppedOptionalChildren[0].ChildRegionDebugPath);
	TestEqual(
		TEXT("Snapshot-only optional-drop pseudo-async proof keeps the same optional-drop source entry id"),
		SnapshotOnlyScheduleResult.MergedSolveResult.DroppedOptionalChildren[0].SourceContentEntryId,
		SourceBackedScheduleResult.MergedSolveResult.DroppedOptionalChildren[0].SourceContentEntryId);
	TestTrue(
		TEXT("Snapshot-only optional-drop pseudo-async proof keeps the same parent proof placements"),
		AreRewritePlacedModuleArraysEquivalent(
			SnapshotOnlyScheduleResult.RegionResults[0].SolveResult.Placements,
			SourceBackedScheduleResult.RegionResults[0].SolveResult.Placements));
	TestEqual(
		TEXT("Snapshot-only optional-drop pseudo-async proof keeps the same child source-entry id"),
		SnapshotOnlyScheduleResult.RegionResults[1].SourceContentEntryId,
		SourceBackedScheduleResult.RegionResults[1].SourceContentEntryId);
	TestTrue(
		TEXT("Snapshot-only optional-drop pseudo-async proof keeps the same child proof placements"),
		AreRewritePlacedModuleArraysEquivalent(
			SnapshotOnlyScheduleResult.RegionResults[1].SolveResult.Placements,
			SourceBackedScheduleResult.RegionResults[1].SolveResult.Placements));
	TestTrue(
		TEXT("Snapshot-only optional-drop pseudo-async proof keeps the same child stepped terrain support map"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			SnapshotOnlyScheduleResult.RegionResults[1].SteppedTerrainSupportMap,
			SourceBackedScheduleResult.RegionResults[1].SteppedTerrainSupportMap));
	TestTrue(
		TEXT("Snapshot-only optional-drop pseudo-async proof keeps the surviving child stepped terrain support map"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			SnapshotOnlyScheduleResult.RegionResults[1].SteppedTerrainSupportMap,
			SuccessfulDemandResult.ChildRequest.SteppedTerrainSupportMap));
	return true;
}

bool FLayoutRecursiveScheduleRewriteExecuteNegotiatedProofScheduleUsesIndependentOptionalDropContractTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	FNegotiatedDemandResult SuccessfulDemandResult;
	if (!BuildExecutableZeroEndpointProofFixture(
		*this,
		RootRequest,
		SuccessfulDemandResult))
	{
		return false;
	}

	const FNegotiatedDemandResult FailedOptionalDemandResult =
		MakeFailedOptionalDemandResult(
			RootRequest.RegionDebugPath,
			TEXT("DroppedOptionalChild"),
			TEXT("DroppedOptionalEntry"),
			TEXT("DroppedOptionalSnapshot"),
			TEXT("Optional child could not satisfy the negotiated proof contract."));

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract =
		BuildNegotiatedProofScheduleContract(
			SolveContext,
			{FailedOptionalDemandResult, SuccessfulDemandResult});
	if (!TestEqual(TEXT("Optional-drop proof fixture keeps one successful child proof request"), ProofContract.ChildRequests.Num(), 1))
	{
		return false;
	}
	if (!TestEqual(TEXT("Optional-drop proof fixture keeps one frozen optional-drop record"), ProofContract.OptionalChildPlan.DroppedOptionalChildren.Num(), 1))
	{
		return false;
	}

	TestTrue(
		TEXT("Optional-drop proof fixture keeps the surviving child stepped terrain support map on the child proof request"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			ProofContract.ChildRequests[0].SteppedTerrainSupportMap,
			SuccessfulDemandResult.ChildRequest.SteppedTerrainSupportMap));

	const FLayoutDroppedOptionalChildRecord& ExpectedDropRecord =
		ProofContract.OptionalChildPlan.DroppedOptionalChildren[0];
	const FLayoutRegionSolveScheduleResult ScheduleResult =
		ExecuteNegotiatedProofSchedule(SolveContext, ProofContract);
	if (!TestTrue(TEXT("Optional-drop proof fixture executes successfully"), ScheduleResult.bSucceeded))
	{
		AddError(ScheduleResult.FailureReason);
		return false;
	}

	TestTrue(
		TEXT("Optional-drop proof fixture uses the rewrite-owned independent schedule-result path"),
		ScheduleResult.bUsedIndependentDeferredProofSplit);
	if (!TestEqual(TEXT("Optional-drop proof fixture keeps parent plus one proved child region result"), ScheduleResult.RegionResults.Num(), 2))
	{
		return false;
	}
	if (!TestEqual(TEXT("Optional-drop proof fixture keeps one merged optional-drop record"), ScheduleResult.MergedSolveResult.DroppedOptionalChildren.Num(), 1))
	{
		return false;
	}

	const FLayoutDroppedOptionalChildRecord& ActualDropRecord =
		ScheduleResult.MergedSolveResult.DroppedOptionalChildren[0];
	TestEqual(
		TEXT("Optional-drop proof fixture preserves the frozen optional-drop decision id"),
		ActualDropRecord.DropDecisionId,
		ExpectedDropRecord.DropDecisionId);
	TestEqual(
		TEXT("Optional-drop proof fixture preserves the frozen optional-drop child region path"),
		ActualDropRecord.ChildRegionDebugPath,
		ExpectedDropRecord.ChildRegionDebugPath);
	TestEqual(
		TEXT("Optional-drop proof fixture preserves the frozen optional-drop source entry id"),
		ActualDropRecord.SourceContentEntryId,
		ExpectedDropRecord.SourceContentEntryId);
	const FLayoutRegionSolveResult* SuccessfulChildResult =
		ScheduleResult.RegionResults.FindByPredicate(
			[&SuccessfulDemandResult](const FLayoutRegionSolveResult& Result)
			{
				return Result.RegionDebugPath == SuccessfulDemandResult.ChildRequest.RegionDebugPath;
			});
	if (!TestNotNull(TEXT("Optional-drop proof fixture publishes the surviving proved child result"), SuccessfulChildResult))
	{
		return false;
	}
	TestTrue(
		TEXT("Optional-drop proof fixture keeps the surviving child stepped terrain support map on the child result"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			SuccessfulChildResult->SteppedTerrainSupportMap,
			SuccessfulDemandResult.ChildRequest.SteppedTerrainSupportMap));
	TestEqual(
		TEXT("Optional-drop proof fixture preserves the frozen dropped planned-cell count"),
		ActualDropRecord.DroppedPlannedCells.Num(),
		ExpectedDropRecord.DroppedPlannedCells.Num());
	if (!ExpectedDropRecord.DroppedPlannedCells.IsEmpty()
		&& !ActualDropRecord.DroppedPlannedCells.IsEmpty())
	{
		TestEqual(
			TEXT("Optional-drop proof fixture preserves the frozen dropped planned cell"),
			ActualDropRecord.DroppedPlannedCells[0].Cell,
			ExpectedDropRecord.DroppedPlannedCells[0].Cell);
		TestEqual(
			TEXT("Optional-drop proof fixture preserves the frozen dropped planned intent"),
			ActualDropRecord.DroppedPlannedCells[0].Intent,
			ExpectedDropRecord.DroppedPlannedCells[0].Intent);
	}
	TestFalse(
		TEXT("Optional-drop proof fixture does not add a proved region result for the dropped optional child"),
		ScheduleResult.RegionResults.ContainsByPredicate(
			[&ExpectedDropRecord](const FLayoutRegionSolveResult& Result)
			{
				return Result.RegionDebugPath == ExpectedDropRecord.ChildRegionDebugPath;
			}));
	return true;
}

bool FLayoutRecursiveScheduleRewriteCoordinatorBuildsSupportableMixedRequiredPlacementBackedDemandResultsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	TArray<FNegotiatedDemandResult> BridgedDemandResults;
	FString BridgeFailureReason;
	if (!TestTrue(
		TEXT("Coordinator required-demand bridge succeeds for the mixed child fixture once sibling seam intents are authored"),
		TryBuildRequiredPlacementBackedDemandResults(
			RootRequest,
			BridgedDemandResults,
			nullptr,
			BridgeFailureReason)))
	{
		AddError(BridgeFailureReason);
		return false;
	}

	if (!TestEqual(
		TEXT("Supportable mixed required-demand bridge keeps two child demand results"),
		BridgedDemandResults.Num(),
		2))
	{
		return false;
	}

	const FNegotiatedDemandResult* CommittedChildResult =
		BridgedDemandResults.FindByPredicate(
			[](const FNegotiatedDemandResult& Result)
			{
				return Result.ChildRequest.SourceContentEntryId == FLayoutId(TEXT("ChildRoom"));
			});
	const FNegotiatedDemandResult* ZeroEndpointChildResult =
		BridgedDemandResults.FindByPredicate(
			[](const FNegotiatedDemandResult& Result)
			{
				return Result.ChildRequest.SourceContentEntryId == FLayoutId(TEXT("IndependentChild"));
			});
	if (!TestNotNull(
		TEXT("Supportable mixed required-demand bridge keeps the committed child"),
		CommittedChildResult)
		|| !TestNotNull(
			TEXT("Supportable mixed required-demand bridge keeps the zero-endpoint sibling"),
			ZeroEndpointChildResult))
	{
		return false;
	}

	TestTrue(
		TEXT("Supportable mixed required-demand bridge keeps the committed child result successful"),
		CommittedChildResult->bSucceeded);
	TestTrue(
		TEXT("Supportable mixed required-demand bridge keeps the zero-endpoint sibling result successful"),
		ZeroEndpointChildResult->bSucceeded);
	return true;
}

bool FLayoutRecursiveScheduleRewritePlacementBridgePreservesProtectedStructuralCellsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	RootRequest.ProtectedStructuralCells = {
		FIntVector(9, 0, 0),
		FIntVector(7, 0, 0)
	};

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Coordinator placement-bridge path succeeds while preserving protected structural cells"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	if (!TestEqual(
		TEXT("Placement bridge keeps both protected structural cells on the parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.ProtectedStructuralCells.Num(),
		2))
	{
		return false;
	}

	TestEqual(
		TEXT("Placement bridge keeps the lexicographically earlier protected structural cell first"),
		CoordinatorResult.ProofContract.ParentRequest.ProtectedStructuralCells[0],
		FIntVector(7, 0, 0));
	TestEqual(
		TEXT("Placement bridge keeps the lexicographically later protected structural cell second"),
		CoordinatorResult.ProofContract.ParentRequest.ProtectedStructuralCells[1],
		FIntVector(9, 0, 0));
	return true;
}

bool FLayoutRecursiveScheduleRewritePlacementBridgePreservesRootExternalEndpointCommitmentsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	RootRequest.CommittedEndpointAnchors = {
		MakeEndpointAnchor(TEXT("RootExit"), FIntVector(4, 1, 0), ELayoutFaceDirection::PosX)
	};
	RootRequest.CommittedEndpointAnchors[0].ConnectionTag = LayoutGameplayTags::FaceOpen;
	RootRequest.CommittedEndpointAnchors[0].AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
	RootRequest.CommittedEndpointAnchors[0].TraversalChannels.Reset();

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Coordinator placement-bridge path succeeds while preserving root external endpoint commitments"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	if (!TestEqual(
		TEXT("Placement bridge keeps one root external endpoint commitment on the parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.CommittedEndpointAnchors.Num(),
		1))
	{
		return false;
	}

	TestEqual(
		TEXT("Placement bridge keeps the root external endpoint commitment id"),
		CoordinatorResult.ProofContract.ParentRequest.CommittedEndpointAnchors[0].CommitmentId,
		FLayoutId(TEXT("RootExit")));
	TestEqual(
		TEXT("Placement bridge keeps the root external endpoint commitment cell"),
		CoordinatorResult.ProofContract.ParentRequest.CommittedEndpointAnchors[0].LocalCell,
		FIntVector(4, 1, 0));
	TestEqual(
		TEXT("Placement bridge keeps the root external endpoint commitment face"),
		CoordinatorResult.ProofContract.ParentRequest.CommittedEndpointAnchors[0].FaceDirection,
		ELayoutFaceDirection::PosX);
	TestEqual(
		TEXT("Proof contract still keeps one root external endpoint commitment on the root external endpoint carrier"),
		CoordinatorResult.ProofContract.RootExternalEndpointCommitments.Num(),
		1);
	TestEqual(
		TEXT("Root external endpoint carrier keeps the same commitment id"),
		CoordinatorResult.ProofContract.RootExternalEndpointCommitments[0].CommitmentId,
		FLayoutId(TEXT("RootExit")));
	return true;
}

bool FLayoutRecursiveScheduleRewritePlacementBridgeBuildPreservesCommittedTraversalAnchorsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	RootRequest.CommittedTraversalAnchors = {
		MakeTraversalAnchor(FIntVector(2, 1, 0))
	};

	TArray<FNegotiatedDemandResult> BridgedDemandResults;
	FLayoutRegionSolveRequest ParentProofRequest;
	FString BridgeFailureReason;
	if (!TestTrue(
		TEXT("Supportable mixed fixture still builds a placement-backed parent proof request while preserving root traversal anchors"),
		TryBuildRequiredPlacementBackedDemandResults(
			RootRequest,
			BridgedDemandResults,
			&ParentProofRequest,
			BridgeFailureReason)))
	{
		AddError(BridgeFailureReason);
		return false;
	}

	TestTrue(
		TEXT("Placement-backed parent proof request keeps the root committed traversal anchor cell"),
		ParentProofRequest.CommittedTraversalAnchors.ContainsByPredicate(
			[](const FLayoutCommittedTraversalAnchor& Anchor)
			{
				return Anchor.Cell == FIntVector(2, 1, 0)
					&& Anchor.TraversalChannel == LayoutGameplayTags::TraversalPrimary;
			}));
	return true;
}

bool FLayoutRecursiveScheduleRewritePlacementBridgePreservesCommittedTraversalAnchorsOnProofContractTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	RootRequest.CommittedTraversalAnchors = {
		MakeTraversalAnchor(FIntVector(2, 1, 0))
	};

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Coordinator placement-bridge path succeeds while preserving committed traversal anchors"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	TestTrue(
		TEXT("Placement bridge keeps the root committed traversal anchor on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.CommittedTraversalAnchors.ContainsByPredicate(
			[](const FLayoutCommittedTraversalAnchor& Anchor)
			{
				return Anchor.Cell == FIntVector(2, 1, 0)
					&& Anchor.TraversalChannel == LayoutGameplayTags::TraversalPrimary;
			}));
	return true;
}

bool FLayoutRecursiveScheduleRewritePlacementBridgePreservesIncomingBoundaryPointsOnProofContractTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	FLayoutSolveBoundaryPoint& RootBoundaryPoint =
		RootRequest.IncomingBoundaryPoints.AddDefaulted_GetRef();
	RootBoundaryPoint.LocalCell = FIntVector(0, 1, 0);
	RootBoundaryPoint.FaceDirection = ELayoutFaceDirection::NegX;
	RootBoundaryPoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	RootBoundaryPoint.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
	RootBoundaryPoint.ConnectedTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
	RootBoundaryPoint.bRepresentsFilledNeighbor = false;
	RootBoundaryPoint.bRequiresBoundaryFacing = true;
	RootBoundaryPoint.SourceRegionDebugPath = TEXT("RootIncomingBoundary");
	RootBoundaryPoint.CommitmentId = FLayoutId(TEXT("RootBoundaryPoint"));

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Coordinator placement-bridge path succeeds while preserving incoming boundary points"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	TestTrue(
		TEXT("Placement bridge keeps the root incoming boundary point on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.IncomingBoundaryPoints.ContainsByPredicate(
			[](const FLayoutSolveBoundaryPoint& BoundaryPoint)
			{
				return BoundaryPoint.LocalCell == FIntVector(0, 1, 0)
					&& BoundaryPoint.FaceDirection == ELayoutFaceDirection::NegX
					&& BoundaryPoint.ConnectionTag == LayoutGameplayTags::FaceOpen
					&& BoundaryPoint.AllowedConnectionTags.HasTagExact(LayoutGameplayTags::FaceOpen)
					&& BoundaryPoint.ConnectedTraversalChannels.HasTagExact(LayoutGameplayTags::TraversalPrimary)
					&& !BoundaryPoint.bRepresentsFilledNeighbor
					&& BoundaryPoint.bRequiresBoundaryFacing
					&& BoundaryPoint.SourceRegionDebugPath == TEXT("RootIncomingBoundary")
					&& BoundaryPoint.CommitmentId == FLayoutId(TEXT("RootBoundaryPoint"));
			}));
	return true;
}

bool FLayoutRecursiveScheduleRewritePlacementBridgePreservesNegotiatedContractDiagnosticsOnProofContractTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Coordinator placement-bridge path succeeds while preserving negotiated contract diagnostics"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	if (!TestTrue(
		TEXT("Placement bridge keeps negotiated child responsibility contracts on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts.Num() > 0))
	{
		return false;
	}

	for (const FLayoutNegotiatedChildResponsibilityContract& ResponsibilityContract :
		CoordinatorResult.ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts)
	{
		const FLayoutId ExpectedContractRecordId(*FString::Printf(
			TEXT("%s.NegotiatedChildResponsibility.%s"),
			*CoordinatorResult.ProofContract.ParentRequest.RegionDebugPath,
			*ResponsibilityContract.ChildRegionDebugPath));

		TestTrue(
			FString::Printf(
				TEXT("Placement bridge keeps the negotiated child responsibility validation assertion for '%s' on the final parent proof request"),
				*ResponsibilityContract.ChildRegionDebugPath),
			CoordinatorResult.ProofContract.ParentRequest.ValidationAssertions.ContainsByPredicate(
				[ExpectedContractRecordId](const FLayoutValidationAssertionRecord& Assertion)
				{
					return Assertion.AssertionId == ExpectedContractRecordId
						&& Assertion.AssertionKind == ELayoutValidationAssertionKind::ChildCommitmentContractValid
						&& Assertion.bPassed;
				}));
		TestTrue(
			FString::Printf(
				TEXT("Placement bridge keeps the negotiated child responsibility validation proof record for '%s' on the final parent proof request"),
				*ResponsibilityContract.ChildRegionDebugPath),
			CoordinatorResult.ProofContract.ParentRequest.ProofRecords.ContainsByPredicate(
				[ExpectedContractRecordId](const FLayoutProofRecord& ProofRecord)
				{
					return ProofRecord.ProofId == ExpectedContractRecordId
						&& ProofRecord.ProofKind == ELayoutProofKind::NormalizedCommitment;
				}));
	}
	return true;
}

bool FLayoutRecursiveScheduleRewritePlacementBridgePreservesRootProofDiagnosticsOnProofContractTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	RootRequest.ProofRecords = {
		MakeRewriteProofRecord(TEXT("RootProof"), TEXT("RootTarget"))
	};
	RootRequest.ValidationAssertions = {
		MakeRewriteValidationAssertion(TEXT("RootAssertion"), true)
	};

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Coordinator placement-bridge path succeeds while preserving root-authored proof diagnostics"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	TestTrue(
		TEXT("Placement bridge keeps the root-authored proof record on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.ProofRecords.ContainsByPredicate(
			[](const FLayoutProofRecord& ProofRecord)
			{
				return ProofRecord.ProofId == FLayoutId(TEXT("RootProof"))
					&& ProofRecord.TargetId == FLayoutId(TEXT("RootTarget"));
			}));
	TestTrue(
		TEXT("Placement bridge keeps the root-authored validation assertion on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.ValidationAssertions.ContainsByPredicate(
			[](const FLayoutValidationAssertionRecord& Assertion)
			{
				return Assertion.AssertionId == FLayoutId(TEXT("RootAssertion"))
					&& Assertion.bPassed;
			}));
	return true;
}

bool FLayoutRecursiveScheduleRewritePlacementBridgePreservesRootPublicationAndWorldBindingMetadataOnProofContractTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	RootRequest.TemplatePlacementZOffsetBlocks = 13;
	RootRequest.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	RootRequest.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 448;
	RootRequest.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 96;
	RootRequest.WorldBindingPlacementPolicy.TerrainSampleGridSpacing = 6;
	RootRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold = 5;
	RootRequest.WorldBindingPlacementPolicy.TerrainTransition.bAllowFoundationFill = true;
	RootRequest.WorldBindingPlacementPolicy.TerrainTransition.MaxFoundationDepth = 4;
	RootRequest.WorldBindingPlacementPolicy.TerrainTransition.bAllowPerimeterRampTransition = true;
	RootRequest.RootSolveId = TEXT("RootSolve");
	RootRequest.RootCandidateId = TEXT("RootCandidate");
	RootRequest.RootPlacementPolicyId = TEXT("RootPlacementPolicy");

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Coordinator placement-bridge path succeeds while preserving root publication and world-binding metadata"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	TestEqual(
		TEXT("Placement bridge keeps the root template placement Z offset on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.TemplatePlacementZOffsetBlocks,
		13);
	TestEqual(
		TEXT("Placement bridge keeps the root placement kind on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.RootPlacementKind,
		ELayoutWorldBindingPlacementKind::OrdinaryRoot);
	TestEqual(
		TEXT("Placement bridge keeps the root world-binding placement kind on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.RootPlacementKind,
		ELayoutWorldBindingPlacementKind::OrdinaryRoot);
	TestEqual(
		TEXT("Placement bridge keeps the root terrain search start on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ,
		448);
	TestEqual(
		TEXT("Placement bridge keeps the root terrain search depth on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks,
		96);
	TestEqual(
		TEXT("Placement bridge keeps the root terrain sample spacing on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.WorldBindingPlacementPolicy.TerrainSampleGridSpacing,
		6);
	TestEqual(
		TEXT("Placement bridge keeps the root terrain height-ignore threshold on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.WorldBindingPlacementPolicy.HeightIgnoreThreshold,
		5);
		TestTrue(
		TEXT("Placement bridge keeps the root foundation-fill policy on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.WorldBindingPlacementPolicy.TerrainTransition.bAllowFoundationFill);
	TestEqual(
		TEXT("Placement bridge keeps the root max foundation depth on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.WorldBindingPlacementPolicy.TerrainTransition.MaxFoundationDepth,
		4);
	TestTrue(
		TEXT("Placement bridge keeps the root perimeter-ramp policy on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.WorldBindingPlacementPolicy.TerrainTransition.bAllowPerimeterRampTransition);
	TestEqual(
		TEXT("Placement bridge keeps the root solve id on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.RootSolveId,
		FLayoutId(TEXT("RootSolve")));
	TestEqual(
		TEXT("Placement bridge keeps the root candidate id on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.RootCandidateId,
		FLayoutId(TEXT("RootCandidate")));
	TestEqual(
		TEXT("Placement bridge keeps the root placement policy id on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.RootPlacementPolicyId,
		FLayoutId(TEXT("RootPlacementPolicy")));
	return true;
}

bool FLayoutRecursiveScheduleRewritePlacementBridgePreservesRootContinuationSelectionAndSourceEntryOnProofContractTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	RootRequest.SourceContentEntryId = TEXT("RootEntry");
	RootRequest.RootContinuationSelection.FamilyId = TEXT("BridgeContinuationFamily");
	RootRequest.RootContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	RootRequest.RootContinuationSelection.ResolvedEntryLevel = 2;

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Coordinator placement-bridge path succeeds while preserving root continuation selection and source entry"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	TestEqual(
		TEXT("Placement bridge keeps the root source content entry id on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.SourceContentEntryId,
		FName(TEXT("RootEntry")));
	TestEqual(
		TEXT("Placement bridge keeps the root continuation family id on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.RootContinuationSelection.FamilyId,
		FName(TEXT("BridgeContinuationFamily")));
	TestEqual(
		TEXT("Placement bridge keeps the root continuation placement kind on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.RootContinuationSelection.PlacementKind,
		ELayoutWorldBindingPlacementKind::BridgeContinuation);
	TestEqual(
		TEXT("Placement bridge keeps the root continuation entry level on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.RootContinuationSelection.ResolvedEntryLevel,
		2);
	return true;
}

bool FLayoutRecursiveScheduleRewritePlacementBridgePreservesRebuiltParentPlanOnProofContractTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	TArray<FNegotiatedDemandResult> BridgedDemandResults;
	FLayoutRegionSolveRequest PlacementBackedParentProofRequest;
	FString BridgeFailureReason;
	if (!TestTrue(
		TEXT("Supportable mixed fixture still builds a placement-backed parent proof request while preserving the rebuilt parent plan"),
		TryBuildRequiredPlacementBackedDemandResults(
			RootRequest,
			BridgedDemandResults,
			&PlacementBackedParentProofRequest,
			BridgeFailureReason)))
	{
		AddError(BridgeFailureReason);
		return false;
	}

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Coordinator placement-bridge path succeeds while preserving the rebuilt parent plan"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	TestEqual(
		TEXT("Placement bridge keeps the rebuilt parent supplied-planned-cells flag on the final parent proof request"),
		!CoordinatorResult.ProofContract.ParentRequest.PlannedCells.IsEmpty(),
		!PlacementBackedParentProofRequest.PlannedCells.IsEmpty());
	TestEqual(
		TEXT("Placement bridge keeps the rebuilt parent footprint on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.FootprintSize,
		PlacementBackedParentProofRequest.FootprintSize);
	TestTrue(
		TEXT("Placement bridge keeps the rebuilt parent planned cells on the final parent proof request"),
		AreRewritePlannedCellArraysEquivalent(
			CoordinatorResult.ProofContract.ParentRequest.PlannedCells,
			PlacementBackedParentProofRequest.PlannedCells));
	return true;
}

bool FLayoutRecursiveScheduleRewritePlacementBridgePreservesRootRequestIdentityOnProofContractTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	RootRequest.RegionDebugPath = TEXT("RootBridgeIdentity");
	RootRequest.EffectiveSnapshotId = TEXT("RootBridgeSnapshot");
	RootRequest.Seed = 9123;

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Coordinator placement-bridge path succeeds while preserving root request identity"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	TestEqual(
		TEXT("Placement bridge keeps the root region debug path on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.RegionDebugPath,
		FString(TEXT("RootBridgeIdentity")));
	TestEqual(
		TEXT("Placement bridge keeps the root effective snapshot id on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.EffectiveSnapshotId,
		FLayoutId(TEXT("RootBridgeSnapshot")));
	TestEqual(
		TEXT("Placement bridge keeps the root solve seed on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.Seed,
		9123);
	return true;
}

bool FLayoutRecursiveScheduleRewritePlacementBridgePreservesRootSnapshotSchemaVersionOnProofContractTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	RootRequest.SnapshotSchemaVersion = 77;

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Coordinator placement-bridge path succeeds while preserving the root snapshot schema version"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	TestEqual(
		TEXT("Placement bridge keeps the root snapshot schema version on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.SnapshotSchemaVersion,
		77);
	return true;
}

bool FLayoutRecursiveScheduleRewritePlacementBridgePreservesRootDeferredValidationFlagsOnProofContractTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	RootRequest.bDeferClosureValidationToSchedule = true;
	RootRequest.bDeferTraversalValidationToSchedule = true;

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Coordinator placement-bridge path succeeds while preserving the root deferred validation flags"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	TestTrue(
		TEXT("Placement bridge keeps the root deferred closure-validation flag on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.bDeferClosureValidationToSchedule);
	TestTrue(
		TEXT("Placement bridge keeps the root deferred traversal-validation flag on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.bDeferTraversalValidationToSchedule);
	return true;
}

bool FLayoutRecursiveScheduleRewritePlacementBridgePreservesRootSteppedTerrainSupportMapOnProofContractTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	PopulateRewriteRouteConstraint(
		RootRequest.RequiredRouteConstraints.AddDefaulted_GetRef(),
		TEXT("RootSteppedSupportRoute"),
		FIntVector(1, 0, 0),
		ELayoutCellIntent::Interior);
	FLayoutSteppedTerrainSupportSample& FirstSupportSample =
		RootRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
	FirstSupportSample.LocalCell = FIntVector(0, 0, 0);
	FirstSupportSample.SupportSurfaceZ = 64;
	FLayoutSteppedTerrainSupportSample& SecondSupportSample =
		RootRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
	SecondSupportSample.LocalCell = FIntVector(1, 0, 0);
	SecondSupportSample.SupportSurfaceZ = 80;
	FLayoutSteppedTerrainAdjacencyStep& AdjacencyStep =
		RootRequest.SteppedTerrainSupportMap.AdjacencySteps.AddDefaulted_GetRef();
	AdjacencyStep.FromCell = FIntVector(0, 0, 0);
	AdjacencyStep.ToCell = FIntVector(1, 0, 0);
	AdjacencyStep.StepHeightBlocks = 16;
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 16;

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Coordinator placement-bridge path succeeds while preserving the root stepped terrain support map"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	TestEqual(
		TEXT("Placement bridge keeps the root stepped support shared cell height on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks,
		16);
	TestEqual(
		TEXT("Placement bridge keeps one root required route constraint on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.RequiredRouteConstraints.Num(),
		1);
	TestEqual(
		TEXT("Placement bridge keeps the root required route constraint id on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.RequiredRouteConstraints[0].ConstraintId,
		FLayoutId(TEXT("RootSteppedSupportRoute")));
	TestEqual(
		TEXT("Placement bridge keeps the root required route constraint cell on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.RequiredRouteConstraints[0].Cell,
		FIntVector(1, 0, 0));
	TestTrue(
		TEXT("Placement bridge keeps the root required route traversal channel on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.RequiredRouteConstraints[0].FaceRequirements[0].TraversalChannel
			== LayoutGameplayTags::TraversalPrimary);
	TestEqual(
		TEXT("Placement bridge keeps the root stepped support sample count on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.SteppedTerrainSupportMap.SupportSamples.Num(),
		2);
	TestEqual(
		TEXT("Placement bridge keeps the root stepped support adjacency count on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.SteppedTerrainSupportMap.AdjacencySteps.Num(),
		1);
	TestEqual(
		TEXT("Placement bridge keeps the root stepped support maximum observed neighbor delta on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta,
		16);
	TestEqual(
		TEXT("Placement bridge keeps the first root stepped support sample cell on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.SteppedTerrainSupportMap.SupportSamples[0].LocalCell,
		FIntVector(0, 0, 0));
	TestEqual(
		TEXT("Placement bridge keeps the first root stepped support sample height on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.SteppedTerrainSupportMap.SupportSamples[0].SupportSurfaceZ,
		64);
	TestEqual(
		TEXT("Placement bridge keeps the second root stepped support sample cell on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.SteppedTerrainSupportMap.SupportSamples[1].LocalCell,
		FIntVector(1, 0, 0));
	TestEqual(
		TEXT("Placement bridge keeps the second root stepped support sample height on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.SteppedTerrainSupportMap.SupportSamples[1].SupportSurfaceZ,
		80);
	TestEqual(
		TEXT("Placement bridge keeps the root stepped adjacency source cell on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.SteppedTerrainSupportMap.AdjacencySteps[0].FromCell,
		FIntVector(0, 0, 0));
	TestEqual(
		TEXT("Placement bridge keeps the root stepped adjacency destination cell on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.SteppedTerrainSupportMap.AdjacencySteps[0].ToCell,
		FIntVector(1, 0, 0));
	TestEqual(
		TEXT("Placement bridge keeps the root stepped adjacency height on the final parent proof request"),
		CoordinatorResult.ProofContract.ParentRequest.SteppedTerrainSupportMap.AdjacencySteps[0].StepHeightBlocks,
		16);
	return true;
}

bool FLayoutRecursiveScheduleRewritePlacementBridgePreservesFullRootSteppedCarrierSetOnProofContractTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	if (!BuildExecutableSupportableMixedProofFixture(
		*this,
		RootRequest,
		nullptr,
		nullptr))
	{
		return false;
	}

	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	PopulateRewriteRouteConstraint(
		RootRequest.RequiredRouteConstraints.AddDefaulted_GetRef(),
		TEXT("RootSteppedCarrierRoute"),
		FIntVector(1, 0, 0),
		ELayoutCellIntent::Interior);
	FLayoutSteppedTerrainSupportSample& FirstSupportSample =
		RootRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
	FirstSupportSample.LocalCell = FIntVector(0, 0, 0);
	FirstSupportSample.SupportSurfaceZ = 64;
	FLayoutSteppedTerrainSupportSample& SecondSupportSample =
		RootRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
	SecondSupportSample.LocalCell = FIntVector(1, 0, 0);
	SecondSupportSample.SupportSurfaceZ = 80;
	FLayoutSteppedTerrainAdjacencyStep& AdjacencyStep =
		RootRequest.SteppedTerrainSupportMap.AdjacencySteps.AddDefaulted_GetRef();
	AdjacencyStep.FromCell = FIntVector(0, 0, 0);
	AdjacencyStep.ToCell = FIntVector(1, 0, 0);
	AdjacencyStep.StepHeightBlocks = 16;
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 16;
	FLayoutForcedPlacementBundleInsertion& RootInsertion =
		RootRequest.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
	RootInsertion.BundleId = BuildRewriteRequestPlacementBundleId(RootRequest, 0);
	RootInsertion.AnchorCell = FIntVector(0, 0, 0);
	RootInsertion.ProvingCell = FIntVector(0, 0, 0);

	TArray<FNegotiatedDemandResult> BridgedDemandResults;
	FLayoutRegionSolveRequest PlacementBackedParentProofRequest;
	FString BridgeFailureReason;
	if (!TestTrue(
		TEXT("Supportable mixed fixture still builds a placement-backed parent proof request while preserving the full root stepped carrier set"),
		TryBuildRequiredPlacementBackedDemandResults(
			RootRequest,
			BridgedDemandResults,
			&PlacementBackedParentProofRequest,
			BridgeFailureReason)))
	{
		AddError(BridgeFailureReason);
		return false;
	}

	const FCapabilityBackedScheduleResult CoordinatorResult =
		SolveRegionTreeWithCapabilityBackedNegotiation(RootRequest);
	if (!TestTrue(
		TEXT("Coordinator placement-bridge path succeeds while preserving the full root stepped carrier set"),
		CoordinatorResult.bSucceeded))
	{
		AddError(CoordinatorResult.FailureReason);
		return false;
	}

	TestTrue(
		TEXT("Placement bridge keeps the full root stepped terrain support map on the final parent proof request"),
		AreRewriteSteppedTerrainSupportMapsEquivalent(
			CoordinatorResult.ProofContract.ParentRequest.SteppedTerrainSupportMap,
			PlacementBackedParentProofRequest.SteppedTerrainSupportMap));
	TestTrue(
		TEXT("Placement bridge keeps the full root forced placement bundle insertion set on the final parent proof request"),
		AreRewriteForcedPlacementBundleInsertionsEquivalent(
			CoordinatorResult.ProofContract.ParentRequest.ForcedPlacementBundleInsertions,
			PlacementBackedParentProofRequest.ForcedPlacementBundleInsertions));
	TestTrue(
		TEXT("Placement bridge keeps the full root required route constraint set on the final parent proof request"),
		AreRewriteRouteConstraintArraysEquivalent(
			CoordinatorResult.ProofContract.ParentRequest.RequiredRouteConstraints,
			PlacementBackedParentProofRequest.RequiredRouteConstraints));
	return true;
}

bool FLayoutRecursiveScheduleRewritePostStructuralHandoffTest::RunTest(const FString& Parameters)
{
	UObject* TestOuter = CreateRewriteTestOuter(TEXT("RewritePostStructuralHandoff"));
	ULayoutModuleAsset* OpenModule = CreateRewriteOpenFillModule(
		TestOuter,
		TEXT("RewritePostStructuralModule"),
		{ELayoutCellIntent::Interior});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	FLayoutDroppedOptionalChildRecord ExistingDrop;
	ExistingDrop.DropDecisionId = TEXT("ExistingDrop");
	ScheduleResult.MergedSolveResult.DroppedOptionalChildren = {ExistingDrop};
	ScheduleResult.MergedSolveResult.PlannedCells.AddDefaulted_GetRef().Cell =
		FIntVector(66, 66, 66);
	ScheduleResult.MergedSolveResult.Placements.AddDefaulted_GetRef().Cell =
		FIntVector(99, 99, 99);
	ScheduleResult.MergedSolveResult.ExportedEntryCells.Add(FIntVector(55, 55, 55));
	ScheduleResult.MergedSolveResult.CompiledReservations.AddDefaulted_GetRef().Cell =
		FIntVector(44, 44, 44);
	ScheduleResult.MergedSolveResult.RouteConstraints.AddDefaulted_GetRef().Cell =
		FIntVector(33, 33, 33);
	ScheduleResult.MergedSolveResult.ClosureCoverage.AddDefaulted_GetRef().ClosureId =
		TEXT("StaleClosure");
	ScheduleResult.MergedSolveResult.ClosureSegments.AddDefaulted_GetRef().Cell =
		FIntVector(22, 22, 22);
	ScheduleResult.MergedSolveResult.ClosureRuns.AddDefaulted_GetRef().StartCell =
		FIntVector(11, 11, 11);
	ScheduleResult.MergedSolveResult.ResidualUnoccupiedCells.AddDefaulted_GetRef().Cell =
		FIntVector(88, 88, 88);
	ScheduleResult.MergedSolveResult.SparsePlacementCommitments.AddDefaulted_GetRef().Cell =
		FIntVector(77, 77, 77);

	FLayoutRegionSolveResult& ParentResult =
		ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = FIntVector(3, 4, 0);

	FLayoutPlacedModule& ParentPlacement =
		ParentResult.SolveResult.Placements.AddDefaulted_GetRef();
	ParentPlacement.Cell = FIntVector(0, 0, 0);
	ParentPlacement.Intent = ELayoutCellIntent::Interior;
	ParentPlacement.Module = OpenModule;
	ParentPlacement.SourceContentEntryId = TEXT("RootEntry");

	FLayoutPlannedCell& ParentPlannedCell =
		ParentResult.SolveResult.PlannedCells.AddDefaulted_GetRef();
	ParentPlannedCell.Cell = FIntVector(0, 2, 0);
	ParentPlannedCell.Intent = ELayoutCellIntent::Interior;

	ParentResult.SolveResult.ExportedEntryCells.Add(FIntVector(0, 3, 0));

	FLayoutCellReservationRecord& ParentReservation =
		ParentResult.SolveResult.CompiledReservations.AddDefaulted_GetRef();
	ParentReservation.ReservationId = TEXT("RootReservation");
	ParentReservation.Cell = FIntVector(2, 0, 0);
	ParentReservation.Intent = ELayoutCellIntent::Interior;

	FLayoutRouteConstraintRecord& ParentConstraint =
		ParentResult.SolveResult.RouteConstraints.AddDefaulted_GetRef();
	ParentConstraint.ConstraintId = TEXT("RootRoute");
	ParentConstraint.Cell = FIntVector(3, 0, 0);
	ParentConstraint.Intent = ELayoutCellIntent::Interior;
	ParentConstraint.bScoreAsMainRoute = true;
	FLayoutRouteFaceRequirement& FaceRequirement =
		ParentConstraint.FaceRequirements.AddDefaulted_GetRef();
	FaceRequirement.FaceDirection = ELayoutFaceDirection::PosX;
	FaceRequirement.TraversalChannel = LayoutGameplayTags::TraversalPrimary;

	FLayoutClosureCoverageRecord& ParentClosure =
		ParentResult.SolveResult.ClosureCoverage.AddDefaulted_GetRef();
	ParentClosure.ClosureId = TEXT("RootClosure");
	ParentClosure.RequiredSegmentCount = 1;
	ParentClosure.CoveredSegmentCount = 1;
	ParentClosure.bSatisfied = true;

	FLayoutClosureCoverageSegmentRecord& ParentClosureSegment =
		ParentResult.SolveResult.ClosureSegments.AddDefaulted_GetRef();
	ParentClosureSegment.ClosureId = TEXT("RootClosure");
	ParentClosureSegment.Cell = FIntVector(4, 0, 0);
	ParentClosureSegment.FaceDirection = ELayoutFaceDirection::PosY;
	ParentClosureSegment.bCovered = true;

	FLayoutClosureRunRecord& ParentClosureRun =
		ParentResult.SolveResult.ClosureRuns.AddDefaulted_GetRef();
	ParentClosureRun.ClosureId = TEXT("RootClosure");
	ParentClosureRun.StartCell = FIntVector(4, 0, 0);
	ParentClosureRun.EndCell = FIntVector(4, 1, 0);
	ParentClosureRun.FaceDirection = ELayoutFaceDirection::PosY;
	ParentClosureRun.SegmentCount = 2;
	ParentClosureRun.bCovered = true;

	FLayoutSparsePlacementCommitment& ParentSparse =
		ParentResult.SolveResult.SparsePlacementCommitments.AddDefaulted_GetRef();
	ParentSparse.RuleId = TEXT("RootSparse");
	ParentSparse.SourceContentEntryId = TEXT("RootEntry");
	ParentSparse.SourceRegionDebugPath = TEXT("Root");
	ParentSparse.Cell = FIntVector(1, 0, 0);
	ParentSparse.Intent = ELayoutCellIntent::Interior;
	ParentSparse.Module = OpenModule;

	FLayoutRegionSolveResult& ChildResult =
		ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = FIntVector(7, 0, 0);

	FLayoutPlacedModule& ChildPlacement =
		ChildResult.SolveResult.Placements.AddDefaulted_GetRef();
	ChildPlacement.Cell = FIntVector(0, 1, 0);
	ChildPlacement.Intent = ELayoutCellIntent::Interior;
	ChildPlacement.Module = OpenModule;
	ChildPlacement.SourceContentEntryId = TEXT("ChildEntry");

	FLayoutClosureCoverageRecord& ChildClosure =
		ChildResult.SolveResult.ClosureCoverage.AddDefaulted_GetRef();
	ChildClosure.ClosureId = TEXT("ChildClosure");
	ChildClosure.RequiredSegmentCount = 2;
	ChildClosure.CoveredSegmentCount = 2;
	ChildClosure.bSatisfied = true;

	FLayoutResidualCellRecord& ChildResidual =
		ChildResult.SolveResult.ResidualUnoccupiedCells.AddDefaulted_GetRef();
	ChildResidual.Cell = FIntVector(0, 0, 0);
	ChildResidual.Intent = ELayoutCellIntent::Interior;
	ChildResidual.SourceRegionDebugPath = TEXT("Child");
	ChildResidual.SourceContentEntryId = TEXT("ChildEntry");

	FNegotiatedProofScheduleContract ProofContract;
	ProofContract.OptionalChildPlan.DroppedOptionalChildren = {ExistingDrop};

	FLayoutDroppedOptionalChildRecord NewDrop;
	NewDrop.DropDecisionId = TEXT("NewDrop");
	NewDrop.ChildRegionDebugPath = TEXT("DroppedChild");
	ProofContract.OptionalChildPlan.DroppedOptionalChildren.Add(NewDrop);

	FString PostStructuralFailureReason;
	TestTrue(
		TEXT("Post-structural handoff succeeds when no zone-feature audit fails"),
		ApplyPostStructuralScheduleHandoffs(
			ScheduleResult,
			ProofContract,
			PostStructuralFailureReason));
	TestTrue(
		TEXT("Successful post-structural handoff leaves no failure reason"),
		PostStructuralFailureReason.IsEmpty());

	TestEqual(TEXT("Post-structural handoff preserves the existing merged optional-drop set"), ScheduleResult.MergedSolveResult.DroppedOptionalChildren.Num(), 1);
	TestTrue(TEXT("Post-structural handoff does not mutate merged optional-drop artifacts until the derived rebuild step moves here"), ScheduleResult.MergedSolveResult.DroppedOptionalChildren.ContainsByPredicate(
		[](const FLayoutDroppedOptionalChildRecord& Record)
		{
			return Record.DropDecisionId == TEXT("ExistingDrop");
		}));
	TArray<FLayoutPlacedModule> ExpectedMergedPlacements =
		BuildRewriteTranslatedPlacedModules(
			ParentResult.SolveResult,
			ParentResult.RegionCellOffset);
	ExpectedMergedPlacements.Append(
		BuildRewriteTranslatedPlacedModules(
			ChildResult.SolveResult,
			ChildResult.RegionCellOffset));
	SortRewritePlacedModulesForComparison(ExpectedMergedPlacements);

	TArray<FLayoutPlacedModule> ActualMergedPlacements =
		ScheduleResult.MergedSolveResult.Placements;
	SortRewritePlacedModulesForComparison(ActualMergedPlacements);
	TestTrue(
		TEXT("Post-structural handoff rebuilds merged placements from proved region results"),
		AreRewritePlacedModuleArraysEquivalent(
			ActualMergedPlacements,
			ExpectedMergedPlacements));
	TestEqual(
		TEXT("Post-structural handoff rebuilds merged planned cells from proved region results"),
		ScheduleResult.MergedSolveResult.PlannedCells.Num(),
		1);
	TestEqual(
		TEXT("Post-structural handoff translates planned cells into merged world space"),
		ScheduleResult.MergedSolveResult.PlannedCells[0].Cell,
		FIntVector(3, 6, 0));
	TestEqual(
		TEXT("Post-structural handoff rebuilds merged exported entry cells from proved region results"),
		ScheduleResult.MergedSolveResult.ExportedEntryCells.Num(),
		1);
	TestEqual(
		TEXT("Post-structural handoff translates exported entry cells into merged world space"),
		ScheduleResult.MergedSolveResult.ExportedEntryCells[0],
		FIntVector(3, 7, 0));
	TestEqual(
		TEXT("Post-structural handoff rebuilds merged reservations from proved region results"),
		ScheduleResult.MergedSolveResult.CompiledReservations.Num(),
		1);
	TestEqual(
		TEXT("Post-structural handoff translates reservations into merged world space"),
		ScheduleResult.MergedSolveResult.CompiledReservations[0].Cell,
		FIntVector(5, 4, 0));
	TestEqual(
		TEXT("Post-structural handoff rebuilds merged route constraints from proved region results"),
		ScheduleResult.MergedSolveResult.RouteConstraints.Num(),
		1);
	TestEqual(
		TEXT("Post-structural handoff translates route constraints into merged world space"),
		ScheduleResult.MergedSolveResult.RouteConstraints[0].Cell,
		FIntVector(6, 4, 0));
	TestEqual(
		TEXT("Post-structural handoff rebuilds merged closure summaries from proved region results"),
		ScheduleResult.MergedSolveResult.ClosureCoverage.Num(),
		2);
	TestTrue(
		TEXT("Post-structural handoff keeps parent closure coverage"),
		ScheduleResult.MergedSolveResult.ClosureCoverage.ContainsByPredicate(
			[](const FLayoutClosureCoverageRecord& Record)
			{
				return Record.ClosureId == TEXT("RootClosure")
					&& Record.bSatisfied;
			}));
	TestTrue(
		TEXT("Post-structural handoff keeps child closure coverage"),
		ScheduleResult.MergedSolveResult.ClosureCoverage.ContainsByPredicate(
			[](const FLayoutClosureCoverageRecord& Record)
			{
				return Record.ClosureId == TEXT("ChildClosure")
					&& Record.bSatisfied;
			}));
	TestEqual(
		TEXT("Post-structural handoff rebuilds merged closure segments from proved region results"),
		ScheduleResult.MergedSolveResult.ClosureSegments.Num(),
		1);
	TestEqual(
		TEXT("Post-structural handoff translates closure segments into merged world space"),
		ScheduleResult.MergedSolveResult.ClosureSegments[0].Cell,
		FIntVector(7, 4, 0));
	TestEqual(
		TEXT("Post-structural handoff rebuilds merged closure runs from proved region results"),
		ScheduleResult.MergedSolveResult.ClosureRuns.Num(),
		1);
	TestEqual(
		TEXT("Post-structural handoff translates closure runs into merged world space"),
		ScheduleResult.MergedSolveResult.ClosureRuns[0].StartCell,
		FIntVector(7, 4, 0));
	TestEqual(
		TEXT("Post-structural handoff translates closure run end cells into merged world space"),
		ScheduleResult.MergedSolveResult.ClosureRuns[0].EndCell,
		FIntVector(7, 5, 0));
	TestEqual(
		TEXT("Post-structural handoff rebuilds merged residual cells from proved region results"),
		ScheduleResult.MergedSolveResult.ResidualUnoccupiedCells.Num(),
		1);
	TestEqual(
		TEXT("Post-structural handoff translates residual cells into merged world space"),
		ScheduleResult.MergedSolveResult.ResidualUnoccupiedCells[0].Cell,
		FIntVector(7, 0, 0));
	TestEqual(
		TEXT("Post-structural handoff rebuilds merged sparse-placement commitments from proved region results"),
		ScheduleResult.MergedSolveResult.SparsePlacementCommitments.Num(),
		1);
	TestEqual(
		TEXT("Post-structural handoff translates sparse-placement commitments into merged world space"),
		ScheduleResult.MergedSolveResult.SparsePlacementCommitments[0].Cell,
		FIntVector(4, 4, 0));
	return true;
}

bool FLayoutRecursiveScheduleRewritePostStructuralZoneFeatureAuditTest::RunTest(const FString& Parameters)
{
	UObject* TestOuter = CreateRewriteTestOuter(TEXT("RewritePostStructuralZoneFeatureAudit"));
	ULayoutModuleAsset* OpenModule = CreateRewriteOpenFillModule(
		TestOuter,
		TEXT("RewriteZoneFeatureModule"),
		{ELayoutCellIntent::Interior});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	FLayoutRegionSolveResult& ParentResult =
		ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");

	FLayoutPlacedModule& ParentPlacement =
		ParentResult.SolveResult.Placements.AddDefaulted_GetRef();
	ParentPlacement.Cell = FIntVector(0, 0, 0);
	ParentPlacement.Intent = ELayoutCellIntent::Interior;
	ParentPlacement.Module = OpenModule;
	ParentPlacement.SourceContentEntryId = TEXT("PlainEntry");
	ParentResult.SolveResult.FootprintSize = FIntPoint(1, 1);

	FNegotiatedProofScheduleContract ProofContract;
	ProofContract.ParentRequest.RegionDebugPath = TEXT("Root");
	ProofContract.ParentRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	FLayoutZoneFeatureRequirement& Requirement =
		ProofContract.ParentRequest.ProfileSnapshot.ZoneFeatureRequirements.AddDefaulted_GetRef();
	Requirement.RequirementId = TEXT("RoomRequirement");
	Requirement.RequiredFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
	Requirement.MatchMode = ELayoutZoneFeatureMatchMode::Any;
	Requirement.MinCount = 1;

	FLayoutRegionContentEntrySolveSnapshot& Entry =
		ProofContract.ParentRequest.ContentSetSnapshot.Entries.AddDefaulted_GetRef();
	Entry.EntryId = TEXT("PlainEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;

	FString PostStructuralFailureReason;
	TestFalse(
		TEXT("Post-structural handoff rejects a missing hard zone-feature requirement"),
		ApplyPostStructuralScheduleHandoffs(
			ScheduleResult,
			ProofContract,
			PostStructuralFailureReason));
	TestTrue(
		TEXT("Zone-feature failure cites the missing requirement"),
		PostStructuralFailureReason.Contains(TEXT("RoomRequirement")));
	TestEqual(
		TEXT("Zone-feature audit appends one merged validation message before rebuild"),
		ScheduleResult.MergedSolveResult.Messages.Num(),
		1);
	TestEqual(
		TEXT("Zone-feature audit appends one per-region validation message before rebuild"),
		ParentResult.SolveResult.Messages.Num(),
		1);
	return true;
}

bool FLayoutRecursiveScheduleRewritePostStructuralSparsePlacementTest::RunTest(const FString& Parameters)
{
	UObject* TestOuter = CreateRewriteTestOuter(TEXT("RewritePostStructuralSparse"));
	ULayoutModuleAsset* StructuralModule = CreateRewriteOpenFillModule(
		TestOuter,
		TEXT("RewriteStructuralSparseModule"),
		{ELayoutCellIntent::Interior});
	ULayoutModuleAsset* SparseModule = CreateRewriteOpenFillModule(
		TestOuter,
		TEXT("RewriteSparsePlacementModule"),
		{ELayoutCellIntent::Interior});

	FLayoutRegionContentEntry StructuralEntry;
	StructuralEntry.EntryId = TEXT("StructuralBase");
	StructuralEntry.ContentKind = ELayoutRegionContentKind::Module;
	StructuralEntry.ModuleSettings.Module = StructuralModule;
	ULayoutRegionContentSetAsset* StructuralContentSet = CreateRegionContentSet(
		TestOuter,
		TEXT("RewriteStructuralSparseContentSet"),
		{StructuralEntry});

	FLayoutRegionContentEntry SparseEntry;
	SparseEntry.EntryId = TEXT("ResidualSparseEntry");
	SparseEntry.ContentKind = ELayoutRegionContentKind::Module;
	SparseEntry.ModuleSettings.Module = SparseModule;
	ULayoutRegionContentSetAsset* SparseContentSet = CreateRegionContentSet(
		TestOuter,
		TEXT("RewritePostStructuralSparseRuleContentSet"),
		{SparseEntry});

	ULayoutProfileAsset* ParentProfile = CreateProfile(
		TestOuter,
		TEXT("RewritePostStructuralSparseProfile"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	ParentProfile->SparsePlacementRules.Add(
		MakeRewriteSparseRule(
			TEXT("DroppedChildReuse"),
			SparseContentSet,
			1,
			ELayoutPlacementZone::Any));

	FNegotiatedProofScheduleContract ProofContract;
	ProofContract.ParentRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		StructuralContentSet,
		ParentProfile,
		9021,
		TEXT("Root"));
	ProofContract.OptionalChildPlan.DroppedOptionalChildren.Add(
		MakeRewriteDroppedOptionalChildRecord(
			TEXT("DroppedAnnex"),
			TEXT("Root"),
			TEXT("DroppedChild")));

	FLayoutRegionSolveScheduleResult ScheduleResult;
	FLayoutRegionSolveResult& ParentResult =
		ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.SolveResult.FootprintSize = FIntPoint(2, 1);

	FLayoutPlacedModule& ParentPlacement =
		ParentResult.SolveResult.Placements.AddDefaulted_GetRef();
		ParentPlacement.Cell = FIntVector(0, 0, 0);
		ParentPlacement.Intent = ELayoutCellIntent::Interior;
		ParentPlacement.Module = StructuralModule;
		ParentPlacement.SourceContentEntryId = TEXT("StructuralBase");

	FString PostStructuralFailureReason;
	TestTrue(
		TEXT("Post-structural handoff applies sparse placement from dropped optional-child residuals"),
		ApplyPostStructuralScheduleHandoffs(
			ScheduleResult,
			ProofContract,
			PostStructuralFailureReason));
	TestTrue(
		TEXT("Sparse post-structural handoff leaves no failure reason"),
		PostStructuralFailureReason.IsEmpty());
	TestEqual(
		TEXT("Post-structural sparse handoff consumes the dropped-child residual cell on the parent result"),
		ParentResult.SolveResult.ResidualUnoccupiedCells.Num(),
		0);
	TestEqual(
		TEXT("Post-structural sparse handoff records one parent sparse commitment"),
		ParentResult.SolveResult.SparsePlacementCommitments.Num(),
		1);
	if (!TestEqual(
		TEXT("Post-structural sparse handoff rebuilds one merged sparse commitment"),
		ScheduleResult.MergedSolveResult.SparsePlacementCommitments.Num(),
		1))
	{
		return false;
	}
	const FLayoutSparsePlacementCommitment& Commitment =
		ScheduleResult.MergedSolveResult.SparsePlacementCommitments[0];
	TestEqual(
		TEXT("Post-structural sparse handoff keeps the sparse rule id"),
		Commitment.RuleId,
		FName(TEXT("DroppedChildReuse")));
	TestEqual(
		TEXT("Post-structural sparse handoff reuses the dropped child cell"),
		Commitment.Cell,
		FIntVector(1, 0, 0));
	TestEqual(
		TEXT("Post-structural sparse handoff keeps the parent region path on the sparse commitment"),
		Commitment.SourceRegionDebugPath,
		FString(TEXT("Root")));
	TestEqual(
		TEXT("Post-structural sparse handoff uses the sparse content entry id"),
		Commitment.SourceContentEntryId,
		FName(TEXT("ResidualSparseEntry")));
	TestEqual(
		TEXT("Post-structural sparse handoff aggregates one evaluated sparse rule"),
		ScheduleResult.MergedSolveResult.SparsePlacementStats.RuleCount,
		1);
	TestEqual(
		TEXT("Post-structural sparse handoff aggregates accepted sparse placement count"),
		ScheduleResult.MergedSolveResult.SparsePlacementStats.AcceptedPlacementCount,
		1);
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedAnchorTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8001)
		});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;
	ChildResult.CommittedEndpointAnchors[0].LocalCell = FIntVector(99, 99, 99);

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a changed committed child anchor"), bValid);
	TestTrue(TEXT("Validation failure explains the changed child-anchor set"), FailureReason.Contains(TEXT("child-anchor set")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedJunctionRequirementSetTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8006)
		});
	AppendProofContractJunctionCarrier(ProofContract);

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutOwnedSeamJunctionRequirement WrongRequirement;
	WrongRequirement.JunctionRequirementId = TEXT("WrongJunction");
	WrongRequirement.OwnerRegionDebugPath = TEXT("Child");
	WrongRequirement.PassiveRegionDebugPath = TEXT("OtherChild");
	WrongRequirement.AdjacencyClassId = TEXT("SiblingSharedCorner");
	WrongRequirement.JunctionCell = FIntVector(99, 99, 99);
	WrongRequirement.ContinuingSeamId = TEXT("WrongRun");
	WrongRequirement.BranchSeamId = TEXT("WrongBranch");
	WrongRequirement.ContinuingPassiveRegionDebugPath = TEXT("WrongPassive");
	WrongRequirement.InterfaceFamily = LayoutGameplayTags::InterfacePartitionDoor;
	WrongRequirement.ContinuingOwnerFaceDirection = ELayoutFaceDirection::NegX;
	WrongRequirement.BranchOwnerFaceDirection = ELayoutFaceDirection::NegY;
	ScheduleResult.PlannedJunctionRequirements.Add(WrongRequirement);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a changed planned owner-side junction requirement set"), bValid);
	TestTrue(TEXT("Validation failure explains the changed planned junction requirement set"), FailureReason.Contains(TEXT("junction requirement set")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedAuthoritativeSeamSetTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8007)
		});
	AppendProofContractJunctionCarrier(ProofContract);

	const FLayoutRegionSolveScheduleRequest CompatibilityRequest =
		BuildProofExecutionCompatibilityScheduleRequest(ProofContract);

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);
	ScheduleResult.PlannedJunctionRequirements = CompatibilityRequest.PlannedJunctionRequirements;
	ScheduleResult.MergedSolveResult.PartitionSeams =
		CompatibilityRequest.PlannedPartitionSeams;
	ScheduleResult.MergedSolveResult.PartitionSeams[0].OwnerEndCell = FIntVector(99, 1, 0);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a changed authoritative planned seam set"), bValid);
	TestTrue(TEXT("Validation failure explains the changed authoritative planned seam set"), FailureReason.Contains(TEXT("authoritative planned seam set")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedParentRootEndpointCommitmentSetTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.CommittedEndpointAnchors = {
		MakeEndpointAnchor(TEXT("RootDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX)
	};

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8008)
		});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.CommittedEndpointAnchors[0].LocalCell = FIntVector(99, 0, 0);

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a changed parent/root endpoint commitment set"), bValid);
	TestTrue(TEXT("Validation failure explains the changed parent/root endpoint commitment set"), FailureReason.Contains(TEXT("parent root-endpoint commitment set")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedParentProofRecordSetTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.ProofRecords = {
		MakeRewriteProofRecord(TEXT("RootProof"), TEXT("Root"))
	};

	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8010);
	DemandResult.ChildRequest.ProofRecords = {
		MakeRewriteProofRecord(TEXT("ChildProof"), TEXT("Child"))
	};

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{DemandResult});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;
	ParentResult.ProofRecords[0].ProofSummary = TEXT("Mutated parent proof summary");

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a changed parent proof-record set"), bValid);
	TestTrue(TEXT("Validation failure explains the dropped parent proof-record set"), FailureReason.Contains(TEXT("dropped request-carried parent proof records")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedParentValidationAssertionSetTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.ValidationAssertions = {
		MakeRewriteValidationAssertion(TEXT("RootAssertion"), true)
	};

	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8016);
	DemandResult.ChildRequest.ValidationAssertions = {
		MakeRewriteValidationAssertion(TEXT("ChildAssertion"), true)
	};

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{DemandResult});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;
	ParentResult.ValidationAssertions[0].bPassed = false;
	ParentResult.ValidationAssertions[0].FailureReason = TEXT("Mutated parent assertion");

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a changed parent validation-assertion set"), bValid);
	TestTrue(TEXT("Validation failure explains the dropped parent validation-assertion set"), FailureReason.Contains(TEXT("dropped request-carried parent validation assertions")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedParentSteppedTerrainSupportMapTest::RunTest(const FString& Parameters)
{
	FRecursiveScheduleSolveContext SolveContext;
	FNegotiatedProofScheduleContract ProofContract;
	BuildRewriteParentCarrierValidationFixture(
		SolveContext,
		ProofContract);

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);
	InitializeValidationReadyRewriteParentAndChildResults(
		ScheduleResult,
		ProofContract);
	ScheduleResult.RegionResults[0].SteppedTerrainSupportMap.SupportSamples[0].SupportSurfaceZ += 32;

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a changed parent stepped terrain support map"), bValid);
	TestTrue(TEXT("Validation failure explains the changed parent stepped terrain support map"), FailureReason.Contains(TEXT("parent stepped terrain support map")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedParentForcedPlacementBundleInsertionsTest::RunTest(const FString& Parameters)
{
	FRecursiveScheduleSolveContext SolveContext;
	FNegotiatedProofScheduleContract ProofContract;
	BuildRewriteParentCarrierValidationFixture(
		SolveContext,
		ProofContract);

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);
	InitializeValidationReadyRewriteParentAndChildResults(
		ScheduleResult,
		ProofContract);
	ScheduleResult.RegionResults[0].ForcedPlacementBundleInsertions[0].BundleId =
		TEXT("MutatedParentCarrierBundle");

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects changed parent forced placement bundle insertions"), bValid);
	TestTrue(TEXT("Validation failure explains the changed parent forced placement bundle insertions"), FailureReason.Contains(TEXT("parent forced placement bundle insertions")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedParentRequiredRouteConstraintsTest::RunTest(const FString& Parameters)
{
	FRecursiveScheduleSolveContext SolveContext;
	FNegotiatedProofScheduleContract ProofContract;
	BuildRewriteParentCarrierValidationFixture(
		SolveContext,
		ProofContract);

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);
	InitializeValidationReadyRewriteParentAndChildResults(
		ScheduleResult,
		ProofContract);
	ScheduleResult.RegionResults[0].RequiredRouteConstraints[0].ConstraintId =
		TEXT("MutatedParentCarrierRoute");

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects changed parent required route constraints"), bValid);
	TestTrue(TEXT("Validation failure explains the changed parent required route constraints"), FailureReason.Contains(TEXT("parent required route constraints")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedMergedRootSteppedTerrainSupportMapTest::RunTest(const FString& Parameters)
{
	FRecursiveScheduleSolveContext SolveContext;
	FNegotiatedProofScheduleContract ProofContract;
	BuildRewriteParentCarrierValidationFixture(
		SolveContext,
		ProofContract);

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);
	InitializeValidationReadyRewriteParentAndChildResults(
		ScheduleResult,
		ProofContract);
	ScheduleResult.MergedSolveResult.SteppedTerrainSupportMap.SupportSamples[0].SupportSurfaceZ += 32;

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a changed merged root stepped terrain support map"), bValid);
	TestTrue(TEXT("Validation failure explains the changed merged root stepped terrain support map"), FailureReason.Contains(TEXT("merged root stepped terrain support map")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedMergedRootForcedPlacementBundleInsertionsTest::RunTest(const FString& Parameters)
{
	FRecursiveScheduleSolveContext SolveContext;
	FNegotiatedProofScheduleContract ProofContract;
	BuildRewriteParentCarrierValidationFixture(
		SolveContext,
		ProofContract);

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);
	InitializeValidationReadyRewriteParentAndChildResults(
		ScheduleResult,
		ProofContract);
	ScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions[0].BundleId =
		TEXT("MutatedMergedCarrierBundle");

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects changed merged root forced placement bundle insertions"), bValid);
	TestTrue(TEXT("Validation failure explains the changed merged root forced placement bundle insertions"), FailureReason.Contains(TEXT("merged root forced placement bundle insertions")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedMergedRootRequiredRouteConstraintsTest::RunTest(const FString& Parameters)
{
	FRecursiveScheduleSolveContext SolveContext;
	FNegotiatedProofScheduleContract ProofContract;
	BuildRewriteParentCarrierValidationFixture(
		SolveContext,
		ProofContract);

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);
	InitializeValidationReadyRewriteParentAndChildResults(
		ScheduleResult,
		ProofContract);
	ScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints[0].ConstraintId =
		TEXT("MutatedMergedCarrierRoute");

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects changed merged root required route constraints"), bValid);
	TestTrue(TEXT("Validation failure explains the changed merged root required route constraints"), FailureReason.Contains(TEXT("merged root required route constraints")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedSupportableMixedParentSteppedCarriersTest::RunTest(const FString& Parameters)
{
	FRecursiveScheduleSolveContext SolveContext;
	FNegotiatedProofScheduleContract ProofContract;
	FLayoutRegionSolveScheduleResult BaselineScheduleResult;
	if (!BuildLiveSupportableMixedParentCarrierValidationFixture(
		*this,
		SolveContext,
		ProofContract,
		BaselineScheduleResult))
	{
		return false;
	}

	const FLayoutRegionSolveResult* BaselineParentResult =
		BaselineScheduleResult.RegionResults.FindByPredicate(
			[&ProofContract](const FLayoutRegionSolveResult& Result)
			{
				return Result.RegionDebugPath == ProofContract.ParentRequest.RegionDebugPath;
			});
	if (!TestNotNull(
		TEXT("Supportable mixed validation fixture publishes the parent/root region result"),
		BaselineParentResult))
	{
		return false;
	}

	if (!TestTrue(
		TEXT("Supportable mixed validation fixture keeps at least one parent/root stepped support sample"),
		BaselineParentResult->SteppedTerrainSupportMap.SupportSamples.Num() > 0)
		|| !TestTrue(
			TEXT("Supportable mixed validation fixture keeps at least one parent/root forced placement bundle insertion"),
			BaselineParentResult->ForcedPlacementBundleInsertions.Num() > 0)
		|| !TestTrue(
			TEXT("Supportable mixed validation fixture keeps at least one parent/root required route constraint"),
			BaselineParentResult->RequiredRouteConstraints.Num() > 0))
	{
		return false;
	}

	auto ExpectSupportableMixedParentValidationFailure =
		[this, &SolveContext, &ProofContract, &BaselineScheduleResult](
			const TCHAR* Context,
			const TCHAR* ExpectedFailureText,
			auto&& MutateSchedule) -> bool
	{
		FLayoutRegionSolveScheduleResult MutatedScheduleResult = BaselineScheduleResult;
		MutateSchedule(MutatedScheduleResult);

		FString FailureReason;
		const bool bValid = ValidateNegotiatedScheduleResult(
			SolveContext,
			ProofContract,
			MutatedScheduleResult,
			FailureReason);
		if (!TestFalse(Context, bValid))
		{
			return false;
		}

		return TestTrue(
			*FString::Printf(TEXT("%s reports the correct failure seam"), Context),
			FailureReason.Contains(ExpectedFailureText));
	};

	if (!ExpectSupportableMixedParentValidationFailure(
		TEXT("Validation rejects a changed supportable-mixed parent stepped terrain support map"),
		TEXT("parent stepped terrain support map"),
		[&ProofContract](FLayoutRegionSolveScheduleResult& InOutScheduleResult)
		{
			FLayoutRegionSolveResult* ParentResult =
				InOutScheduleResult.RegionResults.FindByPredicate(
					[&ProofContract](const FLayoutRegionSolveResult& Result)
					{
						return Result.RegionDebugPath == ProofContract.ParentRequest.RegionDebugPath;
					});
			ParentResult->SteppedTerrainSupportMap.SupportSamples[0].SupportSurfaceZ += 32;
		}))
	{
		return false;
	}

	if (!ExpectSupportableMixedParentValidationFailure(
		TEXT("Validation rejects changed supportable-mixed parent forced placement bundle insertions"),
		TEXT("parent forced placement bundle insertions"),
		[&ProofContract](FLayoutRegionSolveScheduleResult& InOutScheduleResult)
		{
			FLayoutRegionSolveResult* ParentResult =
				InOutScheduleResult.RegionResults.FindByPredicate(
					[&ProofContract](const FLayoutRegionSolveResult& Result)
					{
						return Result.RegionDebugPath == ProofContract.ParentRequest.RegionDebugPath;
					});
			ParentResult->ForcedPlacementBundleInsertions[0].BundleId =
				TEXT("MutatedSupportableMixedParentCarrierBundle");
		}))
	{
		return false;
	}

	if (!ExpectSupportableMixedParentValidationFailure(
		TEXT("Validation rejects changed supportable-mixed parent required route constraints"),
		TEXT("parent required route constraints"),
		[&ProofContract](FLayoutRegionSolveScheduleResult& InOutScheduleResult)
		{
			FLayoutRegionSolveResult* ParentResult =
				InOutScheduleResult.RegionResults.FindByPredicate(
					[&ProofContract](const FLayoutRegionSolveResult& Result)
					{
						return Result.RegionDebugPath == ProofContract.ParentRequest.RegionDebugPath;
					});
			ParentResult->RequiredRouteConstraints[0].ConstraintId =
				TEXT("MutatedSupportableMixedParentCarrierRoute");
		}))
	{
		return false;
	}

	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedSupportableMixedMergedRootSteppedCarriersTest::RunTest(const FString& Parameters)
{
	FRecursiveScheduleSolveContext SolveContext;
	FNegotiatedProofScheduleContract ProofContract;
	FLayoutRegionSolveScheduleResult BaselineScheduleResult;
	if (!BuildLiveSupportableMixedParentCarrierValidationFixture(
		*this,
		SolveContext,
		ProofContract,
		BaselineScheduleResult))
	{
		return false;
	}

	if (!TestTrue(
		TEXT("Supportable mixed validation fixture keeps at least one merged-root stepped support sample"),
		BaselineScheduleResult.MergedSolveResult.SteppedTerrainSupportMap.SupportSamples.Num() > 0)
		|| !TestTrue(
			TEXT("Supportable mixed validation fixture keeps at least one merged-root forced placement bundle insertion"),
			BaselineScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions.Num() > 0)
		|| !TestTrue(
			TEXT("Supportable mixed validation fixture keeps at least one merged-root required route constraint"),
			BaselineScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints.Num() > 0))
	{
		return false;
	}

	auto ExpectSupportableMixedMergedValidationFailure =
		[this, &SolveContext, &ProofContract, &BaselineScheduleResult](
			const TCHAR* Context,
			const TCHAR* ExpectedFailureText,
			auto&& MutateSchedule) -> bool
	{
		FLayoutRegionSolveScheduleResult MutatedScheduleResult = BaselineScheduleResult;
		MutateSchedule(MutatedScheduleResult);

		FString FailureReason;
		const bool bValid = ValidateNegotiatedScheduleResult(
			SolveContext,
			ProofContract,
			MutatedScheduleResult,
			FailureReason);
		if (!TestFalse(Context, bValid))
		{
			return false;
		}

		return TestTrue(
			*FString::Printf(TEXT("%s reports the correct failure seam"), Context),
			FailureReason.Contains(ExpectedFailureText));
	};

	if (!ExpectSupportableMixedMergedValidationFailure(
		TEXT("Validation rejects a changed supportable-mixed merged root stepped terrain support map"),
		TEXT("merged root stepped terrain support map"),
		[](FLayoutRegionSolveScheduleResult& InOutScheduleResult)
		{
			InOutScheduleResult.MergedSolveResult.SteppedTerrainSupportMap.SupportSamples[0].SupportSurfaceZ += 32;
		}))
	{
		return false;
	}

	if (!ExpectSupportableMixedMergedValidationFailure(
		TEXT("Validation rejects changed supportable-mixed merged root forced placement bundle insertions"),
		TEXT("merged root forced placement bundle insertions"),
		[](FLayoutRegionSolveScheduleResult& InOutScheduleResult)
		{
			InOutScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions[0].BundleId =
				TEXT("MutatedSupportableMixedMergedCarrierBundle");
		}))
	{
		return false;
	}

	if (!ExpectSupportableMixedMergedValidationFailure(
		TEXT("Validation rejects changed supportable-mixed merged root required route constraints"),
		TEXT("merged root required route constraints"),
		[](FLayoutRegionSolveScheduleResult& InOutScheduleResult)
		{
			InOutScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints[0].ConstraintId =
				TEXT("MutatedSupportableMixedMergedCarrierRoute");
		}))
	{
		return false;
	}

	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedChildProofRecordSetTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.ProofRecords = {
		MakeRewriteProofRecord(TEXT("RootProof"), TEXT("Root"))
	};

	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8017);
	DemandResult.ChildRequest.ProofRecords = {
		MakeRewriteProofRecord(TEXT("ChildProof"), TEXT("Child"))
	};

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{DemandResult});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ProofRecords[0].ProofSummary = TEXT("Mutated child proof summary");
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a changed child proof-record set"), bValid);
	TestTrue(TEXT("Validation failure explains the dropped child proof-record set"), FailureReason.Contains(TEXT("dropped request-carried proof records for region 'Child'")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedChildValidationAssertionSetTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.ValidationAssertions = {
		MakeRewriteValidationAssertion(TEXT("RootAssertion"), true)
	};

	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8011);
	DemandResult.ChildRequest.ValidationAssertions = {
		MakeRewriteValidationAssertion(TEXT("ChildAssertion"), true)
	};

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{DemandResult});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;
	ChildResult.ValidationAssertions[0].bPassed = false;
	ChildResult.ValidationAssertions[0].FailureReason = TEXT("Mutated child assertion");

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a changed child validation-assertion set"), bValid);
	TestTrue(TEXT("Validation failure explains the dropped child validation-assertion set"), FailureReason.Contains(TEXT("dropped request-carried validation assertions for region 'Child'")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedParentRegionCellOffsetTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.RegionCellOffset = FIntVector(4, 5, 6);

	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8012);
	DemandResult.ChildRequest.RegionCellOffset = FIntVector(7, 8, 9);

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{DemandResult});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = FIntVector(99, 5, 6);
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a changed parent region-cell offset"), bValid);
	TestTrue(TEXT("Validation failure explains the changed parent region-cell offset"), FailureReason.Contains(TEXT("parent region-cell offset")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedParentSourceContentEntryIdTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.SourceContentEntryId = TEXT("RootEntry");

	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8014);
	DemandResult.ChildRequest.SourceContentEntryId = TEXT("ChildEntry");
	DemandResult.ChildRequest.RegionCellOffset = FIntVector(7, 8, 9);

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{DemandResult});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = TEXT("WrongRootEntry");
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a changed parent source content-entry id"), bValid);
	TestTrue(TEXT("Validation failure explains the changed parent source content-entry id"), FailureReason.Contains(TEXT("parent source content-entry id")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedChildRegionCellOffsetTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.SourceContentEntryId = TEXT("RootEntry");

	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8015);
	DemandResult.ChildRequest.SourceContentEntryId = TEXT("ChildEntry");
	DemandResult.ChildRequest.RegionCellOffset = FIntVector(7, 8, 9);

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{DemandResult});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = FIntVector(99, 8, 9);
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a changed child region-cell offset"), bValid);
	TestTrue(TEXT("Validation failure explains the changed child region-cell offset"), FailureReason.Contains(TEXT("region-cell offset for region 'Child'")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedChildSourceContentEntryIdTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.SourceContentEntryId = TEXT("RootEntry");

	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8013);
	DemandResult.ChildRequest.SourceContentEntryId = TEXT("ChildEntry");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{DemandResult});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = TEXT("WrongChildEntry");
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a changed child source content-entry id"), bValid);
	TestTrue(TEXT("Validation failure explains the changed child source content-entry id"), FailureReason.Contains(TEXT("source content-entry id")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsUnexpectedOptionalDropOnProvedParentTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.SourceContentEntryId = TEXT("RootEntry");

	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8019);
	DemandResult.ChildRequest.SourceContentEntryId = TEXT("ChildEntry");
	DemandResult.ChildRequest.RegionCellOffset = FIntVector(7, 8, 9);

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{DemandResult});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;
	ParentResult.bDroppedAsOptionalChild = true;
	ParentResult.OptionalDropDecisionId = TEXT("UnexpectedParentDrop");
	ParentResult.OptionalDropReason = TEXT("Proof wrapper should not re-drop the proved parent.");

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a proved parent result that is unexpectedly marked as an optional drop"), bValid);
	TestTrue(TEXT("Validation failure explains the unexpected parent optional drop"), FailureReason.Contains(TEXT("optional drop")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsUnexpectedOptionalDropOnProvedChildTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.SourceContentEntryId = TEXT("RootEntry");

	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8018);
	DemandResult.ChildRequest.SourceContentEntryId = TEXT("ChildEntry");
	DemandResult.ChildRequest.RegionCellOffset = FIntVector(7, 8, 9);

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{DemandResult});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;
	ChildResult.bDroppedAsOptionalChild = true;
	ChildResult.OptionalDropDecisionId = TEXT("UnexpectedDrop");
	ChildResult.OptionalDropReason = TEXT("Proof wrapper should not re-drop proved children.");

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a proved child result that is unexpectedly marked as an optional drop"), bValid);
	TestTrue(TEXT("Validation failure explains the unexpected optional drop"), FailureReason.Contains(TEXT("optional drop")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsUnexpectedMergedOptionalDropRecordTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.SourceContentEntryId = TEXT("RootEntry");

	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8017);
	DemandResult.ChildRequest.SourceContentEntryId = TEXT("ChildEntry");
	DemandResult.ChildRequest.RegionCellOffset = FIntVector(7, 8, 9);

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{DemandResult});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;

	ScheduleResult.MergedSolveResult.DroppedOptionalChildren.Add(
		MakeRewriteDroppedOptionalChildRecord(
			TEXT("UnexpectedDrop"),
			TEXT("Root"),
			TEXT("Child")));

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects merged optional-drop records that were not fixed before proof started"), bValid);
	TestTrue(TEXT("Validation failure explains the unexpected merged optional-drop record"), FailureReason.Contains(TEXT("optional-drop")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsUnexpectedDroppedChildResidualCellTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.SourceContentEntryId = TEXT("RootEntry");

	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8016);
	DemandResult.ChildRequest.SourceContentEntryId = TEXT("ChildEntry");
	DemandResult.ChildRequest.RegionCellOffset = FIntVector(7, 8, 9);

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{DemandResult});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;

	FLayoutResidualCellRecord& ResidualCell =
		ScheduleResult.MergedSolveResult.ResidualUnoccupiedCells.AddDefaulted_GetRef();
	ResidualCell.Cell = FIntVector(1, 0, 0);
	ResidualCell.Intent = ELayoutCellIntent::Interior;
	ResidualCell.Source = ELayoutResidualCellSource::DroppedOptionalChild;
	ResidualCell.SourceRegionDebugPath = TEXT("Child");
	ResidualCell.SourceContentEntryId = TEXT("ChildEntry");
	ResidualCell.RelatedDropDecisionId = TEXT("UnexpectedDrop");

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects dropped-child residual cells that were not fixed by the proof contract"), bValid);
	TestTrue(TEXT("Validation failure explains the unexpected dropped-child residual attribution"), FailureReason.Contains(TEXT("dropped-child residual")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsUnexpectedParentResultDroppedChildResidualCellTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.SourceContentEntryId = TEXT("RootEntry");

	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8015);
	DemandResult.ChildRequest.SourceContentEntryId = TEXT("ChildEntry");
	DemandResult.ChildRequest.RegionCellOffset = FIntVector(7, 8, 9);

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{DemandResult});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;

	FLayoutResidualCellRecord& ResidualCell =
		ParentResult.SolveResult.ResidualUnoccupiedCells.AddDefaulted_GetRef();
	ResidualCell.Cell = FIntVector(2, 0, 0);
	ResidualCell.Intent = ELayoutCellIntent::Interior;
	ResidualCell.Source = ELayoutResidualCellSource::DroppedOptionalChild;
	ResidualCell.SourceRegionDebugPath = TEXT("Child");
	ResidualCell.SourceContentEntryId = TEXT("ChildEntry");
	ResidualCell.RelatedDropDecisionId = TEXT("UnexpectedParentResultDrop");

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects dropped-child residual cells on the proved parent result when the proof contract never fixed that drop decision"), bValid);
	TestTrue(TEXT("Validation failure explains the unexpected parent-result dropped-child residual cell"), FailureReason.Contains(TEXT("parent result")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsUnexpectedChildResultOptionalDropRecordTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.SourceContentEntryId = TEXT("RootEntry");

	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8014);
	DemandResult.ChildRequest.SourceContentEntryId = TEXT("ChildEntry");
	DemandResult.ChildRequest.RegionCellOffset = FIntVector(7, 8, 9);

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{DemandResult});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;
	ChildResult.SolveResult.DroppedOptionalChildren.Add(
		MakeRewriteDroppedOptionalChildRecord(
			TEXT("UnexpectedChildResultDrop"),
			TEXT("Root"),
			TEXT("Child")));

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects optional-drop records attached to a proved child result after negotiation finished"), bValid);
	TestTrue(TEXT("Validation failure explains the unexpected child-result optional-drop record"), FailureReason.Contains(TEXT("proved region")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsChangedRecursiveVerticalAccessSummaryTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.SourceContentEntryId = TEXT("RootEntry");

	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8013);
	DemandResult.ChildRequest.SourceContentEntryId = TEXT("ChildEntry");
	DemandResult.ChildRequest.RegionCellOffset = FIntVector(7, 8, 9);

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{DemandResult});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);
	ScheduleResult.RecursiveVerticalAccessSummary.RequiredHostProviderCount = 1;
	ScheduleResult.RecursiveVerticalAccessSummary.CountedParentProviderCount = 0;
	ScheduleResult.RecursiveVerticalAccessSummary.CountedChildRegionDebugPaths = {TEXT("UnexpectedChild")};

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a recursive vertical-access summary that changes counted child ownership after negotiation finished"), bValid);
	TestTrue(TEXT("Validation failure explains the changed recursive vertical-access ownership summary"), FailureReason.Contains(TEXT("vertical-access ownership summary")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsUnexpectedRecursiveVerticalAccessChildClassificationTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.SourceContentEntryId = TEXT("RootEntry");

	FNegotiatedDemandResult DemandResult = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8012);
	DemandResult.ChildRequest.SourceContentEntryId = TEXT("ChildEntry");
	DemandResult.ChildRequest.RegionCellOffset = FIntVector(7, 8, 9);

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{DemandResult});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);
	ScheduleResult.RecursiveVerticalAccessSummary.RequiredHostProviderCount = 1;
	ScheduleResult.RecursiveVerticalAccessSummary.CountedParentProviderCount = 0;
	ScheduleResult.RecursiveVerticalAccessSummary.CountedChildRegionDebugPaths = {TEXT("Child")};
	ScheduleResult.RecursiveVerticalAccessSummary.LocalOnlyChildRegionDebugPaths = {TEXT("UnexpectedLocalOnlyChild")};

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects recursive vertical-access child classifications that name non-negotiated child regions"), bValid);
	TestTrue(TEXT("Validation failure explains the changed recursive vertical-access ownership summary"), FailureReason.Contains(TEXT("vertical-access ownership summary")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsUnexpectedProvedRegionResultTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8009)
		});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;

	FLayoutRegionSolveResult& UnexpectedResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	UnexpectedResult.RegionDebugPath = TEXT("UnexpectedRegion");

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects an unexpected proved region result"), bValid);
	TestTrue(TEXT("Validation failure explains the changed proved region-result set"), FailureReason.Contains(TEXT("proved region-result set")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofContractSkipsEmptyDirectCommitmentsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	FNegotiatedDemandResult Result;
	Result.bSucceeded = true;
	Result.ChildRegionDebugPath = TEXT("Child");
	Result.ChildRequest.RegionDebugPath = TEXT("Child");
	Result.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	Result.ResponsibilitySet.ResponsibilityContract.ParentRegionDebugPath = TEXT("Root");
	Result.ResponsibilitySet.ResponsibilityContract.ChildRegionDebugPath = TEXT("Child");

	const FNegotiatedProofScheduleContract ProofContract =
		BuildNegotiatedProofScheduleContract(SolveContext, {Result});

	TestEqual(TEXT("Proof contract does not emit an empty direct child commitment"), ProofContract.DirectChildCommitments.Num(), 0);
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsMissingZeroEndpointChildContractTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	FNegotiatedDemandResult Result;
	Result.bSucceeded = true;
	Result.ChildRegionDebugPath = TEXT("Child");
	Result.ChildRequest.RegionDebugPath = TEXT("Child");
	Result.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	Result.ResponsibilitySet.ResponsibilityContract.ParentRegionDebugPath = TEXT("Root");
	Result.ResponsibilitySet.ResponsibilityContract.ChildRegionDebugPath = TEXT("Child");

	const FNegotiatedProofScheduleContract ProofContract =
		BuildNegotiatedProofScheduleContract(SolveContext, {Result});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects a missing zero-endpoint child contract"), bValid);
	TestTrue(TEXT("Validation failure explains the changed child responsibility contract"), FailureReason.Contains(TEXT("child responsibility-contract set")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteValidationRejectsExtraChildAnchorTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FNegotiatedProofScheduleContract ProofContract = BuildNegotiatedProofScheduleContract(
		SolveContext,
		{
			MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8005)
		});

	FLayoutRegionSolveScheduleResult ScheduleResult;
	InitializeSuccessfulRewriteScheduleResult(ScheduleResult, ProofContract);

	FLayoutRegionSolveResult& ParentResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ParentResult.RegionDebugPath = TEXT("Root");
	ParentResult.RegionCellOffset = ProofContract.ParentRequest.RegionCellOffset;
	ParentResult.SourceContentEntryId = ProofContract.ParentRequest.SourceContentEntryId;
	ParentResult.NegotiatedChildResponsibilityContracts =
		ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
	ParentResult.CommittedEndpointAnchors = ProofContract.ParentRequest.CommittedEndpointAnchors;
	ParentResult.ProofRecords = ProofContract.ParentRequest.ProofRecords;
	ParentResult.ValidationAssertions = ProofContract.ParentRequest.ValidationAssertions;

	FLayoutRegionSolveResult& ChildResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	ChildResult.RegionDebugPath = TEXT("Child");
	ChildResult.RegionCellOffset = ProofContract.ChildRequests[0].RegionCellOffset;
	ChildResult.SourceContentEntryId = ProofContract.ChildRequests[0].SourceContentEntryId;
	ChildResult.NegotiatedChildResponsibilityContracts = {
		ProofContract.DirectChildCommitments[0].NegotiatedResponsibilityContract
	};
	ChildResult.CommittedEndpointAnchors = ProofContract.DirectChildCommitments[0].EndpointCommitments;
	ChildResult.ProofRecords = ProofContract.ChildRequests[0].ProofRecords;
	ChildResult.ValidationAssertions = ProofContract.ChildRequests[0].ValidationAssertions;
	ChildResult.CommittedEndpointAnchors.Add(
		MakeEndpointAnchor(TEXT("Extra"), FIntVector(4, 4, 4), ELayoutFaceDirection::PosY));

	FString FailureReason;
	const bool bValid = ValidateNegotiatedScheduleResult(
		SolveContext,
		ProofContract,
		ScheduleResult,
		FailureReason);
	TestFalse(TEXT("Validation rejects an extra committed child anchor"), bValid);
	TestTrue(TEXT("Validation failure explains the changed child-anchor set"), FailureReason.Contains(TEXT("child-anchor set")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofContractPreservesEndpointMetadataTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	FNegotiatedDemandResult Result = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8002);

	FLayoutCommittedEndpointAnchor VariantAnchor = Result.ResponsibilitySet.ContactSet.EndpointCommitments[0];
	VariantAnchor.TraversalChannels.AddTag(LayoutGameplayTags::TraversalSecondary);
	Result.ResponsibilitySet.ContactSet.EndpointCommitments.Add(VariantAnchor);

	const FNegotiatedProofScheduleContract ProofContract =
		BuildNegotiatedProofScheduleContract(SolveContext, {Result});

	TestEqual(TEXT("Proof contract preserves distinct endpoint metadata variants"), ProofContract.ChildRequests[0].CommittedEndpointAnchors.Num(), 3);
	return true;
}

bool FLayoutRecursiveScheduleRewriteProofContractKeepsSharedHostAnchorsProofOnlyTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	FNegotiatedDemandResult Result = MakeSuccessfulDemandResult(TEXT("Root"), TEXT("Child"), 8003);

	FLayoutCommittedEndpointAnchor SharedHostAnchor =
		MakeEndpointAnchor(
			TEXT("Child.SharedHost.(1,0,1).1.Solid"),
			FIntVector(1, 0, 1),
			ELayoutFaceDirection::PosX);
	SharedHostAnchor.ConnectionTag = LayoutGameplayTags::FaceOpen;
	SharedHostAnchor.AllowedConnectionTags.Reset();
	SharedHostAnchor.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
	SharedHostAnchor.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceSolid);

	FLayoutNegotiatedLevelInterfaceContract* UpperInterface =
		Result.ResponsibilitySet.ResponsibilityContract
			.CommittedParentChildInterfacesByLevel.FindByPredicate(
				[](const FLayoutNegotiatedLevelInterfaceContract& InterfaceContract)
				{
					return InterfaceContract.Level == 1;
				});
	TestNotNull(TEXT("Test fixture finds the upper negotiated interface"), UpperInterface);
	if (UpperInterface == nullptr)
	{
		return false;
	}

	UpperInterface->EndpointAnchors.Add(SharedHostAnchor);
	Result.ResponsibilitySet.ContactSet.EndpointCommitments.Add(SharedHostAnchor);
	FLayoutNegotiatedLevelCellSet& ProofOnlyHostShell =
		Result.ResponsibilitySet.ResponsibilityContract
			.ProofOnlyHostAscentParentShellCellsByLevel.AddDefaulted_GetRef();
	ProofOnlyHostShell.Level = 1;
	ProofOnlyHostShell.Cells = {
		FIntVector(6, 1, 1),
		FIntVector(6, 1, 1),
		FIntVector(7, 1, 1)
	};

	const FNegotiatedProofScheduleContract ProofContract =
		BuildNegotiatedProofScheduleContract(SolveContext, {Result});

	const bool bChildRequestContainsSharedHost =
		ProofContract.ChildRequests[0].CommittedEndpointAnchors.ContainsByPredicate(
			[](const FLayoutCommittedEndpointAnchor& Anchor)
			{
				return Anchor.CommitmentId.ToString().Contains(TEXT(".SharedHost."));
			});
	const bool bDirectCommitmentContainsSharedHost =
		ProofContract.DirectChildCommitments[0].EndpointCommitments.ContainsByPredicate(
			[](const FLayoutCommittedEndpointAnchor& Anchor)
			{
				return Anchor.CommitmentId.ToString().Contains(TEXT(".SharedHost."));
			});

	TestFalse(
		TEXT("Proof contract child request keeps synthetic SharedHost anchors out of direct endpoint obligations"),
		bChildRequestContainsSharedHost);
	TestFalse(
		TEXT("Proof contract direct child commitments keep synthetic SharedHost anchors out of direct endpoint obligations"),
		bDirectCommitmentContainsSharedHost);
	TestTrue(
		TEXT("Negotiated contact set can retain the synthetic SharedHost anchor for confirmation"),
		Result.ResponsibilitySet.ContactSet.EndpointCommitments.ContainsByPredicate(
			[](const FLayoutCommittedEndpointAnchor& Anchor)
			{
				return Anchor.CommitmentId.ToString().Contains(TEXT(".SharedHost."));
			}));
	TestTrue(
		TEXT("Negotiated responsibility contract still retains the SharedHost anchor as proof metadata"),
		ProofContract.ResponsibilityContracts[0].CommittedParentChildInterfacesByLevel.ContainsByPredicate(
			[](const FLayoutNegotiatedLevelInterfaceContract& InterfaceContract)
			{
				return InterfaceContract.EndpointAnchors.ContainsByPredicate(
					[](const FLayoutCommittedEndpointAnchor& Anchor)
					{
						return Anchor.CommitmentId.ToString().Contains(TEXT(".SharedHost."));
					});
			}));
	TestEqual(
		TEXT("Proof contract preserves the separate proof-only host shell levels"),
		ProofContract.ResponsibilityContracts[0].ProofOnlyHostAscentParentShellCellsByLevel.Num(),
		1);
	TestEqual(
		TEXT("Proof contract preserves the proof-only host shell cell count"),
		ProofContract.ResponsibilityContracts[0].ProofOnlyHostAscentParentShellCellsByLevel[0].Cells.Num(),
		3);
	TestEqual(
		TEXT("Proof contract keeps the first proof-only host shell cell"),
		ProofContract.ResponsibilityContracts[0].ProofOnlyHostAscentParentShellCellsByLevel[0].Cells[0],
		FIntVector(6, 1, 1));
	TestEqual(
		TEXT("Proof contract keeps the second proof-only host shell cell duplicate"),
		ProofContract.ResponsibilityContracts[0].ProofOnlyHostAscentParentShellCellsByLevel[0].Cells[1],
		FIntVector(6, 1, 1));
	TestEqual(
		TEXT("Proof contract keeps the third proof-only host shell cell"),
		ProofContract.ResponsibilityContracts[0].ProofOnlyHostAscentParentShellCellsByLevel[0].Cells[2],
		FIntVector(7, 1, 1));
	return true;
}



bool FLayoutRecursiveScheduleRewriteDemandPlanPreservesMissingChildTemplateFailureTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.Seed = 9013;

	FLayoutRegionContentEntrySolveSnapshot MissingTemplateEntry;
	MissingTemplateEntry.EntryId = TEXT("MissingTemplateChild");
	MissingTemplateEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	MissingTemplateEntry.ChildProfileSnapshotId = TEXT("MissingProfile");
	MissingTemplateEntry.ChildContentSetSnapshotId = TEXT("MissingContent");
	MissingTemplateEntry.bChildOptional = false;

	RootRequest.ContentSetSnapshot.Entries = {MissingTemplateEntry};

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));

	TestEqual(TEXT("Missing child templates still preserve one authored child demand"), StructuralInputs.Demands.Num(), 1);
	const FNegotiationDemandPlan& Demand = StructuralInputs.Demands[0];
	TestEqual(TEXT("Missing-template demand keeps the child region path"), Demand.ChildRegionDebugPath, TEXT("Root/MissingTemplateChild"));
	TestEqual(TEXT("Missing-template demand keeps the source content-entry id"), Demand.ChildRequest.SourceContentEntryId, TEXT("MissingTemplateChild"));

	const FLayoutValidationAssertionRecord* MissingTemplateAssertion =
		FindRewriteAssertion(Demand.ChildRequest.ValidationAssertions, TEXT("RegionRequest.ChildTemplatePresent"));
	TestNotNull(TEXT("Missing-template demand records a failed child-request assertion"), MissingTemplateAssertion);
	if (MissingTemplateAssertion != nullptr)
	{
		TestFalse(TEXT("Missing-template demand marks the child-request assertion as failed"), MissingTemplateAssertion->bPassed);
		TestTrue(TEXT("Missing-template demand request assertion names the child request path"), MissingTemplateAssertion->FailureReason.Contains(TEXT("Child request 'Root/MissingTemplateChild'")));
		TestTrue(TEXT("Missing-template demand request assertion keeps the frozen child profile snapshot id"), MissingTemplateAssertion->FailureReason.Contains(TEXT("MissingProfile")));
		TestTrue(TEXT("Missing-template demand request assertion keeps the frozen child content-set snapshot id"), MissingTemplateAssertion->FailureReason.Contains(TEXT("MissingContent")));
		TestTrue(TEXT("Missing-template demand request assertion related ids keep the child effective snapshot id"), MissingTemplateAssertion->RelatedIds.Contains(TEXT("Root/MissingTemplateChild.MissingChildTemplate")));
	}

	const FLayoutValidationAssertionRecord* MissingCapabilityAssertion =
		FindRewriteAssertion(Demand.ChildSummary.CapabilityEnvelope.ValidationAssertions, TEXT("ChildCapability.TemplatePresent"));
	TestNotNull(TEXT("Missing-template demand records a failed child-capability assertion"), MissingCapabilityAssertion);
	if (MissingCapabilityAssertion != nullptr)
	{
		TestFalse(TEXT("Missing-template demand marks the child-capability assertion as failed"), MissingCapabilityAssertion->bPassed);
		TestTrue(TEXT("Missing-template demand capability assertion names the child capability envelope path"), MissingCapabilityAssertion->FailureReason.Contains(TEXT("Child capability envelope 'Root/MissingTemplateChild'")));
		TestTrue(TEXT("Missing-template demand capability assertion keeps the frozen child profile snapshot id"), MissingCapabilityAssertion->FailureReason.Contains(TEXT("MissingProfile")));
		TestTrue(TEXT("Missing-template demand capability assertion keeps the frozen child content-set snapshot id"), MissingCapabilityAssertion->FailureReason.Contains(TEXT("MissingContent")));
		TestTrue(TEXT("Missing-template demand capability assertion related ids keep the child effective snapshot id"), MissingCapabilityAssertion->RelatedIds.Contains(TEXT("Root/MissingTemplateChild.MissingChildTemplate")));
	}
	TestFalse(TEXT("Missing-template demand does not advertise parent-owned host-access participation"), Demand.ChildSummary.ResponsibilityParticipation.bSupportsParentOwnedHostAccess);
	TestFalse(TEXT("Missing-template demand does not advertise optional-drop participation when the authored child is required"), Demand.ChildSummary.ResponsibilityParticipation.bSupportsOptionalDrop);

	return true;
}

bool FLayoutRecursiveScheduleRewriteChildCapabilityDedupePreservesRoleDistinctVariantsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.Seed = 9014;

	FLayoutChildRequestTemplateSnapshot Template = MakeDemandCompilationChildTemplate(
		TEXT("RoleProfile"),
		TEXT("RoleContent"),
		TEXT("RoleModuleA"),
		false,
		false);
	FLayoutModuleSolveSnapshot RoleModuleB = MakeDemandCompilationModuleSnapshot(
		TEXT("RoleModuleB"),
		false,
		false);
	RoleModuleB.Roles = {ELayoutModuleRole::Boundary};
	RoleModuleB.DerivedEndpointOffers[0].Roles = RoleModuleB.Roles;
	RoleModuleB.DerivedSpanOffers[0].Roles = RoleModuleB.Roles;
	Template.ModuleCatalog.Modules.Add(RoleModuleB);

	RootRequest.ContentSetSnapshot.Entries = {
		MakeDemandCompilationChildEntry(TEXT("RoleChild"), Template, false, false)
	};

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));
	TestEqual(TEXT("Role-distinct capability test builds one child demand"), StructuralInputs.Demands.Num(), 1);

	const FLayoutChildCapabilityEnvelope& Envelope = StructuralInputs.Demands[0].ChildSummary.CapabilityEnvelope;
	TestEqual(TEXT("Endpoint capability dedupe preserves role-distinct variants"), Envelope.EndpointCapabilities.Num(), 2);
	TestEqual(TEXT("Span capability dedupe preserves role-distinct variants"), Envelope.SpanCapabilities.Num(), 2);
	TestTrue(TEXT("Endpoint capabilities keep the entry-plus-boundary role variant"), Envelope.EndpointCapabilities.ContainsByPredicate(
		[](const FLayoutChildCapabilityEndpoint& Capability)
		{
			return DoesCapabilityExposeExactRoles(Capability.Roles, {ELayoutModuleRole::Boundary, ELayoutModuleRole::Entry});
		}));
	TestTrue(TEXT("Endpoint capabilities keep the boundary-only role variant"), Envelope.EndpointCapabilities.ContainsByPredicate(
		[](const FLayoutChildCapabilityEndpoint& Capability)
		{
			return DoesCapabilityExposeExactRoles(Capability.Roles, {ELayoutModuleRole::Boundary});
		}));
	TestTrue(TEXT("Span capabilities keep the entry-plus-boundary role variant"), Envelope.SpanCapabilities.ContainsByPredicate(
		[](const FLayoutChildCapabilitySpan& Capability)
		{
			return DoesCapabilityExposeExactRoles(Capability.Roles, {ELayoutModuleRole::Boundary, ELayoutModuleRole::Entry});
		}));
	TestTrue(TEXT("Span capabilities keep the boundary-only role variant"), Envelope.SpanCapabilities.ContainsByPredicate(
		[](const FLayoutChildCapabilitySpan& Capability)
		{
			return DoesCapabilityExposeExactRoles(Capability.Roles, {ELayoutModuleRole::Boundary});
		}));

	return true;
}

bool FLayoutRecursiveScheduleRewriteBuildsMultiLevelConnectableAnchorPairsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.Seed = 9017;

	FLayoutChildRequestTemplateSnapshot Template = MakeDemandCompilationChildTemplate(
		TEXT("MultiLevelProfile"),
		TEXT("MultiLevelContent"),
		TEXT("MultiLevelModule"),
		true,
		false);
	FLayoutDerivedEndpointOffer& UpperEndpointOffer =
		Template.ModuleCatalog.Modules[0].DerivedEndpointOffers.AddDefaulted_GetRef();
	UpperEndpointOffer.OfferId = TEXT("MultiLevelModule.EndpointUpper");
	UpperEndpointOffer.LocalCell = FIntVector(0, 0, 1);
	UpperEndpointOffer.FaceDirection = ELayoutFaceDirection::NegX;
	UpperEndpointOffer.ConnectionTag = LayoutGameplayTags::FaceEntry;
	UpperEndpointOffer.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
	UpperEndpointOffer.TraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
	UpperEndpointOffer.Roles = Template.ModuleCatalog.Modules[0].Roles;

	FLayoutDerivedSpanOffer& UpperSpanOffer =
		Template.ModuleCatalog.Modules[0].DerivedSpanOffers.AddDefaulted_GetRef();
	UpperSpanOffer.SpanOfferId = TEXT("MultiLevelModule.SpanUpper");
	UpperSpanOffer.LocalCell = FIntVector(0, 0, 1);
	UpperSpanOffer.FaceDirection = ELayoutFaceDirection::NegY;
	UpperSpanOffer.ConnectionTag = LayoutGameplayTags::FaceOpen;
	UpperSpanOffer.Roles = Template.ModuleCatalog.Modules[0].Roles;
	UpperSpanOffer.ThicknessCells = 1;
	UpperSpanOffer.bSealsBoundary = true;

	RootRequest.ContentSetSnapshot.Entries = {
		MakeDemandCompilationChildEntry(TEXT("MultiLevelChild"), Template, false, true)
	};

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));
	TestEqual(TEXT("Multi-level connectable-pair test builds one child demand"), StructuralInputs.Demands.Num(), 1);

	const FChildCapabilitySummary& Summary = StructuralInputs.Demands[0].ChildSummary;
	TestEqual(TEXT("Multi-level connectable-pair test preserves both endpoint offers"), Summary.EndpointOffers.Num(), 2);
	TestEqual(TEXT("Multi-level connectable-pair test preserves both endpoint-offer level summaries"), Summary.EndpointOffersByLevel.Num(), 2);
	TestTrue(TEXT("Lower endpoint capability keeps its proving local cell"), Summary.EndpointOffers.ContainsByPredicate(
		[](const FLayoutChildCapabilityEndpoint& Endpoint)
		{
			return Endpoint.LocalCell == FIntVector(0, 0, 0);
		}));
	TestTrue(TEXT("Upper endpoint capability keeps its proving local cell"), Summary.EndpointOffers.ContainsByPredicate(
		[](const FLayoutChildCapabilityEndpoint& Endpoint)
		{
			return Endpoint.LocalCell == FIntVector(0, 0, 1);
		}));
	TestEqual(TEXT("Multi-level span test preserves both span offers"), Summary.SpanOffers.Num(), 2);
	TestEqual(TEXT("Multi-level span test preserves both span-offer level summaries"), Summary.SpanOffersByLevel.Num(), 2);
	TestEqual(TEXT("Multi-level seam test preserves both seam offers"), Summary.SeamOffers.Num(), 2);
	TestEqual(TEXT("Multi-level seam test preserves both seam-offer level summaries"), Summary.SeamOffersByLevel.Num(), 2);
	TestEqual(TEXT("Multi-level connectable-pair test preserves both vertical-access support bands"), Summary.VerticalAccessSupportBandsByLevel.Num(), 2);
	TestEqual(TEXT("Multi-level connectable-pair test builds two traversal summaries"), Summary.TraversalSummariesByLevel.Num(), 2);

	if (!TestEqual(
			TEXT("Multi-level connectable-pair test builds one generally connectable anchor pair"),
			Summary.GenerallyConnectableAnchorPairIds.Num(),
			1))
	{
		return false;
	}

	const FLayoutId ExpectedPairId = Summary.GenerallyConnectableAnchorPairIds[0];
	const FChildLevelTraversalCapabilitySummary* LowerSummary =
		Summary.TraversalSummariesByLevel.FindByPredicate([](const FChildLevelTraversalCapabilitySummary& TraversalSummary)
		{
			return TraversalSummary.LevelIndex == 0;
		});
	const FChildLevelTraversalCapabilitySummary* UpperSummary =
		Summary.TraversalSummariesByLevel.FindByPredicate([](const FChildLevelTraversalCapabilitySummary& TraversalSummary)
		{
			return TraversalSummary.LevelIndex == 1;
		});
	if (!TestNotNull(TEXT("Lower traversal summary exists"), LowerSummary)
		|| !TestNotNull(TEXT("Upper traversal summary exists"), UpperSummary))
	{
		return false;
	}
	const FChildLevelEndpointCapabilitySummary* LowerEndpointSummary =
		Summary.EndpointOffersByLevel.FindByPredicate([](const FChildLevelEndpointCapabilitySummary& EndpointSummary)
		{
			return EndpointSummary.LevelIndex == 0;
		});
	const FChildLevelEndpointCapabilitySummary* UpperEndpointSummary =
		Summary.EndpointOffersByLevel.FindByPredicate([](const FChildLevelEndpointCapabilitySummary& EndpointSummary)
		{
			return EndpointSummary.LevelIndex == 1;
		});
	if (!TestNotNull(TEXT("Lower endpoint summary exists"), LowerEndpointSummary)
		|| !TestNotNull(TEXT("Upper endpoint summary exists"), UpperEndpointSummary))
	{
		return false;
	}
	const FLayoutValidationAssertionRecord* MultiLevelHostRequestAssertion =
		FindRewriteAssertion(StructuralInputs.Demands[0].ChildRequest.ValidationAssertions, TEXT("RegionRequest.ChildHostVerticalAccessContract"));
	TestNotNull(TEXT("Multi-level host-contributing child records a request-side host-vertical-access contract assertion"), MultiLevelHostRequestAssertion);
	if (MultiLevelHostRequestAssertion != nullptr)
	{
		TestTrue(TEXT("Multi-level host-contributing child passes the request-side host-vertical-access contract assertion"), MultiLevelHostRequestAssertion->bPassed);
	}
	const FLayoutValidationAssertionRecord* MultiLevelHostCapabilityAssertion =
		FindRewriteAssertion(Summary.CapabilityEnvelope.ValidationAssertions, TEXT("ChildCapability.HostVerticalAccessContract"));
	TestNotNull(TEXT("Multi-level host-contributing child records a capability-side host-vertical-access contract assertion"), MultiLevelHostCapabilityAssertion);
	if (MultiLevelHostCapabilityAssertion != nullptr)
	{
		TestTrue(TEXT("Multi-level host-contributing child passes the capability-side host-vertical-access contract assertion"), MultiLevelHostCapabilityAssertion->bPassed);
	}
	TestTrue(TEXT("Multi-level host-contributing child advertises child-owned host access"), Summary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess);
	TestTrue(TEXT("Multi-level host-contributing child advertises composed host access"), Summary.ResponsibilityParticipation.bSupportsComposedHostAccess);
	TestEqual(TEXT("Lower endpoint summary keeps one proving endpoint offer"), LowerEndpointSummary->EndpointOffers.Num(), 1);
	TestEqual(TEXT("Upper endpoint summary keeps one proving endpoint offer"), UpperEndpointSummary->EndpointOffers.Num(), 1);
	TestEqual(TEXT("Lower endpoint summary keeps the exact proving endpoint cell"), LowerEndpointSummary->EndpointOffers[0].LocalCell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Upper endpoint summary keeps the exact proving endpoint cell"), UpperEndpointSummary->EndpointOffers[0].LocalCell, FIntVector(0, 0, 1));

	const FChildLevelSeamCapabilitySummary* LowerSeamSummary =
		Summary.SeamOffersByLevel.FindByPredicate([](const FChildLevelSeamCapabilitySummary& SeamSummary)
		{
			return SeamSummary.LevelIndex == 0;
		});
	const FChildLevelSeamCapabilitySummary* UpperSeamSummary =
		Summary.SeamOffersByLevel.FindByPredicate([](const FChildLevelSeamCapabilitySummary& SeamSummary)
		{
			return SeamSummary.LevelIndex == 1;
		});
	if (!TestNotNull(TEXT("Lower seam summary exists"), LowerSeamSummary)
		|| !TestNotNull(TEXT("Upper seam summary exists"), UpperSeamSummary))
	{
		return false;
	}
	const FChildLevelSpanCapabilitySummary* LowerSpanSummary =
		Summary.SpanOffersByLevel.FindByPredicate([](const FChildLevelSpanCapabilitySummary& SpanSummary)
		{
			return SpanSummary.LevelIndex == 0;
		});
	const FChildLevelSpanCapabilitySummary* UpperSpanSummary =
		Summary.SpanOffersByLevel.FindByPredicate([](const FChildLevelSpanCapabilitySummary& SpanSummary)
		{
			return SpanSummary.LevelIndex == 1;
		});
	if (!TestNotNull(TEXT("Lower span summary exists"), LowerSpanSummary)
		|| !TestNotNull(TEXT("Upper span summary exists"), UpperSpanSummary))
	{
		return false;
	}
	TestEqual(TEXT("Lower seam summary keeps one proving seam offer"), LowerSeamSummary->SeamOffers.Num(), 1);
	TestEqual(TEXT("Upper seam summary keeps one proving seam offer"), UpperSeamSummary->SeamOffers.Num(), 1);
	TestEqual(TEXT("Lower seam summary keeps the exact proving seam cell"), LowerSeamSummary->SeamOffers[0].LocalCell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Upper seam summary keeps the exact proving seam cell"), UpperSeamSummary->SeamOffers[0].LocalCell, FIntVector(0, 0, 1));
	TestEqual(TEXT("Lower span summary keeps one proving span offer"), LowerSpanSummary->SpanOffers.Num(), 1);
	TestEqual(TEXT("Upper span summary keeps one proving span offer"), UpperSpanSummary->SpanOffers.Num(), 1);
	TestEqual(TEXT("Lower span summary keeps the exact proving span cell"), LowerSpanSummary->SpanOffers[0].LocalCell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Upper span summary keeps the exact proving span cell"), UpperSpanSummary->SpanOffers[0].LocalCell, FIntVector(0, 0, 1));

	const FChildVerticalAccessSupportBand* LowerBand =
		Summary.VerticalAccessSupportBandsByLevel.FindByPredicate([](const FChildVerticalAccessSupportBand& Band)
		{
			return Band.LevelIndex == 0;
		});
	const FChildVerticalAccessSupportBand* UpperBand =
		Summary.VerticalAccessSupportBandsByLevel.FindByPredicate([](const FChildVerticalAccessSupportBand& Band)
		{
			return Band.LevelIndex == 1;
		});
	if (!TestNotNull(TEXT("Lower vertical-access support band exists"), LowerBand)
		|| !TestNotNull(TEXT("Upper vertical-access support band exists"), UpperBand))
	{
		return false;
	}
	TestEqual(TEXT("Lower vertical-access support band keeps one proving cell"), LowerBand->ProvingVerticalAccessCells.Num(), 1);
	TestEqual(TEXT("Upper vertical-access support band keeps one proving cell"), UpperBand->ProvingVerticalAccessCells.Num(), 1);
	TestEqual(TEXT("Lower vertical-access support band keeps the exact proving cell"), LowerBand->ProvingVerticalAccessCells[0], FIntVector(0, 0, 1));
	TestEqual(TEXT("Upper vertical-access support band keeps the exact proving cell"), UpperBand->ProvingVerticalAccessCells[0], FIntVector(0, 0, 1));

	TestTrue(TEXT("Lower traversal summary exposes host ingress"), LowerSummary->bCanExposeHostIngress);
	TestTrue(TEXT("Lower traversal summary exposes host egress"), LowerSummary->bCanExposeHostEgress);
	TestTrue(TEXT("Lower traversal summary carries host vertical access"), LowerSummary->bCanCarryHostVerticalAccess);
	TestTrue(TEXT("Lower traversal summary preserves the shared traversal channel"), LowerSummary->TraversalChannels.HasTagExact(LayoutGameplayTags::TraversalPrimary));
	if (!TestEqual(
			TEXT("Lower traversal summary keeps the connectable anchor-pair id"),
			LowerSummary->ConnectableAnchorPairIds.Num(),
			1))
	{
		return false;
	}
	TestEqual(TEXT("Lower traversal summary keeps the expected anchor-pair id"), LowerSummary->ConnectableAnchorPairIds[0], ExpectedPairId);

	TestTrue(TEXT("Upper traversal summary exposes host ingress"), UpperSummary->bCanExposeHostIngress);
	TestTrue(TEXT("Upper traversal summary exposes host egress"), UpperSummary->bCanExposeHostEgress);
	TestTrue(TEXT("Upper traversal summary carries host vertical access"), UpperSummary->bCanCarryHostVerticalAccess);
	TestTrue(TEXT("Upper traversal summary preserves the shared traversal channel"), UpperSummary->TraversalChannels.HasTagExact(LayoutGameplayTags::TraversalPrimary));
	if (!TestEqual(
			TEXT("Upper traversal summary keeps the connectable anchor-pair id"),
			UpperSummary->ConnectableAnchorPairIds.Num(),
			1))
	{
		return false;
	}
	TestEqual(TEXT("Upper traversal summary keeps the expected anchor-pair id"), UpperSummary->ConnectableAnchorPairIds[0], ExpectedPairId);
	return true;
}

bool FLayoutRecursiveScheduleRewriteRejectsHostContributingChildWithoutConnectableUpperLowerAnchorPairTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.Seed = 9018;

	FLayoutChildRequestTemplateSnapshot Template = MakeDemandCompilationChildTemplate(
		TEXT("DisconnectedTraversalProfile"),
		TEXT("DisconnectedTraversalContent"),
		TEXT("DisconnectedTraversalModule"),
		true,
		false);
	FLayoutDerivedEndpointOffer& UpperEndpointOffer =
		Template.ModuleCatalog.Modules[0].DerivedEndpointOffers.AddDefaulted_GetRef();
	UpperEndpointOffer.OfferId = TEXT("DisconnectedTraversalModule.EndpointUpper");
	UpperEndpointOffer.LocalCell = FIntVector(0, 0, 1);
	UpperEndpointOffer.FaceDirection = ELayoutFaceDirection::NegX;
	UpperEndpointOffer.ConnectionTag = LayoutGameplayTags::FaceEntry;
	UpperEndpointOffer.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
	UpperEndpointOffer.TraversalChannels.Reset();
	UpperEndpointOffer.TraversalChannels.AddTag(LayoutGameplayTags::TraversalSecondary);
	UpperEndpointOffer.Roles = Template.ModuleCatalog.Modules[0].Roles;

	FLayoutDerivedSpanOffer& UpperSpanOffer =
		Template.ModuleCatalog.Modules[0].DerivedSpanOffers.AddDefaulted_GetRef();
	UpperSpanOffer.SpanOfferId = TEXT("DisconnectedTraversalModule.SpanUpper");
	UpperSpanOffer.LocalCell = FIntVector(0, 0, 1);
	UpperSpanOffer.FaceDirection = ELayoutFaceDirection::NegY;
	UpperSpanOffer.ConnectionTag = LayoutGameplayTags::FaceOpen;
	UpperSpanOffer.Roles = Template.ModuleCatalog.Modules[0].Roles;
	UpperSpanOffer.ThicknessCells = 1;
	UpperSpanOffer.bSealsBoundary = true;

	RootRequest.ContentSetSnapshot.Entries = {
		MakeDemandCompilationChildEntry(TEXT("DisconnectedTraversalChild"), Template, false, true)
	};

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));
	if (!TestEqual(TEXT("Disconnected-traversal host-contributing child still compiles one demand"), StructuralInputs.Demands.Num(), 1))
	{
		return false;
	}

	const FNegotiationDemandPlan& Demand = StructuralInputs.Demands[0];
	const FLayoutValidationAssertionRecord* RequestAssertion =
		FindRewriteAssertion(Demand.ChildRequest.ValidationAssertions, TEXT("RegionRequest.ChildHostVerticalAccessContract"));
	TestNotNull(TEXT("Disconnected-traversal child records a request-side host-vertical-access contract assertion"), RequestAssertion);
	if (RequestAssertion != nullptr)
	{
		TestFalse(TEXT("Disconnected-traversal child fails the request-side host-vertical-access contract assertion"), RequestAssertion->bPassed);
	}
	const FLayoutValidationAssertionRecord* CapabilityAssertion =
		FindRewriteAssertion(Demand.ChildSummary.CapabilityEnvelope.ValidationAssertions, TEXT("ChildCapability.HostVerticalAccessContract"));
	TestNotNull(TEXT("Disconnected-traversal child records a capability-side host-vertical-access contract assertion"), CapabilityAssertion);
	if (CapabilityAssertion != nullptr)
	{
		TestFalse(TEXT("Disconnected-traversal child fails the capability-side host-vertical-access contract assertion"), CapabilityAssertion->bPassed);
	}
	TestFalse(TEXT("Disconnected-traversal child no longer advertises child-owned host access"), Demand.ChildSummary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess);
	TestFalse(TEXT("Disconnected-traversal child no longer advertises composed host access"), Demand.ChildSummary.ResponsibilityParticipation.bSupportsComposedHostAccess);
	TestFalse(TEXT("Disconnected-traversal child no longer advertises child traversal-bridge participation"), Demand.FeatureFlags.bAllowsChildTraversalBridge);
	TestTrue(TEXT("Disconnected-traversal child still preserves multi-level negotiation shape"), Demand.FeatureFlags.bRequiresMultiLevelAnchorSet);
	return true;
}

bool FLayoutRecursiveScheduleRewriteExpandsRepeatedChildDemandFromHardFeatureRequirementTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.Seed = 9015;

	FLayoutZoneFeatureRequirement& Requirement =
		RootRequest.ProfileSnapshot.ZoneFeatureRequirements.AddDefaulted_GetRef();
	Requirement.RequirementId = TEXT("RoomRequirement");
	Requirement.RequiredFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
	Requirement.MatchMode = ELayoutZoneFeatureMatchMode::Any;
	Requirement.MinCount = 2;

	FLayoutRegionContentEntrySolveSnapshot Entry = MakeDemandCompilationChildEntry(
		TEXT("RoomChild"),
		MakeDemandCompilationChildTemplate(
			TEXT("RoomProfile"),
			TEXT("RoomContent"),
			TEXT("RoomModule"),
			false,
			false),
		true,
		false);
	Entry.ProvidedZoneFeatures.AddTag(LayoutGameplayTags::FeatureRoom);

	RootRequest.ContentSetSnapshot.Entries = {Entry};

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));

	TestEqual(TEXT("One uniquely matching hard feature requirement expands the child demand count"), StructuralInputs.Demands.Num(), 2);
	TestEqual(TEXT("First repeated child demand uses the first deterministic region path"), StructuralInputs.Demands[0].ChildRegionDebugPath, TEXT("Root/RoomChild_1"));
	TestEqual(TEXT("Second repeated child demand uses the second deterministic region path"), StructuralInputs.Demands[1].ChildRegionDebugPath, TEXT("Root/RoomChild_2"));
	TestFalse(TEXT("Repeated hard-required child demands are not treated as optional"), StructuralInputs.Demands[0].FeatureFlags.bOptionalDemand);
	TestFalse(TEXT("Repeated hard-required child demands are not treated as optional"), StructuralInputs.Demands[1].FeatureFlags.bOptionalDemand);
	TestNotEqual(TEXT("Repeated child demand seeds stay deterministic but distinct per instance"), StructuralInputs.Demands[0].ChildRequest.Seed, StructuralInputs.Demands[1].ChildRequest.Seed);
	return true;
}

bool FLayoutRecursiveScheduleRewriteCompilesAmbiguousRepeatedChildRequirementAsChoiceSlotsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.Seed = 9016;

	FLayoutZoneFeatureRequirement& Requirement =
		RootRequest.ProfileSnapshot.ZoneFeatureRequirements.AddDefaulted_GetRef();
	Requirement.RequirementId = TEXT("RoomRequirement");
	Requirement.RequiredFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
	Requirement.MatchMode = ELayoutZoneFeatureMatchMode::Any;
	Requirement.MinCount = 2;

	FLayoutRegionContentEntrySolveSnapshot FirstEntry = MakeDemandCompilationChildEntry(
		TEXT("RoomChildA"),
		MakeDemandCompilationChildTemplate(
			TEXT("RoomProfileA"),
			TEXT("RoomContentA"),
			TEXT("RoomModuleA"),
			false,
			false),
		false,
		false);
	FirstEntry.ProvidedZoneFeatures.AddTag(LayoutGameplayTags::FeatureRoom);

	FLayoutRegionContentEntrySolveSnapshot SecondEntry = MakeDemandCompilationChildEntry(
		TEXT("RoomChildB"),
		MakeDemandCompilationChildTemplate(
			TEXT("RoomProfileB"),
			TEXT("RoomContentB"),
			TEXT("RoomModuleB"),
			false,
			false),
		false,
		false);
	SecondEntry.ProvidedZoneFeatures.AddTag(LayoutGameplayTags::FeatureRoom);

	RootRequest.ContentSetSnapshot.Entries = {FirstEntry, SecondEntry};

	const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(BuildSolveContext(RootRequest));

	TestEqual(TEXT("Ambiguous hard feature requirement compiles its exact counted slot count"), StructuralInputs.Demands.Num(), 2);
	TestEqual(TEXT("First counted choice slot uses stable requirement path"), StructuralInputs.Demands[0].ChildRegionDebugPath, FString(TEXT("Root/RoomRequirement_1")));
	TestEqual(TEXT("Second counted choice slot uses stable requirement path"), StructuralInputs.Demands[1].ChildRegionDebugPath, FString(TEXT("Root/RoomRequirement_2")));
	TestTrue(TEXT("Counted choice slots are not authored optional drops"), StructuralInputs.Demands.ContainsByPredicate(
		[](const FNegotiationDemandPlan& Demand)
		{
			return Demand.FeatureFlags.bCountedProviderChoiceSlot
				&& !Demand.FeatureFlags.bOptionalDemand;
		}));
	return true;
}
