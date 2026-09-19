// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutRegionScheduleSolverFacade.h"
#include "Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

namespace
{
	using namespace LayoutRegionScheduleSolverFacade;
	using namespace PorismLayoutTestUtilities;

	FLayoutChildCapabilityEndpoint MakeEndpointOffer(
		const FLayoutId CapabilityId,
		const FIntVector& LocalCell,
		const ELayoutFaceDirection FaceDirection,
		const FGameplayTagContainer& TraversalChannels)
	{
		FLayoutChildCapabilityEndpoint EndpointOffer;
		EndpointOffer.CapabilityId = CapabilityId;
		EndpointOffer.LocalCell = LocalCell;
		EndpointOffer.FaceDirection = FaceDirection;
		EndpointOffer.ConnectionTag = LayoutGameplayTags::FaceOpen;
		EndpointOffer.AllowedConnectionTags = MakeTags({LayoutGameplayTags::FaceOpen});
		EndpointOffer.TraversalChannels = TraversalChannels;
		return EndpointOffer;
	}

	FPlacementCapabilityBundle MakePlacementBundle(
		const TArray<FIntVector>& OccupiedLocalCells,
		const TArray<int32>& CoveredLevels)
	{
		FPlacementCapabilityBundle PlacementBundle;
		PlacementBundle.OccupiedLocalCells = OccupiedLocalCells;
		PlacementBundle.CoveredLevels = CoveredLevels;
		PlacementBundle.RequiredSupportCells = OccupiedLocalCells;
		return PlacementBundle;
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

	TArray<FCommittedParentContactFace> BuildDirectParentContactFaces(
		const TArray<FIntVector>& ParentContactCells,
		const TArray<FLayoutCommittedEndpointAnchor>& EndpointCommitments)
	{
		TArray<FCommittedParentContactFace> ParentContactFaces;
		const int32 PairCount = FMath::Min(
			ParentContactCells.Num(),
			EndpointCommitments.Num());
		for (int32 PairIndex = 0; PairIndex < PairCount; ++PairIndex)
		{
			FCommittedParentContactFace& ParentContactFace =
				ParentContactFaces.AddDefaulted_GetRef();
			ParentContactFace.Cell = ParentContactCells[PairIndex];
			ParentContactFace.FaceDirection =
				FLayoutDirectionUtils::GetOpposite(
					EndpointCommitments[PairIndex].FaceDirection);
		}
		return ParentContactFaces;
	}

	void PopulateDirectParentContactFaces(
		FContactBackedPlacementFamily& InOutContactFamily)
	{
		InOutContactFamily.ParentContactFaces =
			BuildDirectParentContactFaces(
				InOutContactFamily.ParentContactCells,
				InOutContactFamily.EndpointCommitments);
	}

	FLayoutId BuildConnectableAnchorPairId(
		const FLayoutCommittedEndpointAnchor& LeftAnchor,
		const FLayoutCommittedEndpointAnchor& RightAnchor)
	{
		const FLayoutId FirstId = LeftAnchor.CommitmentId.LexicalLess(RightAnchor.CommitmentId)
			? LeftAnchor.CommitmentId
			: RightAnchor.CommitmentId;
		const FLayoutId SecondId = LeftAnchor.CommitmentId.LexicalLess(RightAnchor.CommitmentId)
			? RightAnchor.CommitmentId
			: LeftAnchor.CommitmentId;
		return FLayoutId(*FString::Printf(
			TEXT("%s__%s"),
			*FirstId.ToString(),
			*SecondId.ToString()));
	}

	void PopulateCoarseNegotiatedHostAscentProof(
		FLayoutNegotiatedChildResponsibilityContract& InOutContract,
		const FLayoutCommittedEndpointAnchor& LowerAnchor,
		const FLayoutCommittedEndpointAnchor& UpperAnchor)
	{
		InOutContract.RequiredChildGenerallyConnectableAnchorPairId =
			BuildConnectableAnchorPairId(LowerAnchor, UpperAnchor);
		InOutContract.RequiredChildInternalVerticalSpanLevels = {
			LowerAnchor.LocalCell.Z,
			LowerAnchor.LocalCell.Z + 1,
			UpperAnchor.LocalCell.Z
		};
	}

	void PopulateGenerallyConnectableHostTraversalSummary(
		FChildCapabilitySummary& InOutSummary,
		const FLayoutChildCapabilityEndpoint& LowerEndpoint,
		const FLayoutChildCapabilityEndpoint& UpperEndpoint)
	{
		const FLayoutId AnchorPairId =
			LowerEndpoint.CapabilityId.LexicalLess(UpperEndpoint.CapabilityId)
				? FLayoutId(*FString::Printf(TEXT("%s__%s"), *LowerEndpoint.CapabilityId.ToString(), *UpperEndpoint.CapabilityId.ToString()))
				: FLayoutId(*FString::Printf(TEXT("%s__%s"), *UpperEndpoint.CapabilityId.ToString(), *LowerEndpoint.CapabilityId.ToString()));
		InOutSummary.GenerallyConnectableAnchorPairIds = {AnchorPairId};

		const int32 LowerLevel = FMath::Min(
			LowerEndpoint.LocalCell.Z,
			UpperEndpoint.LocalCell.Z);
		const int32 UpperLevel = FMath::Max(
			LowerEndpoint.LocalCell.Z,
			UpperEndpoint.LocalCell.Z);
		for (int32 Level = LowerLevel; Level <= UpperLevel; ++Level)
		{
			FChildLevelTraversalCapabilitySummary* ExistingSummary =
				InOutSummary.TraversalSummariesByLevel.FindByPredicate(
					[Level](const FChildLevelTraversalCapabilitySummary& Summary)
					{
						return Summary.LevelIndex == Level;
					});
			if (ExistingSummary == nullptr)
			{
				ExistingSummary = &InOutSummary.TraversalSummariesByLevel.AddDefaulted_GetRef();
				ExistingSummary->LevelIndex = Level;
			}

			ExistingSummary->TraversalChannels.AppendTags(LowerEndpoint.TraversalChannels);
			ExistingSummary->TraversalChannels.AppendTags(UpperEndpoint.TraversalChannels);
			ExistingSummary->bCanCarryHostVerticalAccess = true;
			ExistingSummary->ConnectableAnchorPairIds = {AnchorPairId};
			if (Level == LowerEndpoint.LocalCell.Z)
			{
				ExistingSummary->bCanExposeHostIngress = true;
			}
			if (Level == UpperEndpoint.LocalCell.Z)
			{
				ExistingSummary->bCanExposeHostEgress = true;
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationCollectsParentContactCapabilityCandidatesTest,
	"PorismExtension.Layout.Solver.Negotiation.CollectsParentContactCapabilityCandidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationBuildsCapabilityBackedContactFamiliesTest,
	"PorismExtension.Layout.Solver.Negotiation.BuildsCapabilityBackedContactFamilies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationSelectsTraversalIngressSubsetFromResidualParentComponentTest,
	"PorismExtension.Layout.Solver.Negotiation.SelectsTraversalIngressSubsetFromResidualParentComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationBuildsExplicitResponsibilityContractFromContactFamilyTest,
	"PorismExtension.Layout.Solver.Negotiation.BuildsExplicitResponsibilityContractFromContactFamily",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationBuildsOneLevelMultiEntryPlacementFamiliesTest,
	"PorismExtension.Layout.Solver.Negotiation.BuildsOneLevelMultiEntryPlacementFamilies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationSelectsParentOwnedOneLevelMultiEntryStairCommitmentTest,
	"PorismExtension.Layout.Solver.Negotiation.SelectsParentOwnedOneLevelMultiEntryStairCommitment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationBuildsOneLevelExactCountPlacementFamiliesFromEndpointSupersetTest,
	"PorismExtension.Layout.Solver.Negotiation.BuildsOneLevelExactCountPlacementFamiliesFromEndpointSuperset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationBuildsOneLevelRangeCountPlacementFamiliesFromEndpointSupersetTest,
	"PorismExtension.Layout.Solver.Negotiation.BuildsOneLevelRangeCountPlacementFamiliesFromEndpointSuperset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationBuildsMultiLevelAnchorSetPlacementFamiliesTest,
	"PorismExtension.Layout.Solver.Negotiation.BuildsMultiLevelAnchorSetPlacementFamilies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationBuildsCombinedMultiEntryMultiLevelPlacementFamiliesTest,
	"PorismExtension.Layout.Solver.Negotiation.BuildsCombinedMultiEntryMultiLevelPlacementFamilies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationSelectsBestPlacementFamilyForDemandTest,
	"PorismExtension.Layout.Solver.Negotiation.SelectsBestPlacementFamilyForDemand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationSelectsFirstConfirmedPlacementFamilyWhenHigherRankedFamilyFailsTest,
	"PorismExtension.Layout.Solver.Negotiation.SelectsFirstConfirmedPlacementFamilyWhenHigherRankedFamilyFails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationPrefersPlacementFamilyThatSatisfiesRequiredHostProviderCountTest,
	"PorismExtension.Layout.Solver.Negotiation.PrefersPlacementFamilyThatSatisfiesRequiredHostProviderCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationPrefersPlacementFamilyWithMoreCountedParentProvidersWhenMultipleSatisfyRequiredHostProviderCountTest,
	"PorismExtension.Layout.Solver.Negotiation.PrefersPlacementFamilyWithMoreCountedParentProvidersWhenMultipleSatisfyRequiredHostProviderCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationPrefersPlacementFamilyOnParentRouteSeedComponentTest,
	"PorismExtension.Layout.Solver.Negotiation.PrefersPlacementFamilyOnParentRouteSeedComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationPrefersPlacementFamilyOnRootExternalEndpointComponentTest,
	"PorismExtension.Layout.Solver.Negotiation.PrefersPlacementFamilyOnRootExternalEndpointComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationPrefersPlacementFamilyThatPreservesReachableRootExternalEndpointsTest,
	"PorismExtension.Layout.Solver.Negotiation.PrefersPlacementFamilyThatPreservesReachableRootExternalEndpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationBuildsProofReadyChildRequestOnSuccessTest,
	"PorismExtension.Layout.Solver.Negotiation.BuildsProofReadyChildRequestOnSuccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationFailsWhenNoPlacementFamilySurvivesTest,
	"PorismExtension.Layout.Solver.Negotiation.FailsWhenNoPlacementFamilySurvives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationConfirmsCoherentResponsibilitySetTest,
	"PorismExtension.Layout.Solver.Negotiation.ConfirmsCoherentResponsibilitySet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationBuildsBoundedParentProofEvidenceOnConfirmedResponsibilitySetTest,
	"PorismExtension.Layout.Solver.Negotiation.BuildsBoundedParentProofEvidenceOnConfirmedResponsibilitySet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsResponsibilitySetWhenChildBundleSupportIsMissingTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsResponsibilitySetWhenChildBundleSupportIsMissing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsResponsibilitySetWhenRootExternalEndpointBecomesUnreachableTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsResponsibilitySetWhenRootExternalEndpointBecomesUnreachable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsResponsibilitySetWhenRootExternalEndpointReachabilityClassificationIsMissingTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsResponsibilitySetWhenRootExternalEndpointReachabilityClassificationIsMissing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsResponsibilitySetWhenRootExternalEndpointSurvivesOnDifferentComponentTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsResponsibilitySetWhenRootExternalEndpointSurvivesOnDifferentComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsResponsibilitySetWithWrongParentComponentTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsResponsibilitySetWithWrongParentComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsResponsibilitySetWithParentContactFaceMissingFromResidualParentTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsResponsibilitySetWithParentContactFaceMissingFromResidualParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsResponsibilitySetOutsideParentRouteSeedComponentTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsResponsibilitySetOutsideParentRouteSeedComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsResponsibilitySetOutsideRootExternalEndpointComponentTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsResponsibilitySetOutsideRootExternalEndpointComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsResponsibilitySetWithCountedParentProviderOutsideSelectedComponentTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsResponsibilitySetWithCountedParentProviderOutsideSelectedComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsResponsibilitySetWithEndpointOutsidePlacementBundleTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsResponsibilitySetWithEndpointOutsidePlacementBundle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsResponsibilitySetWithReplacementVolumeCountMismatchTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsResponsibilitySetWithReplacementVolumeCountMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsResponsibilitySetWithReplacementVolumeOverlappingRetainedShellTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsResponsibilitySetWithReplacementVolumeOverlappingRetainedShell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationSelectsChildOwnedHostAscentForMultiLevelDemandTest,
	"PorismExtension.Layout.Solver.Negotiation.SelectsChildOwnedHostAscentForMultiLevelDemand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationSelectsComposedHostAscentForMultiLevelDemandTest,
	"PorismExtension.Layout.Solver.Negotiation.SelectsComposedHostAscentForMultiLevelDemand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationTreatsSingleLowerParentHandoffAsChildOwnedExactSingleTest,
	"PorismExtension.Layout.Solver.Negotiation.TreatsSingleLowerParentHandoffAsChildOwnedExactSingle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationTreatsSingleLowerParentRouteHandoffAsChildOwnedExactSingleTest,
	"PorismExtension.Layout.Solver.Negotiation.TreatsSingleLowerParentRouteHandoffAsChildOwnedExactSingle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationKeepsExactSingleUpperRouteSupportProviderComposedTest,
	"PorismExtension.Layout.Solver.Negotiation.KeepsExactSingleUpperRouteSupportProviderComposed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationUsesRouteSupportFallbackForParentOwnedHostAscentTest,
	"PorismExtension.Layout.Solver.Negotiation.UsesRouteSupportFallbackForParentOwnedHostAscent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationDisplacedCountedParentProviderYieldsChildOwnedHostAscentTest,
	"PorismExtension.Layout.Solver.Negotiation.DisplacedCountedParentProviderYieldsChildOwnedHostAscent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationFailsWhenNoPlacementFamilyMeetsRequiredHostProviderCountTest,
	"PorismExtension.Layout.Solver.Negotiation.FailsWhenNoPlacementFamilyMeetsRequiredHostProviderCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsChildOwnedResponsibilitySetWithoutChildRouteTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsChildOwnedResponsibilitySetWithoutCoarseAnchorPairProof",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationSelectsChildOwnedHostAscentFromRouteBackedContactsWithoutAnchorPairProofTest,
	"PorismExtension.Layout.Solver.Negotiation.SelectsChildOwnedHostAscentFromRouteBackedContactsWithoutAnchorPairProof",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsChildOwnedResponsibilitySetWithoutIntermediateRouteLevelCoverageTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsChildOwnedResponsibilitySetWithoutIntermediateAnchorPairSpanCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsComposedResponsibilitySetWithoutCountedParentProvidersTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsComposedResponsibilitySetWithoutCountedParentProviders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsResponsibilitySetWithCountedParentProviderOutsideResidualSummaryTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsResponsibilitySetWithCountedParentProviderOutsideResidualSummary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsChildOwnedResponsibilitySetWithHostAnchorOutsideCommittedEndpointsTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsChildOwnedResponsibilitySetWithHostAnchorOutsideCommittedEndpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationRejectsChildOwnedResponsibilitySetWithRouteOutsidePlacementBundleTest,
	"PorismExtension.Layout.Solver.Negotiation.RejectsChildOwnedResponsibilitySetWithRouteOutsidePlacementBundle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResponsibilityNegotiationBuildsFallbackRequiredHostProviderCountFromParentPlanTest,
	"PorismExtension.Layout.Solver.Negotiation.BuildsFallbackRequiredHostProviderCountFromParentPlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutResponsibilityNegotiationCollectsParentContactCapabilityCandidatesTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FChildCapabilitySummary ChildSummary;
	FChildLevelEndpointCapabilitySummary& GroundEndpoints = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	GroundEndpoints.LevelIndex = 0;
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("GroundDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));

	FChildLevelEndpointCapabilitySummary& UpperEndpoints = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpoints.LevelIndex = 1;
	UpperEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 1), ELayoutFaceDirection::PosY, PrimaryTraversal));

	ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0)}, {0}));
	ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 1)}, {1}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(4, 1, 0),
		FIntVector(6, 1, 1),
		FIntVector(9, 9, 0)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(4, 1, 0),
		{ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosY});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(6, 1, 1),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(9, 9, 0),
		{ELayoutFaceDirection::PosX});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(4, 1, 0), 10);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(6, 1, 1), 11);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(9, 9, 0), 99);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(4, 1, 0)};
	ParentSummary.SeamRelevantBoundaryCells = {FIntVector(4, 1, 0)};
	FResidualBoundarySpanClassification& BoundaryClassification =
		ParentSummary.BoundarySpanClassifications.AddDefaulted_GetRef();
	BoundaryClassification.LevelIndex = 0;
	BoundaryClassification.Cells = {FIntVector(4, 1, 0)};
	BoundaryClassification.AdjacencyClassId = TEXT("BoundaryDoor");

	const TArray<FParentContactCapabilityCandidate> Candidates =
		CollectParentContactCapabilityCandidates(ChildSummary, ParentSummary);
	if (!TestEqual(TEXT("Only level-aware opposite-face residual parent contacts become capability candidates"), Candidates.Num(), 2))
	{
		return false;
	}

	const FParentContactCapabilityCandidate* GroundCandidate =
		Candidates.FindByPredicate([](const FParentContactCapabilityCandidate& Candidate)
		{
			return Candidate.ParentContactCell == FIntVector(4, 1, 0);
		});
	const FParentContactCapabilityCandidate* UpperCandidate =
		Candidates.FindByPredicate([](const FParentContactCapabilityCandidate& Candidate)
		{
			return Candidate.ParentContactCell == FIntVector(6, 1, 1);
		});
	if (!TestNotNull(TEXT("Ground-level candidate is present"), GroundCandidate)
		|| !TestNotNull(TEXT("Upper-level candidate is present"), UpperCandidate))
	{
		return false;
	}

	TestEqual(TEXT("Ground candidate keeps the child-local level index"), GroundCandidate->LevelIndex, 0);
	TestEqual(TEXT("Ground candidate keeps the residual parent component id"), GroundCandidate->ParentComponentId, 10);
	TestEqual(TEXT("Ground candidate keeps the boundary adjacency class"), GroundCandidate->AdjacencyClassId, FLayoutId(TEXT("BoundaryDoor")));
	TestEqual(TEXT("Ground candidate keeps the child capability id as its commitment id"), GroundCandidate->EndpointCommitment.CommitmentId, FLayoutId(TEXT("GroundDoor")));
	TestEqual(TEXT("Ground candidate keeps the exact required parent contact face direction"), GroundCandidate->ParentContactFaceDirection, ELayoutFaceDirection::NegX);
	TestTrue(TEXT("Ground candidate keeps a parent traversal anchor for the child contact"), GroundCandidate->AllCommittedContactTraversalAnchors.Num() == 1);
	TestTrue(TEXT("Ground candidate support score rewards counted parent providers"), GroundCandidate->ParentSupportScore > UpperCandidate->ParentSupportScore);

	TestEqual(TEXT("Upper candidate keeps the child-local level index"), UpperCandidate->LevelIndex, 1);
	TestEqual(TEXT("Upper candidate keeps the residual parent component id"), UpperCandidate->ParentComponentId, 11);
	TestEqual(TEXT("Upper candidate falls back to the interior adjacency class"), UpperCandidate->AdjacencyClassId, FLayoutId(TEXT("Interior")));
	TestEqual(TEXT("Upper candidate keeps the child capability id as its commitment id"), UpperCandidate->EndpointCommitment.CommitmentId, FLayoutId(TEXT("UpperDoor")));
	TestEqual(TEXT("Upper candidate keeps the exact required parent contact face direction"), UpperCandidate->ParentContactFaceDirection, ELayoutFaceDirection::NegY);
	return true;
}

bool FLayoutResponsibilityNegotiationBuildsCapabilityBackedContactFamiliesTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FChildCapabilitySummary ChildSummary;
	ChildSummary.GenerallyConnectableAnchorPairIds = {TEXT("LowerUpperPair")};
	FChildLevelEndpointCapabilitySummary& GroundEndpoints = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	GroundEndpoints.LevelIndex = 0;
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("SharedDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("SharedDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));
	ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0)}, {0}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {FIntVector(3, 0, 0)};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 4);

	const TArray<FContactBackedPlacementFamily> ContactFamilies =
		CollectCapabilityBackedContactSets(ChildSummary, ParentSummary);
	if (!TestEqual(TEXT("Duplicate capability candidates normalize into one contact family"), ContactFamilies.Num(), 1))
	{
		return false;
	}

	const FContactBackedPlacementFamily& ContactFamily = ContactFamilies[0];
	TestTrue(TEXT("Contact family keeps child-bridge allowance when connectable anchor pairs exist"), ContactFamily.bAllowsChildTraversalBridge);
	TestEqual(TEXT("Normalized contact family keeps one committed endpoint"), ContactFamily.EndpointCommitments.Num(), 1);
	TestEqual(TEXT("Normalized contact family keeps one parent contact cell"), ContactFamily.ParentContactCells.Num(), 1);
	TestEqual(TEXT("Normalized contact family keeps one exact parent contact face"), ContactFamily.ParentContactFaces.Num(), 1);
	TestEqual(TEXT("Normalized contact family keeps one committed traversal anchor"), ContactFamily.AllCommittedContactTraversalAnchors.Num(), 1);
	TestEqual(TEXT("Normalized contact family keeps one parent-ingress traversal anchor"), ContactFamily.ParentTraversalIngressAnchors.Num(), 1);
	TestEqual(TEXT("Normalized contact family keeps the residual parent component id"), ContactFamily.ParentComponentId, 4);
	TestEqual(TEXT("Normalized contact family keeps the committed endpoint id"), ContactFamily.EndpointCommitments[0].CommitmentId, FLayoutId(TEXT("SharedDoor")));
	TestEqual(TEXT("Normalized contact family keeps the exact required parent contact face direction"), ContactFamily.ParentContactFaces[0].FaceDirection, ELayoutFaceDirection::NegX);
	return true;
}

bool FLayoutResponsibilityNegotiationSelectsTraversalIngressSubsetFromResidualParentComponentTest::RunTest(const FString& Parameters)
{
	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(1, 1, 0),
		FIntVector(3, 3, 0),
		FIntVector(2, 1, 0)
	};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(1, 1, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 3, 0), 9);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(2, 1, 0), 7);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(1, 1, 0)};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 7;

	FLayoutCommittedTraversalAnchor& PreferredPrimary = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	PreferredPrimary.Cell = FIntVector(1, 1, 0);
	PreferredPrimary.TraversalChannel = LayoutGameplayTags::TraversalPrimary;

	FLayoutCommittedTraversalAnchor& WrongComponentPrimary = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	WrongComponentPrimary.Cell = FIntVector(3, 3, 0);
	WrongComponentPrimary.TraversalChannel = LayoutGameplayTags::TraversalPrimary;

	FLayoutCommittedTraversalAnchor& Secondary = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	Secondary.Cell = FIntVector(2, 1, 0);
	Secondary.TraversalChannel = LayoutGameplayTags::TraversalSecondary;

	SelectParentTraversalIngressSubset(ParentSummary, ContactFamily);
	if (!TestEqual(TEXT("Ingress subset keeps one anchor per traversal channel on the selected residual parent component"), ContactFamily.ParentTraversalIngressAnchors.Num(), 2))
	{
		return false;
	}

	TestTrue(TEXT("Primary ingress anchor stays on the chosen residual component"), ContactFamily.ParentTraversalIngressAnchors.ContainsByPredicate(
		[](const FLayoutCommittedTraversalAnchor& TraversalAnchor)
		{
			return TraversalAnchor.Cell == FIntVector(1, 1, 0)
				&& TraversalAnchor.TraversalChannel == LayoutGameplayTags::TraversalPrimary;
		}));
	TestFalse(TEXT("Primary ingress subset drops the wrong-component traversal anchor"), ContactFamily.ParentTraversalIngressAnchors.ContainsByPredicate(
		[](const FLayoutCommittedTraversalAnchor& TraversalAnchor)
		{
			return TraversalAnchor.Cell == FIntVector(3, 3, 0)
				&& TraversalAnchor.TraversalChannel == LayoutGameplayTags::TraversalPrimary;
		}));
	TestTrue(TEXT("Secondary traversal channel remains represented in the parent-ingress subset"), ContactFamily.ParentTraversalIngressAnchors.ContainsByPredicate(
		[](const FLayoutCommittedTraversalAnchor& TraversalAnchor)
		{
			return TraversalAnchor.Cell == FIntVector(2, 1, 0)
				&& TraversalAnchor.TraversalChannel == LayoutGameplayTags::TraversalSecondary;
		}));
	return true;
}

bool FLayoutResponsibilityNegotiationBuildsExplicitResponsibilityContractFromContactFamilyTest::RunTest(const FString& Parameters)
{
	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCount = 2;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(5, 1, 1),
		FIntVector(4, 1, 0)
	};

	FResidualParentCapabilitySummary ParentSummary;

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentContactCells = {
		FIntVector(3, 1, 0),
		FIntVector(6, 2, 1),
		FIntVector(3, 1, 0)
	};

	FLayoutCommittedEndpointAnchor& UpperEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	UpperEndpoint.CommitmentId = TEXT("UpperDoor");
	UpperEndpoint.LocalCell = FIntVector(0, 0, 1);
	UpperEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	UpperEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	UpperEndpoint.TraversalChannels = MakeTags({LayoutGameplayTags::TraversalSecondary});

	FLayoutCommittedEndpointAnchor& LowerEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	LowerEndpoint.CommitmentId = TEXT("LowerDoor");
	LowerEndpoint.LocalCell = FIntVector(0, 0, 0);
	LowerEndpoint.FaceDirection = ELayoutFaceDirection::NegX;
	LowerEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	LowerEndpoint.TraversalChannels = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FLayoutCommittedTraversalAnchor& SecondaryTraversal = ContactFamily.ParentTraversalIngressAnchors.AddDefaulted_GetRef();
	SecondaryTraversal.Cell = FIntVector(6, 2, 1);
	SecondaryTraversal.TraversalChannel = LayoutGameplayTags::TraversalSecondary;

	FLayoutCommittedTraversalAnchor& PrimaryTraversal = ContactFamily.ParentTraversalIngressAnchors.AddDefaulted_GetRef();
	PrimaryTraversal.Cell = FIntVector(3, 1, 0);
	PrimaryTraversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;

	const FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);
	const FLayoutNegotiatedChildResponsibilityContract& Contract =
		ResponsibilitySet.ResponsibilityContract;

	TestEqual(TEXT("Responsibility contract keeps the parent region path"), Contract.ParentRegionDebugPath, FString(TEXT("Root")));
	TestEqual(TEXT("Responsibility contract keeps the child region path"), Contract.ChildRegionDebugPath, FString(TEXT("Root/ChildA")));
	TestEqual(TEXT("Responsibility contract preserves the explicit required host-provider count from the root solve context"), Contract.RequiredHostProviderCount, 2);
	TestEqual(TEXT("Responsibility contract stays parent-owned until full host-access negotiation lands"), Contract.HostVerticalAccessResponsibility, ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned);
	TestEqual(TEXT("Responsibility set keeps the original contact family"), ResponsibilitySet.ContactSet.ParentContactCells.Num(), 3);

	if (!TestEqual(TEXT("Replacement volume groups reserved parent cells by level"), Contract.ReplacementVolumeByLevel.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("Lower replacement level is preserved"), Contract.ReplacementVolumeByLevel[0].Level, 0);
	TestEqual(TEXT("Lower replacement cells are sorted and deduped"), Contract.ReplacementVolumeByLevel[0].Cells.Num(), 1);
	TestEqual(TEXT("Lower replacement cell is kept exactly"), Contract.ReplacementVolumeByLevel[0].Cells[0], FIntVector(4, 1, 0));
	TestEqual(TEXT("Upper replacement level is preserved"), Contract.ReplacementVolumeByLevel[1].Level, 1);
	TestEqual(TEXT("Upper replacement cell is kept exactly"), Contract.ReplacementVolumeByLevel[1].Cells[0], FIntVector(5, 1, 1));

	if (!TestEqual(TEXT("Retained parent shell groups parent contact cells by level"), Contract.RetainedParentShellCellsByLevel.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("Lower retained shell cell is sorted and deduped"), Contract.RetainedParentShellCellsByLevel[0].Cells[0], FIntVector(3, 1, 0));
	TestEqual(TEXT("Upper retained shell cell is kept exactly"), Contract.RetainedParentShellCellsByLevel[1].Cells[0], FIntVector(6, 2, 1));

	if (!TestEqual(TEXT("Committed interface contract groups endpoint and traversal anchors by covered level"), Contract.CommittedParentChildInterfacesByLevel.Num(), 2))
	{
		return false;
	}

	const FLayoutNegotiatedLevelInterfaceContract& LowerInterface = Contract.CommittedParentChildInterfacesByLevel[0];
	const FLayoutNegotiatedLevelInterfaceContract& UpperInterface = Contract.CommittedParentChildInterfacesByLevel[1];
	TestEqual(TEXT("Lower interface level is preserved"), LowerInterface.Level, 0);
	TestEqual(TEXT("Upper interface level is preserved"), UpperInterface.Level, 1);
	TestEqual(TEXT("Lower interface keeps the lower endpoint commitment"), LowerInterface.EndpointAnchors[0].CommitmentId, FLayoutId(TEXT("LowerDoor")));
	TestEqual(TEXT("Upper interface keeps the upper endpoint commitment"), UpperInterface.EndpointAnchors[0].CommitmentId, FLayoutId(TEXT("UpperDoor")));
	TestEqual(TEXT("Lower interface keeps the chosen parent ingress traversal anchor"), LowerInterface.TraversalAnchors[0].Cell, FIntVector(3, 1, 0));
	TestEqual(TEXT("Upper interface keeps the chosen parent ingress traversal anchor"), UpperInterface.TraversalAnchors[0].Cell, FIntVector(6, 2, 1));

	TestFalse(TEXT("Parent-owned contract does not fabricate a host ingress anchor yet"), Contract.bHasRequiredHostIngressAnchor);
	TestFalse(TEXT("Parent-owned contract does not fabricate a host egress anchor yet"), Contract.bHasRequiredHostEgressAnchor);
	TestEqual(TEXT("Parent-owned contract leaves counted parent providers unset in this helper slice"), Contract.CountedParentProviderCount, 0);
	TestTrue(TEXT("Parent-owned contract leaves counted parent provider cells empty in this helper slice"), Contract.CountedParentVerticalAccessCells.IsEmpty());
	return true;
}

bool FLayoutResponsibilityNegotiationBuildsOneLevelMultiEntryPlacementFamiliesTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildMultiEntry");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ChildRequest.ProfileSnapshot.EntryCountMode =
		ELayoutCountConstraintMode::Exact;
	DemandPlan.ChildRequest.ProfileSnapshot.EntryCount = 2;
	DemandPlan.FeatureFlags.bRequiresMultiEntryContactSet = true;

	FChildLevelEndpointCapabilitySummary& GroundEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	GroundEndpoints.LevelIndex = 0;
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LeftDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("RightDoor"), FIntVector(1, 0, 0), ELayoutFaceDirection::PosY, PrimaryTraversal));

	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0), FIntVector(1, 0, 0)}, {0}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(4, 1, 0),
		FIntVector(7, 7, 0)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(4, 1, 0),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(7, 7, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 5);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(4, 1, 0), 5);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(7, 7, 0), 99);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(3, 0, 0)};

	const TArray<FContactBackedPlacementFamily> ContactFamilies =
		BuildNegotiatedPlacementFamilies(DemandPlan, ParentSummary);
	if (!TestEqual(TEXT("One-level multi-entry capability families are resolved as explicit contact sets"), ContactFamilies.Num(), 1))
	{
		return false;
	}

	const FContactBackedPlacementFamily* RootConnectedFamily =
		ContactFamilies.FindByPredicate([](const FContactBackedPlacementFamily& ContactFamily)
		{
			return ContactFamily.ParentComponentId == 5;
		});
	if (!TestNotNull(TEXT("Root-connected multi-entry family is present"), RootConnectedFamily))
	{
		return false;
	}

	TestEqual(TEXT("Multi-entry family keeps both endpoint commitments together"), RootConnectedFamily->EndpointCommitments.Num(), 2);
	TestEqual(TEXT("Multi-entry family keeps both parent contact cells together"), RootConnectedFamily->ParentContactCells.Num(), 2);
	TestEqual(TEXT("Multi-entry family keeps both committed traversal anchors"), RootConnectedFamily->AllCommittedContactTraversalAnchors.Num(), 2);
	TestEqual(TEXT("Multi-entry family keeps a smaller parent-ingress subset when both contacts share one traversal channel"), RootConnectedFamily->ParentTraversalIngressAnchors.Num(), 1);
	TestTrue(TEXT("Ingress subset prefers the counted parent provider on the shared traversal channel"), RootConnectedFamily->ParentTraversalIngressAnchors.ContainsByPredicate(
		[](const FLayoutCommittedTraversalAnchor& TraversalAnchor)
		{
			return TraversalAnchor.Cell == FIntVector(3, 0, 0)
				&& TraversalAnchor.TraversalChannel == LayoutGameplayTags::TraversalPrimary;
		}));
	TestTrue(TEXT("Multi-entry family preserves the left endpoint commitment"), RootConnectedFamily->EndpointCommitments.ContainsByPredicate(
		[](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
		{
			return EndpointCommitment.CommitmentId == FLayoutId(TEXT("LeftDoor"));
		}));
	TestTrue(TEXT("Multi-entry family preserves the right endpoint commitment"), RootConnectedFamily->EndpointCommitments.ContainsByPredicate(
		[](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
		{
			return EndpointCommitment.CommitmentId == FLayoutId(TEXT("RightDoor"));
		}));
	return true;
}

bool FLayoutResponsibilityNegotiationSelectsParentOwnedOneLevelMultiEntryStairCommitmentTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	SolveContext.RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(3, 0, 0), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(4, 0, 0), ELayoutCellIntent::Connector),
		MakePlannedCell(FIntVector(5, 1, 0), ELayoutCellIntent::Connector)
	};
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCountMode =
		ELayoutCountConstraintMode::Exact;
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCount = 1;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ParentOwnedMultiEntryChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/ParentOwnedMultiEntryChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ChildRequest.ProfileSnapshot.EntryCountMode =
		ELayoutCountConstraintMode::Exact;
	DemandPlan.ChildRequest.ProfileSnapshot.EntryCount = 2;
	DemandPlan.FeatureFlags.bRequiresMultiEntryContactSet = true;

	FChildLevelEndpointCapabilitySummary& GroundEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	GroundEndpoints.LevelIndex = 0;
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LowerStairDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("UpperLandingDoor"), FIntVector(1, 0, 0), ELayoutFaceDirection::PosY, PrimaryTraversal));

	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0), FIntVector(1, 0, 0)}, {0}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(5, 1, 0)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(5, 1, 0),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 11);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(5, 1, 0), 11);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(3, 0, 0)};

	DemandPlan.ChildRequest.SourceContentEntryId = TEXT("NegotiatedChild");
	FNegotiatedDemandResult Result =
		NegotiateDemandResponsibilities(SolveContext, DemandPlan, ParentSummary);
	if (!TestTrue(TEXT("One-level multi-entry parent-owned stair demand negotiates successfully"), Result.bSucceeded))
	{
		AddError(Result.FailureReason);
		return false;
	}

	FLayoutNegotiatedChildResponsibilityContract& Contract =
		Result.ResponsibilitySet.ResponsibilityContract;
	TestEqual(TEXT("Negotiation keeps the one-level multi-entry stair demand parent-owned"), Contract.HostVerticalAccessResponsibility, ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned);
	TestEqual(TEXT("Negotiation keeps both entry commitments on the selected parent-owned stair family"), Result.ResponsibilitySet.ContactSet.EndpointCommitments.Num(), 2);
	TestEqual(TEXT("Negotiation keeps both parent contact cells on the selected parent-owned stair family"), Result.ResponsibilitySet.ContactSet.ParentContactCells.Num(), 2);
	TestEqual(TEXT("Negotiation keeps the stair component counted-parent provider count"), Contract.CountedParentProviderCount, 1);
	if (!TestEqual(TEXT("Negotiation keeps one stair component counted-parent provider cell"), Contract.CountedParentVerticalAccessCells.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Negotiation keeps the stair component counted-parent provider cell"), Contract.CountedParentVerticalAccessCells[0], FIntVector(3, 0, 0));
	TestTrue(TEXT("Negotiation keeps the lower stair contact commitment"), Result.ResponsibilitySet.ContactSet.EndpointCommitments.ContainsByPredicate(
		[](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
		{
			return EndpointCommitment.CommitmentId == FLayoutId(TEXT("LowerStairDoor"));
		}));
	TestTrue(TEXT("Negotiation keeps the upper landing contact commitment"), Result.ResponsibilitySet.ContactSet.EndpointCommitments.ContainsByPredicate(
		[](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
		{
			return EndpointCommitment.CommitmentId == FLayoutId(TEXT("UpperLandingDoor"));
		}));
	TestEqual(TEXT("Negotiation keeps one parent traversal-ingress anchor on the shared stair traversal channel"), Result.ResponsibilitySet.ContactSet.ParentTraversalIngressAnchors.Num(), 1);
	TestFalse(TEXT("Parent-owned multi-entry stair negotiation does not fabricate a child-owned host ingress anchor"), Contract.bHasRequiredHostIngressAnchor);
	TestFalse(TEXT("Parent-owned multi-entry stair negotiation does not fabricate a child-owned host egress anchor"), Contract.bHasRequiredHostEgressAnchor);

	FString ConfirmationFailureReason;
	if (!TestTrue(
		TEXT("Confirmed parent-owned stair contract still satisfies bounded responsibility confirmation"),
		ConfirmNegotiatedResponsibilitySet(
			SolveContext.RootRequest,
			ParentSummary,
			DemandPlan.ChildSummary,
			Result.ResponsibilitySet,
			ConfirmationFailureReason)))
	{
		AddError(ConfirmationFailureReason);
		return false;
	}

	return true;
}

bool FLayoutResponsibilityNegotiationBuildsOneLevelExactCountPlacementFamiliesFromEndpointSupersetTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildMultiEntrySuperset");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ChildRequest.ProfileSnapshot.EntryCountMode =
		ELayoutCountConstraintMode::Exact;
	DemandPlan.ChildRequest.ProfileSnapshot.EntryCount = 2;
	DemandPlan.FeatureFlags.bRequiresMultiEntryContactSet = true;

	FChildLevelEndpointCapabilitySummary& GroundEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	GroundEndpoints.LevelIndex = 0;
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LeftDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("MiddleDoor"), FIntVector(1, 0, 0), ELayoutFaceDirection::PosY, PrimaryTraversal));
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("RightDoor"), FIntVector(2, 0, 0), ELayoutFaceDirection::NegY, PrimaryTraversal));

	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(2, 0, 0)}, {0}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(4, 1, 0),
		FIntVector(5, 0, 0)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(4, 1, 0),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(5, 0, 0),
		{ELayoutFaceDirection::PosY});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 5);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(4, 1, 0), 5);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(5, 0, 0), 5);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(3, 0, 0)};

	const TArray<FContactBackedPlacementFamily> ContactFamilies =
		BuildNegotiatedPlacementFamilies(DemandPlan, ParentSummary);
	if (!TestEqual(TEXT("Exact-count multi-entry negotiation resolves every two-of-three endpoint subset"), ContactFamilies.Num(), 3))
	{
		return false;
	}

	for (const FContactBackedPlacementFamily& ContactFamily : ContactFamilies)
	{
		TestEqual(TEXT("Each exact-count subset family keeps exactly two endpoint commitments"), ContactFamily.EndpointCommitments.Num(), 2);
		TestEqual(TEXT("Each exact-count subset family keeps exactly two parent contact cells"), ContactFamily.ParentContactCells.Num(), 2);
		TestEqual(TEXT("Each exact-count subset family keeps a smaller parent-ingress subset on the shared traversal channel"), ContactFamily.ParentTraversalIngressAnchors.Num(), 1);
	}

	TestTrue(TEXT("One exact-count family keeps LeftDoor plus MiddleDoor"), ContactFamilies.ContainsByPredicate(
		[](const FContactBackedPlacementFamily& ContactFamily)
		{
			return ContactFamily.EndpointCommitments.Num() == 2
				&& ContactFamily.EndpointCommitments.ContainsByPredicate([](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
				{
					return EndpointCommitment.CommitmentId == FLayoutId(TEXT("LeftDoor"));
				})
				&& ContactFamily.EndpointCommitments.ContainsByPredicate([](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
				{
					return EndpointCommitment.CommitmentId == FLayoutId(TEXT("MiddleDoor"));
				});
		}));
	TestTrue(TEXT("One exact-count family keeps LeftDoor plus RightDoor"), ContactFamilies.ContainsByPredicate(
		[](const FContactBackedPlacementFamily& ContactFamily)
		{
			return ContactFamily.EndpointCommitments.Num() == 2
				&& ContactFamily.EndpointCommitments.ContainsByPredicate([](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
				{
					return EndpointCommitment.CommitmentId == FLayoutId(TEXT("LeftDoor"));
				})
				&& ContactFamily.EndpointCommitments.ContainsByPredicate([](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
				{
					return EndpointCommitment.CommitmentId == FLayoutId(TEXT("RightDoor"));
				});
		}));
	TestTrue(TEXT("One exact-count family keeps MiddleDoor plus RightDoor"), ContactFamilies.ContainsByPredicate(
		[](const FContactBackedPlacementFamily& ContactFamily)
		{
			return ContactFamily.EndpointCommitments.Num() == 2
				&& ContactFamily.EndpointCommitments.ContainsByPredicate([](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
				{
					return EndpointCommitment.CommitmentId == FLayoutId(TEXT("MiddleDoor"));
				})
				&& ContactFamily.EndpointCommitments.ContainsByPredicate([](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
				{
					return EndpointCommitment.CommitmentId == FLayoutId(TEXT("RightDoor"));
				});
		}));
	return true;
}

bool FLayoutResponsibilityNegotiationBuildsOneLevelRangeCountPlacementFamiliesFromEndpointSupersetTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildRangeMultiEntrySuperset");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ChildRequest.ProfileSnapshot.EntryCountMode =
		ELayoutCountConstraintMode::Range;
	DemandPlan.ChildRequest.ProfileSnapshot.MinEntryCount = 1;
	DemandPlan.ChildRequest.ProfileSnapshot.MaxEntryCount = 2;
	DemandPlan.FeatureFlags.bRequiresMultiEntryContactSet = true;

	FChildLevelEndpointCapabilitySummary& GroundEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	GroundEndpoints.LevelIndex = 0;
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LeftDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("MiddleDoor"), FIntVector(1, 0, 0), ELayoutFaceDirection::PosY, PrimaryTraversal));
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("RightDoor"), FIntVector(2, 0, 0), ELayoutFaceDirection::NegY, PrimaryTraversal));

	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(2, 0, 0)}, {0}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(4, 1, 0),
		FIntVector(5, 0, 0)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(4, 1, 0),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(5, 0, 0),
		{ELayoutFaceDirection::PosY});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 5);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(4, 1, 0), 5);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(5, 0, 0), 5);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(3, 0, 0)};

	const TArray<FContactBackedPlacementFamily> ContactFamilies =
		BuildNegotiatedPlacementFamilies(DemandPlan, ParentSummary);
	if (!TestEqual(TEXT("Range-count multi-entry negotiation resolves both one-of-three and two-of-three endpoint subsets"), ContactFamilies.Num(), 6))
	{
		return false;
	}

	int32 OneEntryFamilyCount = 0;
	int32 TwoEntryFamilyCount = 0;
	for (const FContactBackedPlacementFamily& ContactFamily : ContactFamilies)
	{
		if (ContactFamily.EndpointCommitments.Num() == 1)
		{
			++OneEntryFamilyCount;
		}
		else if (ContactFamily.EndpointCommitments.Num() == 2)
		{
			++TwoEntryFamilyCount;
		}
	}

	TestEqual(TEXT("Range-count negotiation yields three one-entry families"), OneEntryFamilyCount, 3);
	TestEqual(TEXT("Range-count negotiation yields three two-entry families"), TwoEntryFamilyCount, 3);

	TestTrue(TEXT("Range-count negotiation includes a single-entry LeftDoor family"), ContactFamilies.ContainsByPredicate(
		[](const FContactBackedPlacementFamily& ContactFamily)
		{
			return ContactFamily.EndpointCommitments.Num() == 1
				&& ContactFamily.EndpointCommitments[0].CommitmentId == FLayoutId(TEXT("LeftDoor"));
		}));
	TestTrue(TEXT("Range-count negotiation includes a single-entry MiddleDoor family"), ContactFamilies.ContainsByPredicate(
		[](const FContactBackedPlacementFamily& ContactFamily)
		{
			return ContactFamily.EndpointCommitments.Num() == 1
				&& ContactFamily.EndpointCommitments[0].CommitmentId == FLayoutId(TEXT("MiddleDoor"));
		}));
	TestTrue(TEXT("Range-count negotiation includes a single-entry RightDoor family"), ContactFamilies.ContainsByPredicate(
		[](const FContactBackedPlacementFamily& ContactFamily)
		{
			return ContactFamily.EndpointCommitments.Num() == 1
				&& ContactFamily.EndpointCommitments[0].CommitmentId == FLayoutId(TEXT("RightDoor"));
		}));
	TestTrue(TEXT("Range-count negotiation still includes the LeftDoor plus MiddleDoor subset"), ContactFamilies.ContainsByPredicate(
		[](const FContactBackedPlacementFamily& ContactFamily)
		{
			return ContactFamily.EndpointCommitments.Num() == 2
				&& ContactFamily.EndpointCommitments.ContainsByPredicate([](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
				{
					return EndpointCommitment.CommitmentId == FLayoutId(TEXT("LeftDoor"));
				})
				&& ContactFamily.EndpointCommitments.ContainsByPredicate([](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
				{
					return EndpointCommitment.CommitmentId == FLayoutId(TEXT("MiddleDoor"));
				});
		}));
	return true;
}

bool FLayoutResponsibilityNegotiationBuildsMultiLevelAnchorSetPlacementFamiliesTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildMultiLevel");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.FeatureFlags.bRequiresMultiLevelAnchorSet = true;
	DemandPlan.FeatureFlags.bAllowsChildTraversalBridge = true;

	FChildLevelEndpointCapabilitySummary& GroundEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	GroundEndpoints.LevelIndex = 0;
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));

	FChildLevelEndpointCapabilitySummary& UpperEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpoints.LevelIndex = 1;
	UpperEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 1), ELayoutFaceDirection::PosY, PrimaryTraversal));

	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0), FIntVector(0, 0, 1)}, {0, 1}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(4, 1, 1)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(4, 1, 1),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(4, 1, 1), 7);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(3, 0, 0)};

	const TArray<FContactBackedPlacementFamily> ContactFamilies =
		BuildNegotiatedPlacementFamilies(DemandPlan, ParentSummary);
	if (!TestEqual(TEXT("Multi-level demands assemble explicit lower/upper anchor-set families"), ContactFamilies.Num(), 1))
	{
		return false;
	}

	const FContactBackedPlacementFamily& ContactFamily = ContactFamilies[0];
	TestTrue(TEXT("Multi-level anchor-set family preserves child-bridge allowance"), ContactFamily.bAllowsChildTraversalBridge);
	TestEqual(TEXT("Multi-level anchor-set family keeps both endpoint commitments"), ContactFamily.EndpointCommitments.Num(), 2);
	TestEqual(TEXT("Multi-level anchor-set family keeps both parent contact cells"), ContactFamily.ParentContactCells.Num(), 2);
	TestEqual(TEXT("Multi-level anchor-set family keeps both committed traversal anchors"), ContactFamily.AllCommittedContactTraversalAnchors.Num(), 2);
	TestEqual(TEXT("Multi-level anchor-set family still keeps a smaller ingress subset on one traversal channel"), ContactFamily.ParentTraversalIngressAnchors.Num(), 1);
	TestTrue(TEXT("Multi-level anchor-set family keeps the lower endpoint"), ContactFamily.EndpointCommitments.ContainsByPredicate(
		[](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
		{
			return EndpointCommitment.CommitmentId == FLayoutId(TEXT("LowerDoor"))
				&& EndpointCommitment.LocalCell.Z == 0;
		}));
	TestTrue(TEXT("Multi-level anchor-set family keeps the upper endpoint"), ContactFamily.EndpointCommitments.ContainsByPredicate(
		[](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
		{
			return EndpointCommitment.CommitmentId == FLayoutId(TEXT("UpperDoor"))
				&& EndpointCommitment.LocalCell.Z == 1;
		}));
	TestTrue(TEXT("Multi-level anchor-set family ingress subset prefers the counted lower provider on the shared channel"), ContactFamily.ParentTraversalIngressAnchors.ContainsByPredicate(
		[](const FLayoutCommittedTraversalAnchor& TraversalAnchor)
		{
			return TraversalAnchor.Cell == FIntVector(3, 0, 0)
				&& TraversalAnchor.TraversalChannel == LayoutGameplayTags::TraversalPrimary;
		}));
	return true;
}

bool FLayoutResponsibilityNegotiationBuildsCombinedMultiEntryMultiLevelPlacementFamiliesTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildMultiEntryMultiLevel");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ChildRequest.ProfileSnapshot.EntryCountMode =
		ELayoutCountConstraintMode::Exact;
	DemandPlan.ChildRequest.ProfileSnapshot.EntryCount = 3;
	DemandPlan.FeatureFlags.bRequiresMultiEntryContactSet = true;
	DemandPlan.FeatureFlags.bRequiresMultiLevelAnchorSet = true;
	DemandPlan.FeatureFlags.bAllowsChildTraversalBridge = true;

	FChildLevelEndpointCapabilitySummary& LowerEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpoints.LevelIndex = 0;
	LowerEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LowerLeftDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));
	LowerEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LowerRightDoor"), FIntVector(1, 0, 0), ELayoutFaceDirection::PosY, PrimaryTraversal));

	FChildLevelEndpointCapabilitySummary& UpperEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpoints.LevelIndex = 2;
	UpperEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 2), ELayoutFaceDirection::PosY, PrimaryTraversal));
	PopulateGenerallyConnectableHostTraversalSummary(
		DemandPlan.ChildSummary,
		LowerEndpoints.EndpointOffers[0],
		UpperEndpoints.EndpointOffers[0]);
	PopulateGenerallyConnectableHostTraversalSummary(
		DemandPlan.ChildSummary,
		LowerEndpoints.EndpointOffers[0],
		UpperEndpoints.EndpointOffers[0]);

	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(0, 0, 2)}, {0, 1, 2}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(4, 1, 0),
		FIntVector(5, 1, 2)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(4, 1, 0),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(5, 1, 2),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(4, 1, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(5, 1, 2), 7);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(3, 0, 0)};

	const TArray<FContactBackedPlacementFamily> ContactFamilies =
		BuildNegotiatedPlacementFamilies(DemandPlan, ParentSummary);
	if (!TestEqual(TEXT("Combined multi-entry and multi-level negotiation resolves one explicit exact-count family across both levels"), ContactFamilies.Num(), 1))
	{
		return false;
	}

	const FContactBackedPlacementFamily& ContactFamily = ContactFamilies[0];
	TestTrue(TEXT("Combined family preserves child-bridge allowance"), ContactFamily.bAllowsChildTraversalBridge);
	TestEqual(TEXT("Combined family keeps all three endpoint commitments together"), ContactFamily.EndpointCommitments.Num(), 3);
	TestEqual(TEXT("Combined family keeps all three parent contact cells together"), ContactFamily.ParentContactCells.Num(), 3);
	TestEqual(TEXT("Combined family keeps all three committed traversal anchors"), ContactFamily.AllCommittedContactTraversalAnchors.Num(), 3);
	TestEqual(TEXT("Combined family still keeps the smaller parent-ingress subset on the shared traversal channel"), ContactFamily.ParentTraversalIngressAnchors.Num(), 1);
	TestTrue(TEXT("Combined family includes a lower-level endpoint commitment"), ContactFamily.EndpointCommitments.ContainsByPredicate(
		[](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
		{
			return EndpointCommitment.LocalCell.Z == 0
				&& EndpointCommitment.CommitmentId == FLayoutId(TEXT("LowerLeftDoor"));
		}));
	TestTrue(TEXT("Combined family includes the second lower-level endpoint commitment"), ContactFamily.EndpointCommitments.ContainsByPredicate(
		[](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
		{
			return EndpointCommitment.LocalCell.Z == 0
				&& EndpointCommitment.CommitmentId == FLayoutId(TEXT("LowerRightDoor"));
		}));
	TestTrue(TEXT("Combined family includes the upper-level endpoint commitment"), ContactFamily.EndpointCommitments.ContainsByPredicate(
		[](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
		{
			return EndpointCommitment.LocalCell.Z == 2
				&& EndpointCommitment.CommitmentId == FLayoutId(TEXT("UpperDoor"));
		}));
	return true;
}

bool FLayoutResponsibilityNegotiationSelectsBestPlacementFamilyForDemandTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	SolveContext.RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Entry)
	};
	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/NegotiatedChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/NegotiatedChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");

	FChildLevelEndpointCapabilitySummary& GroundEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	GroundEndpoints.LevelIndex = 0;
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("SharedDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));
	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0)}, {0}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(8, 0, 0)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(8, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 4);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(8, 0, 0), 9);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(3, 0, 0)};

	DemandPlan.ChildRequest.SourceContentEntryId = TEXT("NegotiatedChild");
	const FNegotiatedDemandResult Result =
		NegotiateDemandResponsibilities(SolveContext, DemandPlan, ParentSummary);
	if (!TestTrue(TEXT("Negotiation succeeds when at least one capability-backed family exists"), Result.bSucceeded))
	{
		return false;
	}

	TestEqual(TEXT("Negotiation keeps the child region path"), Result.ChildRegionDebugPath, FString(TEXT("Root/NegotiatedChild")));
	TestEqual(TEXT("Negotiation keeps the frozen child request"), Result.ChildRequest.RegionDebugPath, FString(TEXT("Root/NegotiatedChild")));
	TestEqual(TEXT("Negotiation keeps the residual parent summary"), Result.ParentSummary.RootConnectedTraversableCells.Num(), 2);
	TestEqual(TEXT("Negotiation selects one committed endpoint family"), Result.ResponsibilitySet.ContactSet.EndpointCommitments.Num(), 1);
	TestEqual(TEXT("Negotiation prefers the family tied to the counted parent provider"), Result.ResponsibilitySet.ContactSet.ParentContactCells[0], FIntVector(3, 0, 0));
	TestEqual(TEXT("Negotiation materializes the matching retained parent shell cell"), Result.ResponsibilitySet.ResponsibilityContract.RetainedParentShellCellsByLevel[0].Cells[0], FIntVector(3, 0, 0));
	return true;
}

bool FLayoutResponsibilityNegotiationSelectsFirstConfirmedPlacementFamilyWhenHigherRankedFamilyFailsTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	SolveContext.RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(3, 0, 0), ELayoutCellIntent::Entry)
	};
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCountMode =
		ELayoutCountConstraintMode::Exact;
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCount = 1;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/NegotiatedChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/NegotiatedChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");

	FChildLevelEndpointCapabilitySummary& GroundEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	GroundEndpoints.LevelIndex = 0;
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("SharedDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));
	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0)}, {0}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(8, 0, 0)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(8, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 1);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(8, 0, 0), 9);
	FResidualExternalEndpointReachability& Reachability =
		ParentSummary.ExternalEndpointReachability.AddDefaulted_GetRef();
	Reachability.CommitmentId = TEXT("RootExit");
	Reachability.ParentComponentId = 9;
	Reachability.bReachableFromResidualParent = true;
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(8, 0, 0)};

	DemandPlan.ChildRequest.SourceContentEntryId = TEXT("NegotiatedChild");
	const FNegotiatedDemandResult Result =
		NegotiateDemandResponsibilities(SolveContext, DemandPlan, ParentSummary);
	if (!TestTrue(TEXT("Negotiation succeeds by falling through to the first family that survives confirmation"), Result.bSucceeded))
	{
		AddError(Result.FailureReason);
		return false;
	}

	TestEqual(TEXT("Negotiation skips the route-seed-ranked family that cannot satisfy the required host-provider count"), Result.ResponsibilitySet.ContactSet.ParentComponentId, 9);
	TestEqual(TEXT("Negotiation keeps the confirmed parent contact cell from the surviving family"), Result.ResponsibilitySet.ContactSet.ParentContactCells[0], FIntVector(8, 0, 0));
	TestEqual(TEXT("Negotiation keeps the counted parent provider count from the surviving family"), Result.ResponsibilitySet.ResponsibilityContract.CountedParentProviderCount, 1);
	return true;
}

bool FLayoutResponsibilityNegotiationPrefersPlacementFamilyThatSatisfiesRequiredHostProviderCountTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	SolveContext.RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(3, 0, 0), ELayoutCellIntent::Entry)
	};
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCountMode =
		ELayoutCountConstraintMode::Exact;
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCount = 1;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/NegotiatedChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/NegotiatedChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ChildRequest.ProfileSnapshot.LevelCount = 2;
	DemandPlan.ChildRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(0, 0, 2), ELayoutCellIntent::Connector)
	};
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess = true;
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsComposedHostAccess = true;
	DemandPlan.ChildSummary.VerticalAccessCells = {FIntVector(0, 0, 1)};
	DemandPlan.FeatureFlags.bRequiresMultiLevelAnchorSet = true;
	DemandPlan.FeatureFlags.bAllowsChildTraversalBridge = true;
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};

	FChildLevelEndpointCapabilitySummary& LowerEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpoints.LevelIndex = 0;
	LowerEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));

	FChildLevelEndpointCapabilitySummary& UpperEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpoints.LevelIndex = 2;
	UpperEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 2), ELayoutFaceDirection::PosY, PrimaryTraversal));
	PopulateGenerallyConnectableHostTraversalSummary(
		DemandPlan.ChildSummary,
		LowerEndpoints.EndpointOffers[0],
		UpperEndpoints.EndpointOffers[0]);

	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)}, {0, 1, 2}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(8, 0, 0),
		FIntVector(8, 0, 2)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(8, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(8, 0, 2),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 1);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(8, 0, 0), 9);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(8, 0, 2), 9);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(8, 0, 0)};
	ParentSummary.RouteSupportVerticalAccessCells = {FIntVector(8, 0, 0)};

	DemandPlan.ChildRequest.SourceContentEntryId = TEXT("NegotiatedChild");
	const FNegotiatedDemandResult Result =
		NegotiateDemandResponsibilities(SolveContext, DemandPlan, ParentSummary);
	if (!TestTrue(TEXT("Negotiation succeeds when one multi-level family can satisfy the required host-provider count with composed ownership"), Result.bSucceeded))
	{
		AddError(Result.FailureReason);
		return false;
	}

	TestEqual(TEXT("Negotiation prefers the family whose selected component satisfies the required host-provider count"), Result.ResponsibilitySet.ContactSet.ParentComponentId, 9);
	TestEqual(TEXT("Negotiation now treats the preferred family's lone lower counted parent handoff as child-owned under exact-single host counting"), Result.ResponsibilitySet.ResponsibilityContract.HostVerticalAccessResponsibility, ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned);
	TestEqual(TEXT("Negotiation clears the exact counted parent provider count from the preferred family once the lone lower handoff is reclassified as child-owned ascent"), Result.ResponsibilitySet.ResponsibilityContract.CountedParentProviderCount, 0);
	return true;
}

bool FLayoutResponsibilityNegotiationPrefersPlacementFamilyWithMoreCountedParentProvidersWhenMultipleSatisfyRequiredHostProviderCountTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	SolveContext.RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(3, 0, 0), ELayoutCellIntent::Entry)
	};
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCountMode =
		ELayoutCountConstraintMode::Exact;
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCount = 1;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/NegotiatedChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/NegotiatedChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ChildRequest.ProfileSnapshot.LevelCount = 2;
	DemandPlan.ChildRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(0, 0, 2), ELayoutCellIntent::Connector)
	};
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess = true;
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsComposedHostAccess = true;
	DemandPlan.ChildSummary.VerticalAccessCells = {FIntVector(0, 0, 1)};
	DemandPlan.FeatureFlags.bRequiresMultiLevelAnchorSet = true;
	DemandPlan.FeatureFlags.bAllowsChildTraversalBridge = true;
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};

	FChildLevelEndpointCapabilitySummary& LowerEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpoints.LevelIndex = 0;
	LowerEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));

	FChildLevelEndpointCapabilitySummary& UpperEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpoints.LevelIndex = 2;
	UpperEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 2), ELayoutFaceDirection::PosY, PrimaryTraversal));
	PopulateGenerallyConnectableHostTraversalSummary(
		DemandPlan.ChildSummary,
		LowerEndpoints.EndpointOffers[0],
		UpperEndpoints.EndpointOffers[0]);

	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)}, {0, 1, 2}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(8, 0, 0),
		FIntVector(8, 0, 2),
		FIntVector(12, 0, 0),
		FIntVector(12, 0, 1),
		FIntVector(12, 0, 2)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(8, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(8, 0, 2),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(12, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(12, 0, 1),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(12, 0, 2),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 1);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(8, 0, 0), 9);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(8, 0, 2), 9);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(12, 0, 0), 12);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(12, 0, 1), 12);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(12, 0, 2), 12);
	ParentSummary.CountedParentVerticalAccessCells = {
		FIntVector(8, 0, 0),
		FIntVector(12, 0, 0),
		FIntVector(12, 0, 1)
	};

	DemandPlan.ChildRequest.SourceContentEntryId = TEXT("NegotiatedChild");
	const FNegotiatedDemandResult Result =
		NegotiateDemandResponsibilities(SolveContext, DemandPlan, ParentSummary);
	if (!TestTrue(TEXT("Negotiation succeeds when one multi-level family satisfies the required host-provider count after composed contracts stop double-counting child participation"), Result.bSucceeded))
	{
		AddError(Result.FailureReason);
		return false;
	}

	TestEqual(TEXT("Negotiation rejects the richer parent component when it would oversatisfy the exact single-provider requirement under composed counting"), Result.ResponsibilitySet.ContactSet.ParentComponentId, 9);
	TestEqual(TEXT("Negotiation reclassifies the surviving exact-single lower-handoff component as child-owned host ascent when no distinct upper parent provider survives"), Result.ResponsibilitySet.ResponsibilityContract.HostVerticalAccessResponsibility, ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned);
	TestEqual(TEXT("Negotiation clears the counted parent provider count on the surviving exact-single component once the lone lower handoff is reclassified as child-owned"), Result.ResponsibilitySet.ResponsibilityContract.CountedParentProviderCount, 0);
	return true;
}

bool FLayoutResponsibilityNegotiationPrefersPlacementFamilyOnParentRouteSeedComponentTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	SolveContext.RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(8, 0, 0), ELayoutCellIntent::Entry)
	};

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/RouteSeedChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/RouteSeedChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");

	FChildLevelEndpointCapabilitySummary& GroundEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	GroundEndpoints.LevelIndex = 0;
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("SharedDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));
	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0)}, {0}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(8, 0, 0)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(8, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 1);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(8, 0, 0), 9);

	DemandPlan.ChildRequest.SourceContentEntryId = TEXT("NegotiatedChild");
	const FNegotiatedDemandResult Result =
		NegotiateDemandResponsibilities(SolveContext, DemandPlan, ParentSummary);
	if (!TestTrue(TEXT("Negotiation succeeds when one family intersects the parent route-seed component"), Result.bSucceeded))
	{
		AddError(Result.FailureReason);
		return false;
	}

	TestEqual(TEXT("Negotiation prefers the family on the parent route-seed component over the lexically earlier disconnected branch"), Result.ResponsibilitySet.ContactSet.ParentComponentId, 9);
	TestEqual(TEXT("Negotiation keeps the route-seed component parent contact cell"), Result.ResponsibilitySet.ContactSet.ParentContactCells[0], FIntVector(8, 0, 0));
	return true;
}

bool FLayoutResponsibilityNegotiationPrefersPlacementFamilyOnRootExternalEndpointComponentTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	FLayoutCommittedEndpointAnchor& RootExternalEndpoint =
		SolveContext.RootRequest.CommittedEndpointAnchors.AddDefaulted_GetRef();
	RootExternalEndpoint.CommitmentId = TEXT("RootExit");
	RootExternalEndpoint.LocalCell = FIntVector(8, 0, 0);
	RootExternalEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	RootExternalEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/RouteSeedChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/RouteSeedChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");

	FChildLevelEndpointCapabilitySummary& GroundEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	GroundEndpoints.LevelIndex = 0;
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("SharedDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));
	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0)}, {0}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(8, 0, 0)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(8, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 1);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(8, 0, 0), 9);
	FResidualExternalEndpointReachability& Reachability =
		ParentSummary.ExternalEndpointReachability.AddDefaulted_GetRef();
	Reachability.CommitmentId = TEXT("RootExit");
	Reachability.ParentComponentId = 9;
	Reachability.bReachableFromResidualParent = true;

	DemandPlan.ChildRequest.SourceContentEntryId = TEXT("NegotiatedChild");
	const FNegotiatedDemandResult Result =
		NegotiateDemandResponsibilities(SolveContext, DemandPlan, ParentSummary);
	if (!TestTrue(TEXT("Negotiation succeeds when one family intersects the root external endpoint component"), Result.bSucceeded))
	{
		AddError(Result.FailureReason);
		return false;
	}

	TestEqual(TEXT("Negotiation prefers the family on the root external endpoint component over the lexically earlier disconnected branch"), Result.ResponsibilitySet.ContactSet.ParentComponentId, 9);
	TestEqual(TEXT("Negotiation keeps the root external endpoint component parent contact cell"), Result.ResponsibilitySet.ContactSet.ParentContactCells[0], FIntVector(8, 0, 0));
	return true;
}

bool FLayoutResponsibilityNegotiationPrefersPlacementFamilyThatPreservesReachableRootExternalEndpointsTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	SolveContext.RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(3, 0, 0), ELayoutCellIntent::Entry)
	};
	FLayoutCommittedEndpointAnchor& RootExternalEndpoint =
		SolveContext.RootRequest.CommittedEndpointAnchors.AddDefaulted_GetRef();
	RootExternalEndpoint.CommitmentId = TEXT("RootExit");
	RootExternalEndpoint.LocalCell = FIntVector(8, 0, 0);
	RootExternalEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	RootExternalEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/RouteSeedChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/RouteSeedChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");

	FChildLevelEndpointCapabilitySummary& GroundEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	GroundEndpoints.LevelIndex = 0;
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("SharedDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));
	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0)}, {0}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(8, 0, 0)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(8, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 1);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(8, 0, 0), 9);
	FResidualExternalEndpointReachability& Reachability =
		ParentSummary.ExternalEndpointReachability.AddDefaulted_GetRef();
	Reachability.CommitmentId = TEXT("RootExit");
	Reachability.ParentComponentId = 9;
	Reachability.bReachableFromResidualParent = true;

	DemandPlan.ChildRequest.SourceContentEntryId = TEXT("NegotiatedChild");
	const FNegotiatedDemandResult Result =
		NegotiateDemandResponsibilities(SolveContext, DemandPlan, ParentSummary);
	if (!TestTrue(TEXT("Negotiation succeeds when multiple route-seed components remain viable and one preserves a reachable root external endpoint"), Result.bSucceeded))
	{
		AddError(Result.FailureReason);
		return false;
	}

	TestEqual(TEXT("Negotiation prefers the family whose selected component preserves the reachable root external endpoint commitment"), Result.ResponsibilitySet.ContactSet.ParentComponentId, 9);
	TestEqual(TEXT("Negotiation keeps the root external endpoint preserving parent contact cell"), Result.ResponsibilitySet.ContactSet.ParentContactCells[0], FIntVector(8, 0, 0));
	return true;
}

bool FLayoutResponsibilityNegotiationBuildsProofReadyChildRequestOnSuccessTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/NegotiatedChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/NegotiatedChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");

	FChildLevelEndpointCapabilitySummary& GroundEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	GroundEndpoints.LevelIndex = 0;
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("SharedDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));
	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0)}, {0}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {FIntVector(3, 0, 0)};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 4);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(3, 0, 0)};

	DemandPlan.ChildRequest.SourceContentEntryId = TEXT("NegotiatedChild");
	const FNegotiatedDemandResult Result =
		NegotiateDemandResponsibilities(SolveContext, DemandPlan, ParentSummary);
	if (!TestTrue(TEXT("Negotiation succeeds before packaging the proof-ready child request"), Result.bSucceeded))
	{
		AddError(Result.FailureReason);
		return false;
	}

	TestEqual(TEXT("Successful negotiated demand keeps one committed endpoint anchor on the child request"), Result.ChildRequest.CommittedEndpointAnchors.Num(), 1);
	TestTrue(TEXT("Successful negotiated demand copies the chosen committed endpoint anchor onto the child request"), Result.ChildRequest.CommittedEndpointAnchors.ContainsByPredicate(
		[](const FLayoutCommittedEndpointAnchor& EndpointCommitment)
		{
			return EndpointCommitment.CommitmentId == FLayoutId(TEXT("SharedDoor"))
				&& EndpointCommitment.LocalCell == FIntVector(0, 0, 0)
				&& EndpointCommitment.FaceDirection == ELayoutFaceDirection::PosX;
		}));
	TestEqual(TEXT("Successful negotiated demand keeps one negotiated responsibility contract on the child request"), Result.ChildRequest.NegotiatedChildResponsibilityContracts.Num(), 1);
	TestEqual(TEXT("Successful negotiated demand keeps the negotiated child contract region path on the child request"), Result.ChildRequest.NegotiatedChildResponsibilityContracts[0].ChildRegionDebugPath, FString(TEXT("Root/NegotiatedChild")));
	TestEqual(TEXT("Successful negotiated demand keeps the negotiated parent contract region path on the child request"), Result.ChildRequest.NegotiatedChildResponsibilityContracts[0].ParentRegionDebugPath, FString(TEXT("Root")));
	return true;
}

bool FLayoutResponsibilityNegotiationFailsWhenNoPlacementFamilySurvivesTest::RunTest(const FString& Parameters)
{
	FRecursiveScheduleSolveContext SolveContext;
	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/UnreachableChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");

	FChildLevelEndpointCapabilitySummary& GroundEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	GroundEndpoints.LevelIndex = 0;
	GroundEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("Door"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::TraversalPrimary})));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(9, 9, 1),
		{ELayoutFaceDirection::NegX});

	const FNegotiatedDemandResult Result =
		NegotiateDemandResponsibilities(SolveContext, DemandPlan, ParentSummary);
	TestFalse(TEXT("Negotiation fails when no capability-backed placement family exists"), Result.bSucceeded);
	TestEqual(TEXT("Failure keeps the demand child region path"), Result.ChildRegionDebugPath, FString(TEXT("Root/UnreachableChild")));
	TestTrue(TEXT("Failure reason names the missing placement-family condition"), Result.FailureReason.Contains(TEXT("No capability-backed placement family survived deterministic responsibility negotiation.")));
	return true;
}

bool FLayoutResponsibilityNegotiationConfirmsCoherentResponsibilitySetTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(3, 1, 0), ELayoutCellIntent::Entry)
	};
	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 1, 0),
		FIntVector(6, 2, 1)
	};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(6, 2, 1), 5);

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary})),
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 1), ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::TraversalSecondary}))
	};
	FChildLevelEndpointCapabilitySummary& LowerEndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpointSummary.LevelIndex = 0;
	LowerEndpointSummary.EndpointOffers.Add(ChildSummary.EndpointOffers[0]);
	FChildLevelEndpointCapabilitySummary& UpperEndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpointSummary.LevelIndex = 1;
	UpperEndpointSummary.EndpointOffers.Add(ChildSummary.EndpointOffers[1]);

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(5, 1, 1)
	};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;

	FLayoutCommittedEndpointAnchor& LowerEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	LowerEndpoint.CommitmentId = TEXT("LowerDoor");
	LowerEndpoint.LocalCell = FIntVector(0, 0, 0);
	LowerEndpoint.FaceDirection = ELayoutFaceDirection::NegX;
	LowerEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FLayoutCommittedEndpointAnchor& UpperEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	UpperEndpoint.CommitmentId = TEXT("UpperDoor");
	UpperEndpoint.LocalCell = FIntVector(0, 0, 1);
	UpperEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	UpperEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	ContactFamily.PlacementBundle = MakePlacementBundle(
		{FIntVector(0, 0, 0), FIntVector(0, 0, 1)},
		{0, 1});

	ContactFamily.ParentContactCells = {
		FIntVector(3, 1, 0),
		FIntVector(6, 2, 1)
	};

	FLayoutCommittedTraversalAnchor& LowerTraversal = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	LowerTraversal.Cell = FIntVector(3, 1, 0);
	LowerTraversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	FLayoutCommittedTraversalAnchor& UpperTraversal = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	UpperTraversal.Cell = FIntVector(6, 2, 1);
	UpperTraversal.TraversalChannel = LayoutGameplayTags::TraversalSecondary;
	ContactFamily.ParentTraversalIngressAnchors = {LowerTraversal, UpperTraversal};

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestTrue(TEXT("Coherent responsibility set confirms successfully"), bConfirmed);
	TestTrue(TEXT("Successful confirmation leaves no failure reason"), FailureReason.IsEmpty());
	return true;
}

bool FLayoutResponsibilityNegotiationBuildsBoundedParentProofEvidenceOnConfirmedResponsibilitySetTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(3, 1, 0), ELayoutCellIntent::Entry)
	};
	FLayoutCommittedEndpointAnchor& RootExternalEndpoint =
		RootRequest.CommittedEndpointAnchors.AddDefaulted_GetRef();
	RootExternalEndpoint.CommitmentId = TEXT("RootExit");
	RootExternalEndpoint.LocalCell = FIntVector(3, 1, 0);
	RootExternalEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	RootExternalEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {FIntVector(3, 1, 0)};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);
	ParentSummary.RequiredChildBundleSupportCells = {FIntVector(4, 1, 0)};
	FResidualExternalEndpointReachability& Reachability =
		ParentSummary.ExternalEndpointReachability.AddDefaulted_GetRef();
	Reachability.CommitmentId = TEXT("RootExit");
	Reachability.ParentComponentId = 5;
	Reachability.bReachableFromResidualParent = true;

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("Door"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& EndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	EndpointSummary.LevelIndex = 0;
	EndpointSummary.EndpointOffers = ChildSummary.EndpointOffers;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {FIntVector(4, 1, 0)};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;
	ContactFamily.PlacementBundle = MakePlacementBundle({FIntVector(0, 0, 0)}, {0});
	ContactFamily.ParentContactCells = {FIntVector(3, 1, 0)};

	FLayoutCommittedEndpointAnchor& Endpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	Endpoint.CommitmentId = TEXT("Door");
	Endpoint.LocalCell = FIntVector(0, 0, 0);
	Endpoint.FaceDirection = ELayoutFaceDirection::NegX;
	Endpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FLayoutCommittedTraversalAnchor& Traversal =
		ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	Traversal.Cell = FIntVector(3, 1, 0);
	Traversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	ContactFamily.ParentTraversalIngressAnchors = {Traversal};

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	if (!TestTrue(TEXT("Confirmation succeeds when bounded parent-proof support and root external endpoint reachability are preserved"), bConfirmed))
	{
		AddError(FailureReason);
		return false;
	}

	const FBoundedParentProofEvidence& Evidence =
		ResponsibilitySet.BoundedParentProofEvidence;
	TestEqual(TEXT("Bounded parent-proof evidence keeps the child region path"), Evidence.ChildRegionDebugPath, FString(TEXT("Root/ChildA")));
	TestEqual(TEXT("Bounded parent-proof evidence keeps the selected parent component"), Evidence.ParentComponentId, 5);
	if (!TestEqual(TEXT("Bounded parent-proof evidence keeps one route-seed cell on the selected component"), Evidence.ConfirmedParentRouteSeedCells.Num(), 1)
		|| !TestEqual(TEXT("Bounded parent-proof evidence keeps one confirmed parent contact cell"), Evidence.ConfirmedParentContactCells.Num(), 1)
		|| !TestEqual(TEXT("Bounded parent-proof evidence keeps one confirmed parent traversal-ingress cell"), Evidence.ConfirmedParentTraversalIngressCells.Num(), 1)
		|| !TestEqual(TEXT("Bounded parent-proof evidence keeps one required child-bundle support cell"), Evidence.ConfirmedRequiredChildBundleSupportCells.Num(), 1)
		|| !TestEqual(TEXT("Bounded parent-proof evidence keeps one reachable root external endpoint commitment id"), Evidence.PreservedRootExternalEndpointCommitmentIds.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Bounded parent-proof evidence keeps the route-seed entry cell"), Evidence.ConfirmedParentRouteSeedCells[0], FIntVector(3, 1, 0));
	TestEqual(TEXT("Bounded parent-proof evidence keeps the confirmed parent contact cell"), Evidence.ConfirmedParentContactCells[0], FIntVector(3, 1, 0));
	TestEqual(TEXT("Bounded parent-proof evidence keeps the confirmed parent traversal-ingress cell"), Evidence.ConfirmedParentTraversalIngressCells[0], FIntVector(3, 1, 0));
	TestEqual(TEXT("Bounded parent-proof evidence keeps the required child-bundle support cell"), Evidence.ConfirmedRequiredChildBundleSupportCells[0], FIntVector(4, 1, 0));
	TestEqual(TEXT("Bounded parent-proof evidence keeps the reachable root external endpoint commitment id"), Evidence.PreservedRootExternalEndpointCommitmentIds[0], FLayoutId(TEXT("RootExit")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsResponsibilitySetWhenChildBundleSupportIsMissingTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(3, 1, 0), ELayoutCellIntent::Entry)
	};

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {FIntVector(3, 1, 0)};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);
	ParentSummary.RequiredChildBundleSupportCells = {FIntVector(4, 1, 0)};
	ParentSummary.MissingRequiredChildBundleSupportCells = {FIntVector(4, 1, 0)};

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("Door"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& EndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	EndpointSummary.LevelIndex = 0;
	EndpointSummary.EndpointOffers = ChildSummary.EndpointOffers;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {FIntVector(4, 1, 0)};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;
	ContactFamily.PlacementBundle = MakePlacementBundle({FIntVector(0, 0, 0)}, {0});
	ContactFamily.ParentContactCells = {FIntVector(3, 1, 0)};

	FLayoutCommittedEndpointAnchor& Endpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	Endpoint.CommitmentId = TEXT("Door");
	Endpoint.LocalCell = FIntVector(0, 0, 0);
	Endpoint.FaceDirection = ELayoutFaceDirection::NegX;
	Endpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a family when required child-bundle support is already missing in the residual parent summary"), bConfirmed);
	TestTrue(TEXT("Failure reason names the missing child-bundle support cell"), FailureReason.Contains(TEXT("child-bundle support")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsResponsibilitySetWhenRootExternalEndpointBecomesUnreachableTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(3, 1, 0), ELayoutCellIntent::Entry)
	};
	FLayoutCommittedEndpointAnchor& RootExternalEndpoint =
		RootRequest.CommittedEndpointAnchors.AddDefaulted_GetRef();
	RootExternalEndpoint.CommitmentId = TEXT("RootExit");
	RootExternalEndpoint.LocalCell = FIntVector(3, 1, 0);
	RootExternalEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	RootExternalEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {FIntVector(3, 1, 0)};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);
	FResidualExternalEndpointReachability& Reachability =
		ParentSummary.ExternalEndpointReachability.AddDefaulted_GetRef();
	Reachability.CommitmentId = TEXT("RootExit");
	Reachability.ParentComponentId = INDEX_NONE;
	Reachability.bReachableFromResidualParent = false;

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("Door"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& EndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	EndpointSummary.LevelIndex = 0;
	EndpointSummary.EndpointOffers = ChildSummary.EndpointOffers;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {FIntVector(4, 1, 0)};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;
	ContactFamily.PlacementBundle = MakePlacementBundle({FIntVector(0, 0, 0)}, {0});
	ContactFamily.ParentContactCells = {FIntVector(3, 1, 0)};

	FLayoutCommittedEndpointAnchor& Endpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	Endpoint.CommitmentId = TEXT("Door");
	Endpoint.LocalCell = FIntVector(0, 0, 0);
	Endpoint.FaceDirection = ELayoutFaceDirection::NegX;
	Endpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a family when a required root external endpoint is no longer reachable from the residual parent graph"), bConfirmed);
	TestTrue(TEXT("Failure reason names the lost root external endpoint commitment"), FailureReason.Contains(TEXT("RootExit")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsResponsibilitySetWhenRootExternalEndpointReachabilityClassificationIsMissingTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(3, 1, 0), ELayoutCellIntent::Entry)
	};
	FLayoutCommittedEndpointAnchor& RootExternalEndpoint =
		RootRequest.CommittedEndpointAnchors.AddDefaulted_GetRef();
	RootExternalEndpoint.CommitmentId = TEXT("RootExit");
	RootExternalEndpoint.LocalCell = FIntVector(3, 1, 0);
	RootExternalEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	RootExternalEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {FIntVector(3, 1, 0)};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("Door"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& EndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	EndpointSummary.LevelIndex = 0;
	EndpointSummary.EndpointOffers = ChildSummary.EndpointOffers;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {FIntVector(4, 1, 0)};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;
	ContactFamily.PlacementBundle = MakePlacementBundle({FIntVector(0, 0, 0)}, {0});
	ContactFamily.ParentContactCells = {FIntVector(3, 1, 0)};

	FLayoutCommittedEndpointAnchor& Endpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	Endpoint.CommitmentId = TEXT("Door");
	Endpoint.LocalCell = FIntVector(0, 0, 0);
	Endpoint.FaceDirection = ELayoutFaceDirection::NegX;
	Endpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a family when a required named root external endpoint commitment never received residual reachability classification"), bConfirmed);
	TestTrue(TEXT("Failure reason names the missing root external endpoint reachability classification"), FailureReason.Contains(TEXT("RootExit")) && FailureReason.Contains(TEXT("reachability classification")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsResponsibilitySetWhenRootExternalEndpointSurvivesOnDifferentComponentTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(3, 1, 0), ELayoutCellIntent::Entry)
	};
	FLayoutCommittedEndpointAnchor& RootExternalEndpoint =
		RootRequest.CommittedEndpointAnchors.AddDefaulted_GetRef();
	RootExternalEndpoint.CommitmentId = TEXT("RootExit");
	RootExternalEndpoint.LocalCell = FIntVector(9, 1, 0);
	RootExternalEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	RootExternalEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 1, 0),
		FIntVector(9, 1, 0)
	};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(9, 1, 0), 9);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(3, 1, 0)};
	FResidualExternalEndpointReachability& Reachability =
		ParentSummary.ExternalEndpointReachability.AddDefaulted_GetRef();
	Reachability.CommitmentId = TEXT("RootExit");
	Reachability.ParentComponentId = 9;
	Reachability.bReachableFromResidualParent = true;

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("Door"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& EndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	EndpointSummary.LevelIndex = 0;
	EndpointSummary.EndpointOffers = ChildSummary.EndpointOffers;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {FIntVector(4, 1, 0)};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;
	ContactFamily.PlacementBundle = MakePlacementBundle({FIntVector(0, 0, 0)}, {0});
	ContactFamily.ParentContactCells = {FIntVector(3, 1, 0)};

	FLayoutCommittedEndpointAnchor& Endpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	Endpoint.CommitmentId = TEXT("Door");
	Endpoint.LocalCell = FIntVector(0, 0, 0);
	Endpoint.FaceDirection = ELayoutFaceDirection::NegX;
	Endpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a family when a required root external endpoint survives only on a different residual parent component than the chosen family"), bConfirmed);
	TestTrue(TEXT("Failure reason names the different root external endpoint component"), FailureReason.Contains(TEXT("RootExit")) && FailureReason.Contains(TEXT("component 9")) && FailureReason.Contains(TEXT("component 5")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsResponsibilitySetWithWrongParentComponentTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {FIntVector(3, 1, 0)};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("Door"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& EndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	EndpointSummary.LevelIndex = 0;
	EndpointSummary.EndpointOffers = ChildSummary.EndpointOffers;
	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 99;

	FLayoutCommittedEndpointAnchor& Endpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	Endpoint.CommitmentId = TEXT("Door");
	Endpoint.LocalCell = FIntVector(0, 0, 0);
	Endpoint.FaceDirection = ELayoutFaceDirection::NegX;
	Endpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	ContactFamily.PlacementBundle = MakePlacementBundle({FIntVector(0, 0, 0)}, {0});

	ContactFamily.ParentContactCells = {FIntVector(3, 1, 0)};

	FLayoutCommittedTraversalAnchor& Traversal = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	Traversal.Cell = FIntVector(3, 1, 0);
	Traversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	ContactFamily.ParentTraversalIngressAnchors = {Traversal};

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a contact set whose selected component does not match the residual parent summary"), bConfirmed);
	TestTrue(TEXT("Failure reason names the wrong residual parent component"), FailureReason.Contains(TEXT("does not belong to the selected residual parent component")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsResponsibilitySetWithParentContactFaceMissingFromResidualParentTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {FIntVector(3, 1, 0)};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 1, 0),
		{ELayoutFaceDirection::NegX});

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("Door"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& EndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	EndpointSummary.LevelIndex = 0;
	EndpointSummary.EndpointOffers = ChildSummary.EndpointOffers;
	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;

	FLayoutCommittedEndpointAnchor& Endpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	Endpoint.CommitmentId = TEXT("Door");
	Endpoint.LocalCell = FIntVector(0, 0, 0);
	Endpoint.FaceDirection = ELayoutFaceDirection::NegX;
	Endpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	ContactFamily.PlacementBundle = MakePlacementBundle({FIntVector(0, 0, 0)}, {0});

	ContactFamily.ParentContactCells = {FIntVector(3, 1, 0)};
	PopulateDirectParentContactFaces(ContactFamily);

	FLayoutCommittedTraversalAnchor& Traversal =
		ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	Traversal.Cell = FIntVector(3, 1, 0);
	Traversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	ContactFamily.ParentTraversalIngressAnchors = {Traversal};

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a family when the exact required parent contact face is no longer preserved in the residual parent summary"), bConfirmed);
	TestTrue(TEXT("Failure reason names the missing residual parent contact face"), FailureReason.Contains(TEXT("contact face")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsResponsibilitySetOutsideParentRouteSeedComponentTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Entry)
	};

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(1, 0, 0),
		FIntVector(7, 0, 0)
	};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(1, 0, 0), 2);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(7, 0, 0), 9);
	FResidualExternalEndpointReachability& Reachability =
		ParentSummary.ExternalEndpointReachability.AddDefaulted_GetRef();
	Reachability.CommitmentId = TEXT("RootExit");
	Reachability.ParentComponentId = 2;
	Reachability.bReachableFromResidualParent = true;

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("Door"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& EndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	EndpointSummary.LevelIndex = 0;
	EndpointSummary.EndpointOffers = ChildSummary.EndpointOffers;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 9;
	ContactFamily.PlacementBundle = MakePlacementBundle({FIntVector(0, 0, 0)}, {0});
	ContactFamily.ParentContactCells = {FIntVector(7, 0, 0)};

	FLayoutCommittedEndpointAnchor& Endpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	Endpoint.CommitmentId = TEXT("Door");
	Endpoint.LocalCell = FIntVector(0, 0, 0);
	Endpoint.FaceDirection = ELayoutFaceDirection::NegX;
	Endpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a family that does not intersect the current parent route-seed component"), bConfirmed);
	TestTrue(TEXT("Failure reason names the missing parent route-seed intersection"), FailureReason.Contains(TEXT("route seed network")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsResponsibilitySetOutsideRootExternalEndpointComponentTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	FLayoutCommittedEndpointAnchor& RootExternalEndpoint =
		RootRequest.CommittedEndpointAnchors.AddDefaulted_GetRef();
	RootExternalEndpoint.CommitmentId = NAME_None;
	RootExternalEndpoint.LocalCell = FIntVector(1, 0, 0);
	RootExternalEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	RootExternalEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(1, 0, 0),
		FIntVector(7, 0, 0)
	};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(1, 0, 0), 2);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(7, 0, 0), 9);

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("Door"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& EndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	EndpointSummary.LevelIndex = 0;
	EndpointSummary.EndpointOffers = ChildSummary.EndpointOffers;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 9;
	ContactFamily.PlacementBundle = MakePlacementBundle({FIntVector(0, 0, 0)}, {0});
	ContactFamily.ParentContactCells = {FIntVector(7, 0, 0)};

	FLayoutCommittedEndpointAnchor& Endpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	Endpoint.CommitmentId = TEXT("Door");
	Endpoint.LocalCell = FIntVector(0, 0, 0);
	Endpoint.FaceDirection = ELayoutFaceDirection::NegX;
	Endpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a family that does not intersect the current root external endpoint route-seed component"), bConfirmed);
	TestTrue(TEXT("Failure reason names the missing root external endpoint route-seed intersection"), FailureReason.Contains(TEXT("route seed network")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsResponsibilitySetWithCountedParentProviderOutsideSelectedComponentTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 1, 0),
		FIntVector(7, 1, 0)
	};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(7, 1, 0), 9);
	ParentSummary.CountedParentVerticalAccessCells = {
		FIntVector(3, 1, 0),
		FIntVector(7, 1, 0)
	};

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("Door"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& EndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	EndpointSummary.LevelIndex = 0;
	EndpointSummary.EndpointOffers = ChildSummary.EndpointOffers;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;
	ContactFamily.PlacementBundle = MakePlacementBundle({FIntVector(0, 0, 0)}, {0});
	ContactFamily.ParentContactCells = {FIntVector(3, 1, 0)};

	FLayoutCommittedEndpointAnchor& Endpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	Endpoint.CommitmentId = TEXT("Door");
	Endpoint.LocalCell = FIntVector(0, 0, 0);
	Endpoint.FaceDirection = ELayoutFaceDirection::NegX;
	Endpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FLayoutCommittedTraversalAnchor& Traversal = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	Traversal.Cell = FIntVector(3, 1, 0);
	Traversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	ContactFamily.ParentTraversalIngressAnchors = {Traversal};

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);
	ResponsibilitySet.ResponsibilityContract.CountedParentProviderCount = 2;
	ResponsibilitySet.ResponsibilityContract.CountedParentVerticalAccessCells = {
		FIntVector(3, 1, 0),
		FIntVector(7, 1, 0)
	};

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a counted parent provider set that spills onto a different residual parent component"), bConfirmed);
	TestTrue(TEXT("Failure reason names the out-of-component counted parent provider"), FailureReason.Contains(TEXT("does not belong to the selected residual parent component")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsResponsibilitySetWithEndpointOutsidePlacementBundleTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {FIntVector(3, 1, 0)};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("Door"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& EndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	EndpointSummary.LevelIndex = 0;
	EndpointSummary.EndpointOffers = ChildSummary.EndpointOffers;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {FIntVector(4, 1, 0)};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;
	ContactFamily.PlacementBundle = MakePlacementBundle({FIntVector(1, 0, 0)}, {0});

	FLayoutCommittedEndpointAnchor& Endpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	Endpoint.CommitmentId = TEXT("Door");
	Endpoint.LocalCell = FIntVector(0, 0, 0);
	Endpoint.FaceDirection = ELayoutFaceDirection::NegX;
	Endpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	ContactFamily.ParentContactCells = {FIntVector(3, 1, 0)};

	FLayoutCommittedTraversalAnchor& Traversal = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	Traversal.Cell = FIntVector(3, 1, 0);
	Traversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	ContactFamily.ParentTraversalIngressAnchors = {Traversal};

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a negotiated endpoint that lies outside the selected placement bundle"), bConfirmed);
	TestTrue(TEXT("Failure reason names the placement-bundle endpoint mismatch"), FailureReason.Contains(TEXT("is not occupied by the selected placement bundle")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsResponsibilitySetWithReplacementVolumeCountMismatchTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {FIntVector(3, 1, 0)};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("Door"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& EndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	EndpointSummary.LevelIndex = 0;
	EndpointSummary.EndpointOffers = ChildSummary.EndpointOffers;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(5, 1, 0)
	};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;
	ContactFamily.PlacementBundle = MakePlacementBundle({FIntVector(0, 0, 0)}, {0});

	FLayoutCommittedEndpointAnchor& Endpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	Endpoint.CommitmentId = TEXT("Door");
	Endpoint.LocalCell = FIntVector(0, 0, 0);
	Endpoint.FaceDirection = ELayoutFaceDirection::NegX;
	Endpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	ContactFamily.ParentContactCells = {FIntVector(3, 1, 0)};

	FLayoutCommittedTraversalAnchor& Traversal = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	Traversal.Cell = FIntVector(3, 1, 0);
	Traversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	ContactFamily.ParentTraversalIngressAnchors = {Traversal};

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a negotiated replacement volume whose per-level cell count no longer matches the selected placement bundle footprint"), bConfirmed);
	TestTrue(TEXT("Failure reason names the replacement-volume footprint mismatch"), FailureReason.Contains(TEXT("replacement volume keeps")) && FailureReason.Contains(TEXT("occupies")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsResponsibilitySetWithReplacementVolumeOverlappingRetainedShellTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {FIntVector(3, 1, 0)};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("Door"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& EndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	EndpointSummary.LevelIndex = 0;
	EndpointSummary.EndpointOffers = ChildSummary.EndpointOffers;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {
		FIntVector(3, 1, 0)
	};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;
	ContactFamily.PlacementBundle = MakePlacementBundle({FIntVector(0, 0, 0)}, {0});

	FLayoutCommittedEndpointAnchor& Endpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	Endpoint.CommitmentId = TEXT("Door");
	Endpoint.LocalCell = FIntVector(0, 0, 0);
	Endpoint.FaceDirection = ELayoutFaceDirection::NegX;
	Endpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	ContactFamily.ParentContactCells = {FIntVector(3, 1, 0)};

	FLayoutCommittedTraversalAnchor& Traversal = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	Traversal.Cell = FIntVector(3, 1, 0);
	Traversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	ContactFamily.ParentTraversalIngressAnchors = {Traversal};

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);
	FLayoutNegotiatedLevelCellSet& RetainedShellLevel =
		ResponsibilitySet.ResponsibilityContract.RetainedParentShellCellsByLevel.AddDefaulted_GetRef();
	RetainedShellLevel.Level = 0;
	RetainedShellLevel.Cells.Add(FIntVector(3, 1, 0));

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a negotiated contract that keeps the same parent-local cell in both replacement volume and retained shell"), bConfirmed);
	TestTrue(TEXT("Failure reason names the replacement-volume versus retained-shell overlap"), FailureReason.Contains(TEXT("both replacement volume and retained parent shell")));
	return true;
}

bool FLayoutResponsibilityNegotiationSelectsChildOwnedHostAscentForMultiLevelDemandTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildOwnedChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/ChildOwnedChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ChildRequest.ProfileSnapshot.LevelCount = 2;
	DemandPlan.ChildRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(0, 0, 2), ELayoutCellIntent::Connector)
	};
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess = true;
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsComposedHostAccess = true;
	DemandPlan.ChildSummary.VerticalAccessCells = {FIntVector(0, 0, 1)};
	DemandPlan.FeatureFlags.bRequiresMultiLevelAnchorSet = true;
	DemandPlan.FeatureFlags.bAllowsChildTraversalBridge = true;
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};

	FChildLevelEndpointCapabilitySummary& LowerEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpoints.LevelIndex = 0;
	LowerEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));

	FChildLevelEndpointCapabilitySummary& UpperEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpoints.LevelIndex = 2;
	UpperEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 2), ELayoutFaceDirection::PosY, PrimaryTraversal));
	PopulateGenerallyConnectableHostTraversalSummary(
		DemandPlan.ChildSummary,
		LowerEndpoints.EndpointOffers[0],
		UpperEndpoints.EndpointOffers[0]);

	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)}, {0, 1, 2}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(4, 1, 2)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(4, 1, 2),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(4, 1, 2), 7);

	DemandPlan.ChildRequest.SourceContentEntryId = TEXT("NegotiatedChild");
	const FNegotiatedDemandResult Result =
		NegotiateDemandResponsibilities(SolveContext, DemandPlan, ParentSummary);
	if (!TestTrue(TEXT("Multi-level child-owned demand negotiates successfully"), Result.bSucceeded))
	{
		AddError(Result.FailureReason);
		return false;
	}

	const FLayoutNegotiatedChildResponsibilityContract& Contract =
		Result.ResponsibilitySet.ResponsibilityContract;
	TestEqual(
		TEXT("Negotiation classifies the multi-level host ascent as child-owned when no counted parent providers remain"),
		Contract.HostVerticalAccessResponsibility,
		ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned);
	TestEqual(TEXT("Child-owned contract keeps zero counted parent providers"), Contract.CountedParentProviderCount, 0);
	TestTrue(TEXT("Child-owned contract counts the negotiated child provider"), Contract.CountedChildProviderRegionDebugPaths.Contains(DemandPlan.ChildRegionDebugPath));
	TestTrue(TEXT("Child-owned contract fixes a lower host ingress anchor"), Contract.bHasRequiredHostIngressAnchor);
	TestTrue(TEXT("Child-owned contract fixes an upper host egress anchor"), Contract.bHasRequiredHostEgressAnchor);
	TestEqual(TEXT("Child-owned contract keeps the negotiated generally connectable anchor-pair proof"), Contract.RequiredChildGenerallyConnectableAnchorPairId, FLayoutId(TEXT("LowerDoor__UpperDoor")));
	TestEqual(TEXT("Child-owned contract keeps the full negotiated ascent span"), Contract.RequiredChildInternalVerticalSpanLevels.Num(), 3);

	FString ContractFailureReason;
	TestTrue(
		TEXT("Child-owned negotiation contract satisfies the existing negotiated-contract validator"),
		LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
			Contract,
			ContractFailureReason));
	if (!ContractFailureReason.IsEmpty())
	{
		AddInfo(ContractFailureReason);
	}
	return true;
}

bool FLayoutResponsibilityNegotiationDisplacedCountedParentProviderYieldsChildOwnedHostAscentTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCountMode =
		ELayoutCountConstraintMode::Exact;
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCount = 1;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/NegotiatedChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/NegotiatedChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ChildRequest.ProfileSnapshot.LevelCount = 2;
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess = true;
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsComposedHostAccess = true;
	DemandPlan.ChildSummary.VerticalAccessCells = {FIntVector(0, 0, 1)};
	DemandPlan.FeatureFlags.bRequiresMultiLevelAnchorSet = true;
	DemandPlan.FeatureFlags.bAllowsChildTraversalBridge = true;
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};

	FChildLevelEndpointCapabilitySummary& LowerEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpoints.LevelIndex = 0;
	LowerEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));

	FChildLevelEndpointCapabilitySummary& UpperEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpoints.LevelIndex = 2;
	UpperEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 2), ELayoutFaceDirection::PosY, PrimaryTraversal));
	PopulateGenerallyConnectableHostTraversalSummary(
		DemandPlan.ChildSummary,
		LowerEndpoints.EndpointOffers[0],
		UpperEndpoints.EndpointOffers[0]);

	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)}, {0, 1, 2}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(4, 1, 2),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(4, 1, 1), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(4, 1, 2), 7);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(4, 1, 1)};

	DemandPlan.ChildRequest.SourceContentEntryId = TEXT("NegotiatedChild");
	const FNegotiatedDemandResult Result =
		NegotiateDemandResponsibilities(SolveContext, DemandPlan, ParentSummary);
	if (!TestTrue(TEXT("Negotiation succeeds when the only counted parent provider is displaced by the child's replacement volume"), Result.bSucceeded))
	{
		AddError(Result.FailureReason);
		return false;
	}

	const FLayoutNegotiatedChildResponsibilityContract& Contract =
		Result.ResponsibilitySet.ResponsibilityContract;
	TestEqual(
		TEXT("Negotiation classifies the host ascent as child-owned when the selected counted parent provider is displaced"),
		Contract.HostVerticalAccessResponsibility,
		ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned);
	TestEqual(TEXT("Displaced-provider contract keeps zero counted parent providers"), Contract.CountedParentProviderCount, 0);
	TestTrue(TEXT("Displaced-provider contract removes the displaced parent provider cell from the counted set"), Contract.CountedParentVerticalAccessCells.IsEmpty());
	TestTrue(TEXT("Displaced-provider contract still counts the negotiated child provider"), Contract.CountedChildProviderRegionDebugPaths.Contains(DemandPlan.ChildRegionDebugPath));

	FString ContractFailureReason;
	TestTrue(
		TEXT("Displaced-provider contract satisfies the negotiated-contract validator"),
		LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
			Contract,
			ContractFailureReason));
	if (!ContractFailureReason.IsEmpty())
	{
		AddInfo(ContractFailureReason);
	}
	return true;
}

bool FLayoutResponsibilityNegotiationSelectsComposedHostAscentForMultiLevelDemandTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ComposedChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/ComposedChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ChildRequest.ProfileSnapshot.LevelCount = 2;
	DemandPlan.ChildRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(0, 0, 2), ELayoutCellIntent::Connector)
	};
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess = true;
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsComposedHostAccess = true;
	DemandPlan.ChildSummary.VerticalAccessCells = {FIntVector(0, 0, 1)};
	DemandPlan.FeatureFlags.bRequiresMultiLevelAnchorSet = true;
	DemandPlan.FeatureFlags.bAllowsChildTraversalBridge = true;
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};

	FChildLevelEndpointCapabilitySummary& LowerEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpoints.LevelIndex = 0;
	LowerEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));

	FChildLevelEndpointCapabilitySummary& UpperEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpoints.LevelIndex = 2;
	UpperEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 2), ELayoutFaceDirection::PosY, PrimaryTraversal));
	PopulateGenerallyConnectableHostTraversalSummary(
		DemandPlan.ChildSummary,
		LowerEndpoints.EndpointOffers[0],
		UpperEndpoints.EndpointOffers[0]);

	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)}, {0, 1, 2}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(4, 1, 2)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(4, 1, 2),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(4, 1, 2), 7);
	ParentSummary.RootConnectedTraversableCells.Add(FIntVector(9, 0, 0));
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(9, 0, 0), 11);
	ParentSummary.CountedParentVerticalAccessCells = {
		FIntVector(3, 0, 0),
		FIntVector(9, 0, 0)
	};

	DemandPlan.ChildRequest.SourceContentEntryId = TEXT("NegotiatedChild");
	const FNegotiatedDemandResult Result =
		NegotiateDemandResponsibilities(SolveContext, DemandPlan, ParentSummary);
	if (!TestTrue(TEXT("Multi-level composed demand negotiates successfully"), Result.bSucceeded))
	{
		AddError(Result.FailureReason);
		return false;
	}

	const FLayoutNegotiatedChildResponsibilityContract& Contract =
		Result.ResponsibilitySet.ResponsibilityContract;
	TestEqual(
		TEXT("Negotiation classifies the multi-level host ascent as composed when counted parent providers remain"),
		Contract.HostVerticalAccessResponsibility,
		ELayoutNegotiatedHostVerticalAccessResponsibility::Composed);
	TestEqual(TEXT("Composed contract keeps the counted parent provider count"), Contract.CountedParentProviderCount, 1);
	TestEqual(TEXT("Composed contract keeps the exact counted parent provider set"), Contract.CountedParentVerticalAccessCells.Num(), 1);
	TestEqual(TEXT("Composed contract keeps the exact counted parent provider cell"), Contract.CountedParentVerticalAccessCells[0], FIntVector(3, 0, 0));
	TestFalse(TEXT("Composed contract trims disconnected counted parent providers from other residual components"), Contract.CountedParentVerticalAccessCells.Contains(FIntVector(9, 0, 0)));
	TestTrue(TEXT("Composed contract counts the negotiated child provider"), Contract.CountedChildProviderRegionDebugPaths.Contains(DemandPlan.ChildRegionDebugPath));
	TestTrue(TEXT("Composed contract fixes a lower host ingress anchor"), Contract.bHasRequiredHostIngressAnchor);
	TestTrue(TEXT("Composed contract fixes an upper host egress anchor"), Contract.bHasRequiredHostEgressAnchor);
	TestEqual(TEXT("Composed contract keeps the negotiated generally connectable anchor-pair proof"), Contract.RequiredChildGenerallyConnectableAnchorPairId, FLayoutId(TEXT("LowerDoor__UpperDoor")));
	TestEqual(TEXT("Composed contract keeps the full negotiated ascent span"), Contract.RequiredChildInternalVerticalSpanLevels.Num(), 3);

	FString ContractFailureReason;
	TestTrue(
		TEXT("Composed negotiation contract satisfies the existing negotiated-contract validator"),
		LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
			Contract,
			ContractFailureReason));
	if (!ContractFailureReason.IsEmpty())
	{
		AddInfo(ContractFailureReason);
	}
	return true;
}

bool FLayoutResponsibilityNegotiationTreatsSingleLowerParentHandoffAsChildOwnedExactSingleTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCountMode =
		ELayoutCountConstraintMode::Exact;
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCount = 1;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/HandoffChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/HandoffChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ChildRequest.ProfileSnapshot.LevelCount = 2;
	DemandPlan.ChildRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(0, 0, 2), ELayoutCellIntent::Connector)
	};
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess = true;
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsComposedHostAccess = true;
	DemandPlan.ChildSummary.VerticalAccessCells = {FIntVector(0, 0, 1)};
	DemandPlan.FeatureFlags.bRequiresMultiLevelAnchorSet = true;
	DemandPlan.FeatureFlags.bAllowsChildTraversalBridge = true;
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};

	FChildLevelEndpointCapabilitySummary& LowerEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpoints.LevelIndex = 0;
	LowerEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));

	FChildLevelEndpointCapabilitySummary& UpperEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpoints.LevelIndex = 2;
	UpperEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 2), ELayoutFaceDirection::PosY, PrimaryTraversal));
	PopulateGenerallyConnectableHostTraversalSummary(
		DemandPlan.ChildSummary,
		LowerEndpoints.EndpointOffers[0],
		UpperEndpoints.EndpointOffers[0]);

	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)}, {0, 1, 2}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(4, 1, 2)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(4, 1, 2),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(4, 1, 2), 7);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(3, 0, 0)};
	ParentSummary.RouteSupportVerticalAccessCells = {FIntVector(3, 0, 0)};

	DemandPlan.ChildRequest.SourceContentEntryId = TEXT("NegotiatedChild");
	const FNegotiatedDemandResult Result =
		NegotiateDemandResponsibilities(SolveContext, DemandPlan, ParentSummary);
	if (!TestTrue(TEXT("Exact-single negotiation succeeds when the only surviving counted parent provider is the lower handoff into a child-owned ascent"), Result.bSucceeded))
	{
		AddError(Result.FailureReason);
		return false;
	}

	const FLayoutNegotiatedChildResponsibilityContract& Contract =
		Result.ResponsibilitySet.ResponsibilityContract;
	TestEqual(
		TEXT("Exact-single negotiation treats a lone lower counted parent handoff as child-owned instead of composed"),
		Contract.HostVerticalAccessResponsibility,
		ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned);
	TestEqual(TEXT("Child-owned exact-single contract keeps zero counted parent providers"), Contract.CountedParentProviderCount, 0);
	TestTrue(TEXT("Child-owned exact-single contract removes the lower handoff from the counted parent provider set"), Contract.CountedParentVerticalAccessCells.IsEmpty());
	TestTrue(TEXT("Child-owned exact-single contract still counts the negotiated child provider"), Contract.CountedChildProviderRegionDebugPaths.Contains(DemandPlan.ChildRegionDebugPath));
	TestTrue(TEXT("Child-owned exact-single contract still fixes a lower host ingress anchor"), Contract.bHasRequiredHostIngressAnchor);
	TestTrue(TEXT("Child-owned exact-single contract still fixes an upper host egress anchor"), Contract.bHasRequiredHostEgressAnchor);
	TestEqual(TEXT("Child-owned exact-single contract keeps the full negotiated ascent span"), Contract.RequiredChildInternalVerticalSpanLevels.Num(), 3);

	FString ContractFailureReason;
	TestTrue(
		TEXT("Child-owned exact-single contract satisfies the negotiated-contract validator"),
		LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
			Contract,
			ContractFailureReason));
	if (!ContractFailureReason.IsEmpty())
	{
		AddInfo(ContractFailureReason);
	}
	return true;
}

bool FLayoutResponsibilityNegotiationTreatsSingleLowerParentRouteHandoffAsChildOwnedExactSingleTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCountMode =
		ELayoutCountConstraintMode::Exact;
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCount = 1;
	SolveContext.RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(8, 0, 0), ELayoutCellIntent::Entry)
	};

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/RouteHandoffChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/RouteHandoffChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ChildRequest.ProfileSnapshot.LevelCount = 2;
	DemandPlan.ChildRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(0, 0, 2), ELayoutCellIntent::Connector)
	};
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess = true;
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsComposedHostAccess = true;
	DemandPlan.ChildSummary.VerticalAccessCells = {FIntVector(0, 0, 1)};
	DemandPlan.FeatureFlags.bRequiresMultiLevelAnchorSet = true;
	DemandPlan.FeatureFlags.bAllowsChildTraversalBridge = true;
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};

	FChildLevelEndpointCapabilitySummary& LowerEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpoints.LevelIndex = 0;
	LowerEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));

	FChildLevelEndpointCapabilitySummary& UpperEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpoints.LevelIndex = 2;
	UpperEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 2), ELayoutFaceDirection::PosY, PrimaryTraversal));
	PopulateGenerallyConnectableHostTraversalSummary(
		DemandPlan.ChildSummary,
		LowerEndpoints.EndpointOffers[0],
		UpperEndpoints.EndpointOffers[0]);

	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)}, {0, 1, 2}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(8, 0, 0),
		FIntVector(7, 0, 0),
		FIntVector(6, 0, 0),
		FIntVector(5, 0, 0),
		FIntVector(4, 1, 2)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(8, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(7, 0, 0),
		{ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(6, 0, 0),
		{ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(5, 0, 0),
		{ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(4, 1, 2),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(8, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(7, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(6, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(5, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(4, 1, 2), 7);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(7, 0, 0)};
	ParentSummary.RouteSupportVerticalAccessCells = {FIntVector(7, 0, 0)};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 7;
	ContactFamily.ParentContactCells = {
		FIntVector(5, 0, 0),
		FIntVector(4, 1, 2)
	};
	FLayoutCommittedEndpointAnchor& LowerEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	LowerEndpoint.CommitmentId = TEXT("LowerDoor");
	LowerEndpoint.LocalCell = FIntVector(0, 0, 0);
	LowerEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	LowerEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	LowerEndpoint.TraversalChannels = MakeTags({LayoutGameplayTags::TraversalPrimary});
	FLayoutCommittedEndpointAnchor& UpperEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	UpperEndpoint.CommitmentId = TEXT("UpperDoor");
	UpperEndpoint.LocalCell = FIntVector(0, 0, 2);
	UpperEndpoint.FaceDirection = ELayoutFaceDirection::PosY;
	UpperEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	UpperEndpoint.TraversalChannels = MakeTags({LayoutGameplayTags::TraversalPrimary});
	FLayoutCommittedTraversalAnchor& IngressTraversal =
		ContactFamily.ParentTraversalIngressAnchors.AddDefaulted_GetRef();
	IngressTraversal.Cell = FIntVector(5, 0, 0);
	IngressTraversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;

	const FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(
			SolveContext,
			DemandPlan,
			ParentSummary,
			ContactFamily);
	const FLayoutNegotiatedChildResponsibilityContract& Contract =
		ResponsibilitySet.ResponsibilityContract;

	TestEqual(
		TEXT("Exact-single negotiation treats a lone lower route-network parent handoff as child-owned instead of composed"),
		Contract.HostVerticalAccessResponsibility,
		ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned);
	TestEqual(TEXT("Route-handoff child-owned contract keeps zero counted parent providers"), Contract.CountedParentProviderCount, 0);
	TestTrue(TEXT("Route-handoff child-owned contract removes the lower route-network parent provider from the counted set"), Contract.CountedParentVerticalAccessCells.IsEmpty());
	TestTrue(TEXT("Route-handoff child-owned contract still counts the negotiated child provider"), Contract.CountedChildProviderRegionDebugPaths.Contains(DemandPlan.ChildRegionDebugPath));

	FString ContractFailureReason;
	TestTrue(
		TEXT("Route-handoff child-owned contract satisfies the negotiated-contract validator"),
		LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
			Contract,
			ContractFailureReason));
	if (!ContractFailureReason.IsEmpty())
	{
		AddInfo(ContractFailureReason);
	}
	return true;
}

bool FLayoutResponsibilityNegotiationKeepsExactSingleUpperRouteSupportProviderComposedTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCountMode =
		ELayoutCountConstraintMode::Exact;
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCount = 1;
	SolveContext.RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(8, 0, 0), ELayoutCellIntent::Entry)
	};

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/UpperRouteSupportChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/UpperRouteSupportChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ChildRequest.ProfileSnapshot.LevelCount = 2;
	DemandPlan.ChildRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(0, 0, 2), ELayoutCellIntent::Connector)
	};
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess = true;
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsComposedHostAccess = true;
	DemandPlan.ChildSummary.VerticalAccessCells = {FIntVector(0, 0, 1)};
	DemandPlan.FeatureFlags.bRequiresMultiLevelAnchorSet = true;
	DemandPlan.FeatureFlags.bAllowsChildTraversalBridge = true;
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};

	FChildLevelEndpointCapabilitySummary& LowerEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpoints.LevelIndex = 0;
	LowerEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));

	FChildLevelEndpointCapabilitySummary& UpperEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpoints.LevelIndex = 2;
	UpperEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 2), ELayoutFaceDirection::PosY, PrimaryTraversal));
	PopulateGenerallyConnectableHostTraversalSummary(
		DemandPlan.ChildSummary,
		LowerEndpoints.EndpointOffers[0],
		UpperEndpoints.EndpointOffers[0]);

	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)}, {0, 1, 2}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(8, 0, 0),
		FIntVector(7, 0, 0),
		FIntVector(6, 0, 0),
		FIntVector(5, 0, 0),
		FIntVector(5, 1, 2),
		FIntVector(4, 1, 2)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(8, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(7, 0, 0),
		{ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(6, 0, 0),
		{ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(5, 0, 0),
		{ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(5, 1, 2),
		{ELayoutFaceDirection::PosY});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(4, 1, 2),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(8, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(7, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(6, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(5, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(5, 1, 2), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(4, 1, 2), 7);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(5, 1, 2)};
	ParentSummary.RouteSupportVerticalAccessCells = {FIntVector(5, 1, 2)};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 7;
	ContactFamily.ParentContactCells = {
		FIntVector(5, 0, 0),
		FIntVector(4, 1, 2)
	};
	FLayoutCommittedEndpointAnchor& LowerEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	LowerEndpoint.CommitmentId = TEXT("LowerDoor");
	LowerEndpoint.LocalCell = FIntVector(0, 0, 0);
	LowerEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	LowerEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	LowerEndpoint.TraversalChannels = MakeTags({LayoutGameplayTags::TraversalPrimary});
	FLayoutCommittedEndpointAnchor& UpperEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	UpperEndpoint.CommitmentId = TEXT("UpperDoor");
	UpperEndpoint.LocalCell = FIntVector(0, 0, 2);
	UpperEndpoint.FaceDirection = ELayoutFaceDirection::PosY;
	UpperEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	UpperEndpoint.TraversalChannels = MakeTags({LayoutGameplayTags::TraversalPrimary});
	FLayoutCommittedTraversalAnchor& IngressTraversal =
		ContactFamily.ParentTraversalIngressAnchors.AddDefaulted_GetRef();
	IngressTraversal.Cell = FIntVector(5, 0, 0);
	IngressTraversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;

	const FLayoutNegotiatedChildResponsibilityContract& Contract =
		BuildResponsibilitySetFromContactSet(
			SolveContext,
			DemandPlan,
			ParentSummary,
			ContactFamily).ResponsibilityContract;
	TestEqual(
		TEXT("Exact-single negotiation keeps an upper route-support parent provider as composed host ascent"),
		Contract.HostVerticalAccessResponsibility,
		ELayoutNegotiatedHostVerticalAccessResponsibility::Composed);
	TestEqual(TEXT("Upper route-support composed contract keeps one counted parent provider"), Contract.CountedParentProviderCount, 1);
	if (TestEqual(TEXT("Upper route-support composed contract keeps one counted parent provider cell"), Contract.CountedParentVerticalAccessCells.Num(), 1))
	{
		TestEqual(TEXT("Upper route-support composed contract keeps the upper parent provider cell"), Contract.CountedParentVerticalAccessCells[0], FIntVector(5, 1, 2));
	}
	TestTrue(TEXT("Upper route-support composed contract still counts the negotiated child provider"), Contract.CountedChildProviderRegionDebugPaths.Contains(DemandPlan.ChildRegionDebugPath));

	FString ContractFailureReason;
	TestTrue(
		TEXT("Upper route-support composed contract satisfies the negotiated-contract validator"),
		LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
			Contract,
			ContractFailureReason));
	if (!ContractFailureReason.IsEmpty())
	{
		AddInfo(ContractFailureReason);
	}
	return true;
}

bool FLayoutResponsibilityNegotiationUsesRouteSupportFallbackForParentOwnedHostAscentTest::RunTest(const FString& Parameters)
{
	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCountMode =
		ELayoutCountConstraintMode::Exact;
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCount = 1;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ParentOwnedFallbackChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/ParentOwnedFallbackChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ChildRequest.ProfileSnapshot.LevelCount = 1;
	DemandPlan.ChildRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Interior)
	};
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess = false;
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsComposedHostAccess = false;
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0)
	};

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(3, 0, 1),
		FIntVector(4, 0, 0),
		FIntVector(5, 0, 0)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 1),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(4, 0, 0),
		{ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(5, 0, 0),
		{ELayoutFaceDirection::PosX});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 1), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(4, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(5, 0, 0), 7);
	ParentSummary.RouteSupportVerticalAccessCells = {
		FIntVector(3, 0, 0),
		FIntVector(3, 0, 1),
		FIntVector(5, 0, 0)
	};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 7;
	ContactFamily.ParentContactCells = {FIntVector(3, 0, 0)};

	const FLayoutNegotiatedChildResponsibilityContract& Contract =
		BuildResponsibilitySetFromContactSet(
			SolveContext,
			DemandPlan,
			ParentSummary,
			ContactFamily).ResponsibilityContract;

	TestEqual(
		TEXT("Parent-owned negotiation falls back to route-support parent providers when no explicit counted-provider subset was precomputed"),
		Contract.HostVerticalAccessResponsibility,
		ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned);
	TestEqual(TEXT("Parent-owned route-support fallback keeps one counted parent provider"), Contract.CountedParentProviderCount, 1);
	if (TestEqual(TEXT("Parent-owned route-support fallback keeps one counted parent provider cell"), Contract.CountedParentVerticalAccessCells.Num(), 1))
	{
		TestEqual(TEXT("Parent-owned route-support fallback caps the counted parent provider set to the authored exact-single host count"), Contract.CountedParentVerticalAccessCells[0], FIntVector(3, 0, 0));
	}
	TestTrue(TEXT("Parent-owned route-support fallback keeps the child out of the counted host-provider set"), !Contract.CountedChildProviderRegionDebugPaths.Contains(DemandPlan.ChildRegionDebugPath));

	FString ContractFailureReason;
	TestTrue(
		TEXT("Parent-owned route-support fallback contract satisfies the negotiated-contract validator"),
		LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
			Contract,
			ContractFailureReason));
	if (!ContractFailureReason.IsEmpty())
	{
		AddInfo(ContractFailureReason);
	}
	return true;
}

bool FLayoutResponsibilityNegotiationFailsWhenNoPlacementFamilyMeetsRequiredHostProviderCountTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCountMode =
		ELayoutCountConstraintMode::Exact;
	SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCount = 3;

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/NegotiatedChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/NegotiatedChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ChildRequest.ProfileSnapshot.LevelCount = 2;
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess = true;
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsComposedHostAccess = true;
	DemandPlan.ChildSummary.VerticalAccessCells = {FIntVector(0, 0, 1)};
	DemandPlan.FeatureFlags.bRequiresMultiLevelAnchorSet = true;
	DemandPlan.FeatureFlags.bAllowsChildTraversalBridge = true;
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};

	FChildLevelEndpointCapabilitySummary& LowerEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpoints.LevelIndex = 0;
	LowerEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));

	FChildLevelEndpointCapabilitySummary& UpperEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpoints.LevelIndex = 2;
	UpperEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 2), ELayoutFaceDirection::PosY, PrimaryTraversal));

	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)}, {0, 1, 2}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(4, 1, 2)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(4, 1, 2),
		{ELayoutFaceDirection::NegY});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(4, 1, 2), 7);
	ParentSummary.RootConnectedTraversableCells.Add(FIntVector(9, 0, 0));
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(9, 0, 0), 11);
	ParentSummary.CountedParentVerticalAccessCells = {
		FIntVector(3, 0, 0),
		FIntVector(9, 0, 0)
	};

	DemandPlan.ChildRequest.SourceContentEntryId = TEXT("NegotiatedChild");
	const FNegotiatedDemandResult Result =
		NegotiateDemandResponsibilities(SolveContext, DemandPlan, ParentSummary);
	TestFalse(TEXT("Negotiation fails when no surviving family can satisfy the required total host-provider count"), Result.bSucceeded);
	TestTrue(*FString::Printf(TEXT("Failure reason names the unsatisfied exact total host-provider count: %s"), *Result.FailureReason),
		Result.FailureReason.Contains(TEXT("requires exactly 3 total counted host providers")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsChildOwnedResponsibilitySetWithoutChildRouteTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 1, 0),
		FIntVector(6, 2, 2)
	};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(6, 2, 2), 5);

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary})),
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 2), ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& LowerEndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpointSummary.LevelIndex = 0;
	LowerEndpointSummary.EndpointOffers.Add(ChildSummary.EndpointOffers[0]);
	FChildLevelEndpointCapabilitySummary& UpperEndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpointSummary.LevelIndex = 2;
	UpperEndpointSummary.EndpointOffers.Add(ChildSummary.EndpointOffers[1]);
	PopulateGenerallyConnectableHostTraversalSummary(
		ChildSummary,
		LowerEndpointSummary.EndpointOffers[0],
		UpperEndpointSummary.EndpointOffers[0]);
	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;

	FLayoutCommittedEndpointAnchor& LowerEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	LowerEndpoint.CommitmentId = TEXT("LowerDoor");
	LowerEndpoint.LocalCell = FIntVector(0, 0, 0);
	LowerEndpoint.FaceDirection = ELayoutFaceDirection::NegX;
	LowerEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FLayoutCommittedEndpointAnchor& UpperEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	UpperEndpoint.CommitmentId = TEXT("UpperDoor");
	UpperEndpoint.LocalCell = FIntVector(0, 0, 2);
	UpperEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	UpperEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	ContactFamily.PlacementBundle = MakePlacementBundle(
		{FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)},
		{0, 1, 2});

	ContactFamily.ParentContactCells = {
		FIntVector(3, 1, 0),
		FIntVector(6, 2, 2)
	};

	FLayoutCommittedTraversalAnchor& LowerTraversal = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	LowerTraversal.Cell = FIntVector(3, 1, 0);
	LowerTraversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	FLayoutCommittedTraversalAnchor& UpperTraversal = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	UpperTraversal.Cell = FIntVector(6, 2, 2);
	UpperTraversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	ContactFamily.ParentTraversalIngressAnchors = {LowerTraversal};

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);
	ResponsibilitySet.ResponsibilityContract.HostVerticalAccessResponsibility =
		ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned;
	ResponsibilitySet.ResponsibilityContract.CountedChildProviderRegionDebugPaths = {
		DemandPlan.ChildRegionDebugPath
	};
	ResponsibilitySet.ResponsibilityContract.bHasRequiredHostIngressAnchor = true;
	ResponsibilitySet.ResponsibilityContract.RequiredHostIngressAnchor = LowerEndpoint;
	ResponsibilitySet.ResponsibilityContract.bHasRequiredHostEgressAnchor = true;
	ResponsibilitySet.ResponsibilityContract.RequiredHostEgressAnchor = UpperEndpoint;
	ResponsibilitySet.ResponsibilityContract.RequiredChildGenerallyConnectableAnchorPairId = NAME_None;
	ResponsibilitySet.ResponsibilityContract.RequiredChildInternalVerticalSpanLevels.Reset();
	ResponsibilitySet.ResponsibilityContract.RequiredChildInternalVerticalRouteCells.Reset();

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a child-owned contract that omits both the coarse anchor-pair proof and compatibility route cells"), bConfirmed);
	TestTrue(TEXT("Failure reason names the missing coarse child proof surface"), FailureReason.Contains(TEXT("generally connectable child anchor-pair proof")) || FailureReason.Contains(TEXT("compatibility route cells")));
	return true;
}

bool FLayoutResponsibilityNegotiationSelectsChildOwnedHostAscentFromRouteBackedContactsWithoutAnchorPairProofTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/SharedShellChild");
	DemandPlan.ChildRequest.RegionDebugPath = TEXT("Root/SharedShellChild");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ChildRequest.ProfileSnapshot.LevelCount = 2;
	DemandPlan.ChildRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(0, 0, 1), ELayoutCellIntent::VerticalAccess),
		MakePlannedCell(FIntVector(0, 0, 2), ELayoutCellIntent::Connector)
	};
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess = true;
	DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsComposedHostAccess = true;
	DemandPlan.ChildSummary.VerticalAccessCells = {FIntVector(0, 0, 1)};
	DemandPlan.FeatureFlags.bRequiresMultiLevelAnchorSet = true;
	DemandPlan.FeatureFlags.bAllowsChildTraversalBridge = true;
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};

	FChildLevelEndpointCapabilitySummary& LowerEndpoints =
		DemandPlan.ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpoints.LevelIndex = 0;
	LowerEndpoints.EndpointOffers.Add(
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX, PrimaryTraversal));

	DemandPlan.ChildSummary.PlacementBundles.Add(
		MakePlacementBundle({FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)}, {0, 1, 2}));

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 0, 0),
		FIntVector(4, 1, 2)
	};
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(3, 0, 0),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedTraversableFacesByCell.Add(
		FIntVector(4, 1, 2),
		{ELayoutFaceDirection::NegX});
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 0, 0), 7);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(4, 1, 2), 7);

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 7;
	ContactFamily.EndpointCommitments = {
		FLayoutCommittedEndpointAnchor(),
		FLayoutCommittedEndpointAnchor()
	};
	ContactFamily.EndpointCommitments[0].CommitmentId = TEXT("LowerDoor");
	ContactFamily.EndpointCommitments[0].LocalCell = FIntVector(0, 0, 0);
	ContactFamily.EndpointCommitments[0].FaceDirection = ELayoutFaceDirection::PosX;
	ContactFamily.EndpointCommitments[0].ConnectionTag = LayoutGameplayTags::FaceOpen;
	ContactFamily.EndpointCommitments[1].CommitmentId = TEXT("UpperSharedShell");
	ContactFamily.EndpointCommitments[1].LocalCell = FIntVector(0, 0, 2);
	ContactFamily.EndpointCommitments[1].FaceDirection = ELayoutFaceDirection::PosX;
	ContactFamily.EndpointCommitments[1].ConnectionTag = LayoutGameplayTags::FaceOpen;
	ContactFamily.PlacementBundle = MakePlacementBundle(
		{FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)},
		{0, 1, 2});
	ContactFamily.ParentContactCells = {
		FIntVector(3, 0, 0),
		FIntVector(4, 1, 2)
	};
	FLayoutCommittedTraversalAnchor& LowerTraversal =
		ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	LowerTraversal.Cell = FIntVector(3, 0, 0);
	LowerTraversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	ContactFamily.ParentTraversalIngressAnchors = {LowerTraversal};

	const FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(
			SolveContext,
			DemandPlan,
			ParentSummary,
			ContactFamily);
	const FLayoutNegotiatedChildResponsibilityContract& Contract =
		ResponsibilitySet.ResponsibilityContract;

	TestEqual(
		TEXT("Route-backed multi-level contacts still classify the host ascent as child-owned when no counted parent providers remain"),
		Contract.HostVerticalAccessResponsibility,
		ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned);
	TestEqual(TEXT("Route-backed child-owned contract keeps zero counted parent providers"), Contract.CountedParentProviderCount, 0);
	TestTrue(TEXT("Route-backed child-owned contract counts the negotiated child provider"), Contract.CountedChildProviderRegionDebugPaths.Contains(DemandPlan.ChildRegionDebugPath));
	TestTrue(TEXT("Route-backed child-owned contract still fixes a lower host ingress anchor"), Contract.bHasRequiredHostIngressAnchor);
	TestTrue(TEXT("Route-backed child-owned contract still fixes an upper host egress anchor"), Contract.bHasRequiredHostEgressAnchor);
	TestEqual<FLayoutId>(TEXT("Route-backed child-owned contract does not fabricate a generally connectable endpoint pair proof"), Contract.RequiredChildGenerallyConnectableAnchorPairId, NAME_None);
	TestEqual(TEXT("Route-backed child-owned contract keeps the stacked internal ascent route"), Contract.RequiredChildInternalVerticalRouteCells.Num(), 3);

	FString ContractFailureReason;
	TestTrue(
		TEXT("Route-backed child-owned contract still satisfies the negotiated-contract validator"),
		LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
			Contract,
			ContractFailureReason));
	if (!ContractFailureReason.IsEmpty())
	{
		AddInfo(ContractFailureReason);
	}

	return true;
}

bool FLayoutResponsibilityNegotiationRejectsChildOwnedResponsibilitySetWithoutIntermediateRouteLevelCoverageTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 1, 0),
		FIntVector(6, 2, 2)
	};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(6, 2, 2), 5);

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary})),
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 2), ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& LowerEndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpointSummary.LevelIndex = 0;
	LowerEndpointSummary.EndpointOffers.Add(ChildSummary.EndpointOffers[0]);
	FChildLevelEndpointCapabilitySummary& UpperEndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpointSummary.LevelIndex = 2;
	UpperEndpointSummary.EndpointOffers.Add(ChildSummary.EndpointOffers[1]);
	PopulateGenerallyConnectableHostTraversalSummary(
		ChildSummary,
		LowerEndpointSummary.EndpointOffers[0],
		UpperEndpointSummary.EndpointOffers[0]);
	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;

	FLayoutCommittedEndpointAnchor& LowerEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	LowerEndpoint.CommitmentId = TEXT("LowerDoor");
	LowerEndpoint.LocalCell = FIntVector(0, 0, 0);
	LowerEndpoint.FaceDirection = ELayoutFaceDirection::NegX;
	LowerEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FLayoutCommittedEndpointAnchor& UpperEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	UpperEndpoint.CommitmentId = TEXT("UpperDoor");
	UpperEndpoint.LocalCell = FIntVector(0, 0, 2);
	UpperEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	UpperEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	ContactFamily.PlacementBundle = MakePlacementBundle(
		{FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)},
		{0, 1, 2});

	ContactFamily.ParentContactCells = {
		FIntVector(3, 1, 0),
		FIntVector(6, 2, 2)
	};

	FLayoutCommittedTraversalAnchor& LowerTraversal = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	LowerTraversal.Cell = FIntVector(3, 1, 0);
	LowerTraversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	FLayoutCommittedTraversalAnchor& UpperTraversal = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	UpperTraversal.Cell = FIntVector(6, 2, 2);
	UpperTraversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	ContactFamily.ParentTraversalIngressAnchors = {LowerTraversal};

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);
	ResponsibilitySet.ResponsibilityContract.HostVerticalAccessResponsibility =
		ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned;
	ResponsibilitySet.ResponsibilityContract.CountedChildProviderRegionDebugPaths = {
		DemandPlan.ChildRegionDebugPath
	};
	ResponsibilitySet.ResponsibilityContract.bHasRequiredHostIngressAnchor = true;
	ResponsibilitySet.ResponsibilityContract.RequiredHostIngressAnchor = LowerEndpoint;
	ResponsibilitySet.ResponsibilityContract.bHasRequiredHostEgressAnchor = true;
	ResponsibilitySet.ResponsibilityContract.RequiredHostEgressAnchor = UpperEndpoint;
	ResponsibilitySet.ResponsibilityContract.RequiredChildGenerallyConnectableAnchorPairId =
		BuildConnectableAnchorPairId(LowerEndpoint, UpperEndpoint);
	ResponsibilitySet.ResponsibilityContract.RequiredChildInternalVerticalSpanLevels = {
		0,
		2
	};
	ResponsibilitySet.ResponsibilityContract.RequiredChildInternalVerticalRouteCells.Reset();

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a child-owned contract whose coarse anchor-pair proof skips an intermediate ascent level"), bConfirmed);
	TestTrue(TEXT("Failure reason names the missing intermediate ascent level coverage"), FailureReason.Contains(TEXT("cover ascent level 1")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsComposedResponsibilitySetWithoutCountedParentProvidersTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 1, 0),
		FIntVector(6, 2, 2)
	};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(6, 2, 2), 5);

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary})),
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 2), ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& LowerEndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpointSummary.LevelIndex = 0;
	LowerEndpointSummary.EndpointOffers.Add(ChildSummary.EndpointOffers[0]);
	FChildLevelEndpointCapabilitySummary& UpperEndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpointSummary.LevelIndex = 2;
	UpperEndpointSummary.EndpointOffers.Add(ChildSummary.EndpointOffers[1]);
	PopulateGenerallyConnectableHostTraversalSummary(
		ChildSummary,
		LowerEndpointSummary.EndpointOffers[0],
		UpperEndpointSummary.EndpointOffers[0]);
	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;

	FLayoutCommittedEndpointAnchor& LowerEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	LowerEndpoint.CommitmentId = TEXT("LowerDoor");
	LowerEndpoint.LocalCell = FIntVector(0, 0, 0);
	LowerEndpoint.FaceDirection = ELayoutFaceDirection::NegX;
	LowerEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FLayoutCommittedEndpointAnchor& UpperEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	UpperEndpoint.CommitmentId = TEXT("UpperDoor");
	UpperEndpoint.LocalCell = FIntVector(0, 0, 2);
	UpperEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	UpperEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	ContactFamily.PlacementBundle = MakePlacementBundle(
		{FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)},
		{0, 1, 2});

	ContactFamily.ParentContactCells = {
		FIntVector(3, 1, 0),
		FIntVector(6, 2, 2)
	};

	FLayoutCommittedTraversalAnchor& LowerTraversal = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	LowerTraversal.Cell = FIntVector(3, 1, 0);
	LowerTraversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	FLayoutCommittedTraversalAnchor& UpperTraversal = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	UpperTraversal.Cell = FIntVector(6, 2, 2);
	UpperTraversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	ContactFamily.ParentTraversalIngressAnchors = {LowerTraversal};

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);
	ResponsibilitySet.ResponsibilityContract.HostVerticalAccessResponsibility =
		ELayoutNegotiatedHostVerticalAccessResponsibility::Composed;
	ResponsibilitySet.ResponsibilityContract.CountedChildProviderRegionDebugPaths = {
		DemandPlan.ChildRegionDebugPath
	};
	ResponsibilitySet.ResponsibilityContract.bHasRequiredHostIngressAnchor = true;
	ResponsibilitySet.ResponsibilityContract.RequiredHostIngressAnchor = LowerEndpoint;
	ResponsibilitySet.ResponsibilityContract.bHasRequiredHostEgressAnchor = true;
	ResponsibilitySet.ResponsibilityContract.RequiredHostEgressAnchor = UpperEndpoint;
	ResponsibilitySet.ResponsibilityContract.RequiredChildInternalVerticalRouteCells = {
		FIntVector(0, 0, 0),
		FIntVector(0, 0, 1),
		FIntVector(0, 0, 2)
	};
	ResponsibilitySet.ResponsibilityContract.CountedParentProviderCount = 0;
	ResponsibilitySet.ResponsibilityContract.CountedParentVerticalAccessCells.Reset();

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a composed contract that keeps no counted parent providers"), bConfirmed);
	TestTrue(TEXT("Failure reason names the missing counted parent providers"), FailureReason.Contains(TEXT("marked Composed, but negotiation retained no counted parent providers")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsResponsibilitySetWithCountedParentProviderOutsideResidualSummaryTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 1, 0),
		FIntVector(6, 2, 2)
	};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(6, 2, 2), 5);
	ParentSummary.CountedParentVerticalAccessCells = {FIntVector(3, 1, 0)};

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary})),
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 2), ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& LowerEndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpointSummary.LevelIndex = 0;
	LowerEndpointSummary.EndpointOffers.Add(ChildSummary.EndpointOffers[0]);
	FChildLevelEndpointCapabilitySummary& UpperEndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpointSummary.LevelIndex = 2;
	UpperEndpointSummary.EndpointOffers.Add(ChildSummary.EndpointOffers[1]);
	PopulateGenerallyConnectableHostTraversalSummary(
		ChildSummary,
		LowerEndpointSummary.EndpointOffers[0],
		UpperEndpointSummary.EndpointOffers[0]);
	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;
	ContactFamily.PlacementBundle = MakePlacementBundle(
		{FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)},
		{0, 1, 2});

	FLayoutCommittedEndpointAnchor& LowerEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	LowerEndpoint.CommitmentId = TEXT("LowerDoor");
	LowerEndpoint.LocalCell = FIntVector(0, 0, 0);
	LowerEndpoint.FaceDirection = ELayoutFaceDirection::NegX;
	LowerEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FLayoutCommittedEndpointAnchor& UpperEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	UpperEndpoint.CommitmentId = TEXT("UpperDoor");
	UpperEndpoint.LocalCell = FIntVector(0, 0, 2);
	UpperEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	UpperEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	ContactFamily.ParentContactCells = {
		FIntVector(3, 1, 0),
		FIntVector(6, 2, 2)
	};

	FLayoutCommittedTraversalAnchor& LowerTraversal = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	LowerTraversal.Cell = FIntVector(3, 1, 0);
	LowerTraversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	ContactFamily.ParentTraversalIngressAnchors = {LowerTraversal};

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);
	ResponsibilitySet.ResponsibilityContract.HostVerticalAccessResponsibility =
		ELayoutNegotiatedHostVerticalAccessResponsibility::Composed;
	ResponsibilitySet.ResponsibilityContract.CountedChildProviderRegionDebugPaths = {
		DemandPlan.ChildRegionDebugPath
	};
	ResponsibilitySet.ResponsibilityContract.bHasRequiredHostIngressAnchor = true;
	ResponsibilitySet.ResponsibilityContract.RequiredHostIngressAnchor = LowerEndpoint;
	ResponsibilitySet.ResponsibilityContract.bHasRequiredHostEgressAnchor = true;
	ResponsibilitySet.ResponsibilityContract.RequiredHostEgressAnchor = UpperEndpoint;
	ResponsibilitySet.ResponsibilityContract.RequiredChildInternalVerticalRouteCells = {
		FIntVector(0, 0, 0),
		FIntVector(0, 0, 1),
		FIntVector(0, 0, 2)
	};
	ResponsibilitySet.ResponsibilityContract.CountedParentProviderCount = 1;
	ResponsibilitySet.ResponsibilityContract.CountedParentVerticalAccessCells = {
		FIntVector(9, 9, 0)
	};

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a negotiated counted parent provider cell that is not present in the residual parent summary"), bConfirmed);
	TestTrue(TEXT("Failure reason names the counted parent provider mismatch"), FailureReason.Contains(TEXT("counted parent provider cell")) && FailureReason.Contains(TEXT("residual parent counted provider set")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsChildOwnedResponsibilitySetWithHostAnchorOutsideCommittedEndpointsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 1, 0),
		FIntVector(6, 2, 2)
	};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(6, 2, 2), 5);

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary})),
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 2), ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& LowerEndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpointSummary.LevelIndex = 0;
	LowerEndpointSummary.EndpointOffers.Add(ChildSummary.EndpointOffers[0]);
	FChildLevelEndpointCapabilitySummary& UpperEndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpointSummary.LevelIndex = 2;
	UpperEndpointSummary.EndpointOffers.Add(ChildSummary.EndpointOffers[1]);
	PopulateGenerallyConnectableHostTraversalSummary(
		ChildSummary,
		LowerEndpointSummary.EndpointOffers[0],
		UpperEndpointSummary.EndpointOffers[0]);

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;
	ContactFamily.PlacementBundle = MakePlacementBundle(
		{FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)},
		{0, 1, 2});

	FLayoutCommittedEndpointAnchor& LowerEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	LowerEndpoint.CommitmentId = TEXT("LowerDoor");
	LowerEndpoint.LocalCell = FIntVector(0, 0, 0);
	LowerEndpoint.FaceDirection = ELayoutFaceDirection::NegX;
	LowerEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FLayoutCommittedEndpointAnchor& UpperEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	UpperEndpoint.CommitmentId = TEXT("UpperDoor");
	UpperEndpoint.LocalCell = FIntVector(0, 0, 2);
	UpperEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	UpperEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	ContactFamily.ParentContactCells = {
		FIntVector(3, 1, 0),
		FIntVector(6, 2, 2)
	};

	FLayoutCommittedTraversalAnchor& LowerTraversal = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	LowerTraversal.Cell = FIntVector(3, 1, 0);
	LowerTraversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	ContactFamily.ParentTraversalIngressAnchors = {LowerTraversal};

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);
	ResponsibilitySet.ResponsibilityContract.HostVerticalAccessResponsibility =
		ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned;
	ResponsibilitySet.ResponsibilityContract.CountedChildProviderRegionDebugPaths = {
		DemandPlan.ChildRegionDebugPath
	};
	ResponsibilitySet.ResponsibilityContract.bHasRequiredHostIngressAnchor = true;
	ResponsibilitySet.ResponsibilityContract.RequiredHostIngressAnchor.CommitmentId = TEXT("WrongDoor");
	ResponsibilitySet.ResponsibilityContract.RequiredHostIngressAnchor.LocalCell = FIntVector(1, 0, 0);
	ResponsibilitySet.ResponsibilityContract.RequiredHostIngressAnchor.FaceDirection = ELayoutFaceDirection::NegY;
	ResponsibilitySet.ResponsibilityContract.RequiredHostIngressAnchor.ConnectionTag = LayoutGameplayTags::FaceOpen;
	ResponsibilitySet.ResponsibilityContract.bHasRequiredHostEgressAnchor = true;
	ResponsibilitySet.ResponsibilityContract.RequiredHostEgressAnchor = UpperEndpoint;
	ResponsibilitySet.ResponsibilityContract.RequiredChildInternalVerticalRouteCells = {
		FIntVector(0, 0, 0),
		FIntVector(0, 0, 1),
		FIntVector(0, 0, 2)
	};

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a child-owned contract whose required host anchor is not part of the chosen negotiated endpoint commitments"), bConfirmed);
	TestTrue(TEXT("Failure reason names the missing committed host anchor"), FailureReason.Contains(TEXT("required host ingress anchor")) && FailureReason.Contains(TEXT("chosen negotiated endpoint commitment set")));
	return true;
}

bool FLayoutResponsibilityNegotiationRejectsChildOwnedResponsibilitySetWithRouteOutsidePlacementBundleTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");

	FResidualParentCapabilitySummary ParentSummary;
	ParentSummary.RootConnectedTraversableCells = {
		FIntVector(3, 1, 0),
		FIntVector(6, 2, 2)
	};
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(3, 1, 0), 5);
	ParentSummary.RootConnectedComponentIdByCell.Add(FIntVector(6, 2, 2), 5);

	FChildCapabilitySummary ChildSummary;
	ChildSummary.EndpointOffers = {
		MakeEndpointOffer(TEXT("LowerDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::TraversalPrimary})),
		MakeEndpointOffer(TEXT("UpperDoor"), FIntVector(0, 0, 2), ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::TraversalPrimary}))
	};
	FChildLevelEndpointCapabilitySummary& LowerEndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	LowerEndpointSummary.LevelIndex = 0;
	LowerEndpointSummary.EndpointOffers.Add(ChildSummary.EndpointOffers[0]);
	FChildLevelEndpointCapabilitySummary& UpperEndpointSummary = ChildSummary.EndpointOffersByLevel.AddDefaulted_GetRef();
	UpperEndpointSummary.LevelIndex = 2;
	UpperEndpointSummary.EndpointOffers.Add(ChildSummary.EndpointOffers[1]);

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0),
		FIntVector(4, 1, 1),
		FIntVector(4, 1, 2)
	};

	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentComponentId = 5;
	ContactFamily.PlacementBundle = MakePlacementBundle(
		{FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, 2)},
		{0, 1, 2});

	FLayoutCommittedEndpointAnchor& LowerEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	LowerEndpoint.CommitmentId = TEXT("LowerDoor");
	LowerEndpoint.LocalCell = FIntVector(0, 0, 0);
	LowerEndpoint.FaceDirection = ELayoutFaceDirection::NegX;
	LowerEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FLayoutCommittedEndpointAnchor& UpperEndpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	UpperEndpoint.CommitmentId = TEXT("UpperDoor");
	UpperEndpoint.LocalCell = FIntVector(0, 0, 2);
	UpperEndpoint.FaceDirection = ELayoutFaceDirection::PosX;
	UpperEndpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	ContactFamily.ParentContactCells = {
		FIntVector(3, 1, 0),
		FIntVector(6, 2, 2)
	};

	FLayoutCommittedTraversalAnchor& LowerTraversal = ContactFamily.AllCommittedContactTraversalAnchors.AddDefaulted_GetRef();
	LowerTraversal.Cell = FIntVector(3, 1, 0);
	LowerTraversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	ContactFamily.ParentTraversalIngressAnchors = {LowerTraversal};

	FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);
	ResponsibilitySet.ResponsibilityContract.HostVerticalAccessResponsibility =
		ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned;
	ResponsibilitySet.ResponsibilityContract.CountedChildProviderRegionDebugPaths = {
		DemandPlan.ChildRegionDebugPath
	};
	ResponsibilitySet.ResponsibilityContract.bHasRequiredHostIngressAnchor = true;
	ResponsibilitySet.ResponsibilityContract.RequiredHostIngressAnchor = LowerEndpoint;
	ResponsibilitySet.ResponsibilityContract.bHasRequiredHostEgressAnchor = true;
	ResponsibilitySet.ResponsibilityContract.RequiredHostEgressAnchor = UpperEndpoint;
	ResponsibilitySet.ResponsibilityContract.RequiredChildInternalVerticalRouteCells = {
		FIntVector(0, 0, 0),
		FIntVector(1, 0, 1),
		FIntVector(0, 0, 2)
	};

	FString FailureReason;
	const bool bConfirmed = ConfirmNegotiatedResponsibilitySet(
		RootRequest,
		ParentSummary,
		ChildSummary,
		ResponsibilitySet,
		FailureReason);
	TestFalse(TEXT("Confirmation rejects a child-owned contract whose internal route escapes the selected placement bundle"), bConfirmed);
	TestTrue(TEXT("Failure reason names the placement-bundle route mismatch"), FailureReason.Contains(TEXT("vertical-route cell")) && FailureReason.Contains(TEXT("selected placement bundle")));
	return true;
}

bool FLayoutResponsibilityNegotiationBuildsFallbackRequiredHostProviderCountFromParentPlanTest::RunTest(const FString& Parameters)
{
	FRecursiveScheduleSolveContext SolveContext;
	SolveContext.RootRequest.RegionDebugPath = TEXT("Root");
	SolveContext.ParentPlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::VerticalAccess)
	};

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ChildA");
	DemandPlan.ChildRequest.SourceParentRegionDebugPath = TEXT("Root");
	DemandPlan.ReservedParentCells = {
		FIntVector(4, 1, 0)
	};

	FResidualParentCapabilitySummary ParentSummary;
	FContactBackedPlacementFamily ContactFamily;
	ContactFamily.ParentContactCells = {FIntVector(3, 1, 0)};

	FLayoutCommittedEndpointAnchor& Endpoint = ContactFamily.EndpointCommitments.AddDefaulted_GetRef();
	Endpoint.CommitmentId = TEXT("Door");
	Endpoint.LocalCell = FIntVector(0, 0, 0);
	Endpoint.FaceDirection = ELayoutFaceDirection::NegX;
	Endpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;

	FLayoutCommittedTraversalAnchor& Traversal = ContactFamily.ParentTraversalIngressAnchors.AddDefaulted_GetRef();
	Traversal.Cell = FIntVector(3, 1, 0);
	Traversal.TraversalChannel = LayoutGameplayTags::TraversalPrimary;

	const FNegotiatedResponsibilitySet ResponsibilitySet =
		BuildResponsibilitySetFromContactSet(SolveContext, DemandPlan, ParentSummary, ContactFamily);
	TestEqual(
		TEXT("Responsibility contract falls back to one required host provider when the root plan contains parent vertical access and no explicit count is authored"),
		ResponsibilitySet.ResponsibilityContract.RequiredHostProviderCount,
		1);
	return true;
}
