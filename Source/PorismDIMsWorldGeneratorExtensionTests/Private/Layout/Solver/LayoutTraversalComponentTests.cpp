// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Algo/Reverse.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;

	LayoutProfileSolverInternal::FSolveContext BuildTraversalComponentContext(
		const bool bReverseInsertion)
	{
		using namespace LayoutProfileSolverInternal;
		FSolveContext Context;
		Context.Seed = 99;

		FSolveContext::FOrientedModuleVariant& TraversalVariant =
			Context.Variants.AddDefaulted_GetRef();
		TraversalVariant.ModuleSnapshotId = TEXT("TraversalVariant");
		for (const ELayoutFaceDirection Direction : {
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY})
		{
			FLayoutFaceRule Rule;
			Rule.Direction = Direction;
			Rule.ConnectionTag = LayoutGameplayTags::FaceOpen;
			Rule.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
			Rule.OccupancyPolicy = ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor;
			Rule.ConnectedTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
			TraversalVariant.WorldFaceRules.SetRule(Rule);
		}
		FSolveContext::FOrientedModuleVariant& NonTraversalVariant =
			Context.Variants.AddDefaulted_GetRef();
		NonTraversalVariant.ModuleSnapshotId = TEXT("NonTraversalVariant");

		FSolveCandidate TraversalCandidate;
		TraversalCandidate.bEmpty = false;
		TraversalCandidate.VariantIndex = 0;
		TraversalCandidate.ModuleSnapshotId = TraversalVariant.ModuleSnapshotId;
		FSolveCandidate NonTraversalCandidate;
		NonTraversalCandidate.bEmpty = false;
		NonTraversalCandidate.VariantIndex = 1;
		NonTraversalCandidate.ModuleSnapshotId = NonTraversalVariant.ModuleSnapshotId;

		TArray<FIntVector> Cells = {
			FIntVector(0, 0, 1),
			FIntVector(1, 0, 1),
			FIntVector(2, 0, 1),
			FIntVector(3, 0, 1),
			FIntVector(4, 0, 1)};
		if (bReverseInsertion)
		{
			Algo::Reverse(Cells);
		}
		for (const FIntVector& Cell : Cells)
		{
			Context.InitialDomains.Add(Cell, {TraversalCandidate});
		}
		Context.InitialDomains.Add(FIntVector(7, 0, 1), {NonTraversalCandidate});
		Context.TerrainResidualRuleIdByCell.Add(FIntVector(2, 0, 1), TEXT("Preserve"));
		return Context;
	}

	FLayoutRegionSolveRequest BuildBlockedTraversalRequest(
		UObject* Outer,
		const bool bStrictReachability)
	{
		const FGameplayTagContainer Open = MakeTags({LayoutGameplayTags::FaceOpen});
		const FGameplayTagContainer OpenAndEntry = MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry});
		ULayoutModuleAsset* EntryModule = CreateModule(
			Outer,
			TEXT("BlockedTraversalEntry"),
			CreateTemplate(Outer, TEXT("BlockedTraversalEntryTemplate"), FIntVector(8, 8, 8)),
			{ELayoutCellIntent::Entry},
			{
				MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceEntry, LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceEntry, LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceEntry, LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceEntry, LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceOpen, Open, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
				MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceOpen, Open, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
			});
		ULayoutModuleAsset* BlockerModule = CreateModule(
			Outer,
			TEXT("BlockedTraversalMiddle"),
			CreateTemplate(Outer, TEXT("BlockedTraversalMiddleTemplate"), FIntVector(8, 8, 8)),
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core},
			BuildFilledCubeFaces(
				Open,
				OpenAndEntry,
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				Open,
				Open,
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
		ULayoutModuleAsset* WalkableModule = CreateModule(
			Outer,
			TEXT("BlockedTraversalWalkable"),
			CreateTemplate(Outer, TEXT("BlockedTraversalWalkableTemplate"), FIntVector(8, 8, 8)),
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core, ELayoutCellIntent::Connector},
			BuildFilledCubeFaces(
				Open,
				OpenAndEntry,
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				Open,
				Open,
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})));

		FLayoutRegionContentEntry EntryContent;
		EntryContent.EntryId = TEXT("Entry");
		EntryContent.ContentKind = ELayoutRegionContentKind::Module;
		EntryContent.ModuleSettings.Module = EntryModule;
		FLayoutRegionContentEntry BlockerContent;
		BlockerContent.EntryId = TEXT("Blocker");
		BlockerContent.ContentKind = ELayoutRegionContentKind::Module;
		BlockerContent.ModuleSettings.Module = BlockerModule;
		FLayoutRegionContentEntry WalkableContent;
		WalkableContent.EntryId = TEXT("Walkable");
		WalkableContent.ContentKind = ELayoutRegionContentKind::Module;
		WalkableContent.ModuleSettings.Module = WalkableModule;
		ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
			Outer,
			TEXT("BlockedTraversalContent"),
			{EntryContent, BlockerContent, WalkableContent});
		ULayoutProfileAsset* Profile = CreateProfile(
			Outer,
			TEXT("BlockedTraversalProfile"),
			FIntPoint(5, 3),
			FIntPoint(5, 3),
			1,
			2,
			false);
		Profile->ContentSet = ContentSet;
		Profile->bRequireAllTraversalChannelsReachable = bStrictReachability;

		FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ContentSet,
			Profile,
			313,
			TEXT("BlockedTraversal"));
		Request.FootprintSize = FIntPoint(5, 3);
		for (int32 Y = 0; Y < 3; ++Y)
		{
			for (int32 X = 0; X < 5; ++X)
			{
				FLayoutPlannedCell& Cell = Request.PlannedCells.AddDefaulted_GetRef();
				Cell.Cell = FIntVector(X, Y, 0);
				const bool bEntry = Y == 1 && (X == 0 || X == 4);
				const bool bPerimeter = X == 0 || X == 4 || Y == 0 || Y == 2;
				const bool bCorner = (X == 0 || X == 4) && (Y == 0 || Y == 2);
				Cell.Intent = bEntry
					? ELayoutCellIntent::Entry
					: bPerimeter ? ELayoutCellIntent::Boundary : ELayoutCellIntent::Interior;
				Cell.PlacementZone = bCorner
					? ELayoutPlacementZone::Corner
					: bPerimeter ? ELayoutPlacementZone::Edge : ELayoutPlacementZone::Interior;
				Cell.ModuleLevelIndex = 0;
			}
		}
		Request.PrecomputedPlannedCells = Request.PlannedCells;
		Request.bHasFinalizedSteppedTerrainIntents = true;
		const FLayoutModuleSolveSnapshot* BlockerSnapshot =
			Request.ModuleCatalog.Modules.FindByPredicate(
				[](const FLayoutModuleSolveSnapshot& Module)
				{
					return Module.DebugName == TEXT("BlockedTraversalMiddle");
				});
		if (BlockerSnapshot != nullptr)
		{
			Request.CandidateDomainCertificateId = TEXT("BlockedTraversalDomain");
			for (int32 Y = 0; Y < 3; ++Y)
			{
				FLayoutCellCandidateDomainRestriction& Restriction =
					Request.CandidateDomainRestrictions.AddDefaulted_GetRef();
				Restriction.RestrictionId = FLayoutId(*FString::Printf(TEXT("BlockedTraversalBarrier%d"), Y));
				Restriction.Cell = FIntVector(2, Y, 0);
				FLayoutCandidateVariantIdentity& Identity =
					Restriction.AllowedCandidates.AddDefaulted_GetRef();
				Identity.ModuleSnapshotId = BlockerSnapshot->SnapshotId;
				Identity.YawRotationSteps = 0;
			}
		}
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTraversalComponentStableRepresentativeTest,
	"PorismExtension.Layout.Solver.TraversalComponents.StableRepresentative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Proves exact domains form stable components while preserved sparse cells and anchored components are excluded. */
bool FLayoutTraversalComponentStableRepresentativeTest::RunTest(const FString& Parameters)
{
	const TArray<FIntVector> ExistingAnchors = {FIntVector(0, 0, 1)};
	const TArray<FIntVector> Forward =
		LayoutProfileSolverInternal::BuildTraversalComponentRepresentativesForTests(
			BuildTraversalComponentContext(false),
			ExistingAnchors);
	const TArray<FIntVector> Reverse =
		LayoutProfileSolverInternal::BuildTraversalComponentRepresentativesForTests(
			BuildTraversalComponentContext(true),
			ExistingAnchors);

	TestEqual(TEXT("Only unanchored structural component receives a representative"), Forward.Num(), 1);
	if (Forward.Num() == 1)
	{
		TestEqual(TEXT("Nearest deterministic component cell is selected"), Forward[0], FIntVector(3, 0, 1));
	}
	TestEqual(TEXT("Insertion order does not change representative count"), Reverse.Num(), Forward.Num());
	if (Forward.Num() == Reverse.Num() && !Forward.IsEmpty())
	{
		TestEqual(TEXT("Same seed and domains repeat representative selection"), Reverse[0], Forward[0]);
	}
	return true;
}

/** Proves identical blocked topology is fatal only under strict reachability. */
