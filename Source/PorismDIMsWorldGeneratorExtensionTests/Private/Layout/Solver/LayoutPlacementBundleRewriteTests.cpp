// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionScheduleSolverFacade.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutSolveSnapshotValidation.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace LayoutRegionScheduleSolverFacade;
	using namespace PorismLayoutTestUtilities;

	UObject* CreatePlacementBundleTestOuter(const TCHAR* BaseName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return CreatePackage(*PackageName);
	}

	bool ArePlacementBundleGameplayTagContainersEquivalent(
		const FGameplayTagContainer& Left,
		const FGameplayTagContainer& Right)
	{
		return Left.HasAllExact(Right) && Right.HasAllExact(Left);
	}

	bool ArePlacementBundleFaceRulesEquivalent(
		const FLayoutFaceRule& Left,
		const FLayoutFaceRule& Right)
	{
		return Left.Direction == Right.Direction
			&& Left.ConnectionTag == Right.ConnectionTag
			&& ArePlacementBundleGameplayTagContainersEquivalent(Left.AllowedConnectionTags, Right.AllowedConnectionTags)
			&& Left.OccupancyPolicy == Right.OccupancyPolicy
			&& ArePlacementBundleGameplayTagContainersEquivalent(Left.ConnectedTraversalChannels, Right.ConnectedTraversalChannels)
			&& Left.BoundaryRequirement == Right.BoundaryRequirement
			&& Left.bRequireMatchingYawWithFilledNeighbor == Right.bRequireMatchingYawWithFilledNeighbor;
	}

	bool ArePlacementBundleLocalCellFaceRuleSnapshotsEquivalent(
		const FLayoutLocalCellFaceRuleSnapshot& Left,
		const FLayoutLocalCellFaceRuleSnapshot& Right)
	{
		if (Left.LocalCell != Right.LocalCell || Left.ExposedFaceRules.Num() != Right.ExposedFaceRules.Num())
		{
			return false;
		}

		for (int32 FaceIndex = 0; FaceIndex < Left.ExposedFaceRules.Num(); ++FaceIndex)
		{
			if (!ArePlacementBundleFaceRulesEquivalent(Left.ExposedFaceRules[FaceIndex], Right.ExposedFaceRules[FaceIndex]))
			{
				return false;
			}
		}

		return true;
	}

	bool ArePlacementBundleInternalAccessLinksEquivalent(
		const FLayoutInternalAccessLink& Left,
		const FLayoutInternalAccessLink& Right)
	{
		return Left.FromTraversalChannel == Right.FromTraversalChannel
			&& Left.ToTraversalChannel == Right.ToTraversalChannel
			&& Left.bBidirectional == Right.bBidirectional;
	}

	bool ArePlacementBundleDerivedInternalTraversalLinksEquivalent(
		const FLayoutDerivedInternalTraversalLink& Left,
		const FLayoutDerivedInternalTraversalLink& Right)
	{
		return Left.LinkId == Right.LinkId
			&& Left.FromLocalCell == Right.FromLocalCell
			&& Left.FromTraversalChannel == Right.FromTraversalChannel
			&& Left.ToLocalCell == Right.ToLocalCell
			&& Left.ToTraversalChannel == Right.ToTraversalChannel
			&& Left.bBidirectional == Right.bBidirectional;
	}

	bool ArePlacementBundleDerivedInternalTraversalLinksSemanticallyEquivalent(
		const FLayoutDerivedInternalTraversalLink& Left,
		const FLayoutDerivedInternalTraversalLink& Right)
	{
		return Left.FromLocalCell == Right.FromLocalCell
			&& Left.FromTraversalChannel == Right.FromTraversalChannel
			&& Left.ToLocalCell == Right.ToLocalCell
			&& Left.ToTraversalChannel == Right.ToTraversalChannel
			&& Left.bBidirectional == Right.bBidirectional;
	}

	const FLayoutLocalCellFaceRuleSnapshot* FindPlacementBundleLocalCellFaceRuleSnapshot(
		const TArray<FLayoutLocalCellFaceRuleSnapshot>& Snapshots,
		const FIntVector& LocalCell)
	{
		return Snapshots.FindByPredicate([LocalCell](const FLayoutLocalCellFaceRuleSnapshot& Snapshot)
		{
			return Snapshot.LocalCell == LocalCell;
		});
	}

	const FLayoutDerivedInternalTraversalLink* FindPlacementBundleDerivedInternalTraversalLink(
		const TArray<FLayoutDerivedInternalTraversalLink>& Links,
		const FIntVector& FromLocalCell,
		const FGameplayTag& FromTraversalChannel,
		const FIntVector& ToLocalCell,
		const FGameplayTag& ToTraversalChannel)
	{
		return Links.FindByPredicate(
			[&](const FLayoutDerivedInternalTraversalLink& Link)
			{
				return Link.FromLocalCell == FromLocalCell
					&& Link.FromTraversalChannel == FromTraversalChannel
					&& Link.ToLocalCell == ToLocalCell
					&& Link.ToTraversalChannel == ToTraversalChannel;
			});
	}

	const FLayoutValidationAssertionRecord* FindPlacementBundleAssertion(
		const TArray<FLayoutValidationAssertionRecord>& Assertions,
		const FLayoutId AssertionId)
	{
		return Assertions.FindByPredicate([AssertionId](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == AssertionId;
		});
	}

	void RefreshPlacementBundleValidationFromAssertions(FLayoutModuleSolveSnapshot& Snapshot)
	{
		Snapshot.Validation = FLayoutValidationResult();
		for (const FLayoutValidationAssertionRecord& Assertion : Snapshot.ValidationAssertions)
		{
			if (!Assertion.bPassed)
			{
				Snapshot.Validation.AddError(
					Assertion.FailureReason.IsEmpty()
						? FString::Printf(TEXT("Snapshot assertion '%s' failed."), *Assertion.AssertionId.ToString())
						: Assertion.FailureReason);
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteModuleSnapshotValidationPreservesMissingTemplateOwnerTest,
	"PorismExtension.Layout.Solver.Rewrite.ModuleSnapshotValidationPreservesMissingTemplateOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteBuildsCompositePlacementBundleFromLeafModulesTest,
	"PorismExtension.Layout.Solver.Rewrite.BuildsCompositePlacementBundleFromLeafModules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteCompositeSnapshotCarriesLocalizedTraversalBridgesTest,
	"PorismExtension.Layout.Solver.Rewrite.CompositeSnapshotCarriesLocalizedTraversalBridges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteCompositeSnapshotCanonicalizesEquivalentCellOrderTest,
	"PorismExtension.Layout.Solver.Rewrite.CompositeSnapshotCanonicalizesEquivalentCellOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRecursiveScheduleRewriteBuildsCompositePlacementBundleFromLeafModulesTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacementBundleTestOuter(TEXT("LayoutRewriteBundleCompositeLeafModules"));

	UChunkStructureTemplate* LeafTemplate = CreateTemplate(
		Outer,
		TEXT("RewriteBundleTemplate_CompositeLeaf"),
		FIntVector(8, 8, 8));
	ULayoutModuleAsset* EntryLeaf = CreateModule(
		Outer,
		TEXT("RewriteBundleLeaf_Entry"),
		LeafTemplate,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Entry
		},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceEntry}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});
	ULayoutModuleAsset* SolidLeaf = CreateModule(
		Outer,
		TEXT("RewriteBundleLeaf_Solid"),
		LeafTemplate,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Interior
		},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});

	ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("RewriteBundleCompositeLeaf"));
	{
		FLayoutCompositeModuleCell& EntryCell = Composite->Cells.AddDefaulted_GetRef();
		EntryCell.Module = EntryLeaf;
		EntryCell.LocalCell = FIntVector(0, 0, 0);
		EntryCell.RelativeYawRotationSteps = 0;

		FLayoutCompositeModuleCell& SolidCell = Composite->Cells.AddDefaulted_GetRef();
		SolidCell.Module = SolidLeaf;
		SolidCell.LocalCell = FIntVector(1, 0, 0);
		SolidCell.RelativeYawRotationSteps = 0;
	}

	const FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildCompositeModuleSnapshot(Composite, 1);
	FLayoutRegionSolveRequest Request;
	Request.EffectiveSnapshotId = TEXT("RewriteBundleCompositeLeafRequest");
	Request.ModuleCatalog.Modules.Add(Snapshot);

	const TArray<FPlacementCapabilityBundle> PlacementBundles = BuildModulePlacementBundles(Request);
	TestEqual(TEXT("Composite request produces one placement bundle"), PlacementBundles.Num(), 1);
	if (PlacementBundles.Num() != 1)
	{
		return false;
	}

	const FPlacementCapabilityBundle& Bundle = PlacementBundles[0];
	TestEqual(TEXT("Composite bundle preserves both occupied cells"), Bundle.OccupiedLocalCells.Num(), 2);
	TestEqual(TEXT("Composite bundle preserves one face snapshot per occupied cell"), Bundle.ExposedFaceRules.Num(), 2);
	TestEqual(TEXT("Composite bundle preserves one glued traversal bridge"), Bundle.DerivedInternalTraversalLinks.Num(), 1);
	TestEqual(TEXT("Composite bundle does not reuse leaf internal-link carrier"), Bundle.InternalAccessLinks.Num(), 0);

	const FLayoutLocalCellFaceRuleSnapshot* EntryCellSnapshot =
		FindPlacementBundleLocalCellFaceRuleSnapshot(Bundle.ExposedFaceRules, FIntVector(0, 0, 0));
	const FLayoutLocalCellFaceRuleSnapshot* SolidCellSnapshot =
		FindPlacementBundleLocalCellFaceRuleSnapshot(Bundle.ExposedFaceRules, FIntVector(1, 0, 0));
	TestNotNull(TEXT("Composite bundle keeps the entry leaf cell snapshot"), EntryCellSnapshot);
	TestNotNull(TEXT("Composite bundle keeps the solid leaf cell snapshot"), SolidCellSnapshot);
	if (EntryCellSnapshot != nullptr)
	{
		TestTrue(TEXT("Composite entry leaf keeps its exposed explicit entry face"), EntryCellSnapshot->ExposedFaceRules.ContainsByPredicate(
			[](const FLayoutFaceRule& FaceRule)
			{
				return FaceRule.Direction == ELayoutFaceDirection::NegY
					&& FaceRule.ConnectionTag == LayoutGameplayTags::FaceEntry;
			}));
		TestFalse(TEXT("Composite entry leaf suppresses the glued internal face"), EntryCellSnapshot->ExposedFaceRules.ContainsByPredicate(
			[](const FLayoutFaceRule& FaceRule)
			{
				return FaceRule.Direction == ELayoutFaceDirection::PosX;
			}));
	}
	if (SolidCellSnapshot != nullptr)
	{
		TestTrue(TEXT("Composite solid leaf keeps its distinct exterior face contract"), SolidCellSnapshot->ExposedFaceRules.ContainsByPredicate(
			[](const FLayoutFaceRule& FaceRule)
			{
				return FaceRule.Direction == ELayoutFaceDirection::PosY
					&& FaceRule.ConnectionTag == LayoutGameplayTags::FaceSolid;
			}));
		TestFalse(TEXT("Composite solid leaf suppresses the glued internal face"), SolidCellSnapshot->ExposedFaceRules.ContainsByPredicate(
			[](const FLayoutFaceRule& FaceRule)
			{
				return FaceRule.Direction == ELayoutFaceDirection::NegX;
			}));
	}

	const FLayoutDerivedInternalTraversalLink* GlueTraversalLink =
		FindPlacementBundleDerivedInternalTraversalLink(
			Bundle.DerivedInternalTraversalLinks,
			FIntVector(0, 0, 0),
			LayoutGameplayTags::TraversalPrimary,
			FIntVector(1, 0, 0),
			LayoutGameplayTags::TraversalPrimary);
	TestNotNull(TEXT("Composite bundle carries the glued primary traversal bridge"), GlueTraversalLink);
	if (GlueTraversalLink != nullptr)
	{
		TestTrue(TEXT("Composite glued traversal bridge is bidirectional"), GlueTraversalLink->bBidirectional);
	}

	TestEqual(TEXT("Composite bundle derived traversal-link count matches the snapshot"), Bundle.DerivedInternalTraversalLinks.Num(), Snapshot.DerivedInternalTraversalLinks.Num());
	if (!Bundle.DerivedInternalTraversalLinks.IsEmpty() && !Snapshot.DerivedInternalTraversalLinks.IsEmpty())
	{
		TestTrue(TEXT("Composite bundle derived traversal-link payload matches the snapshot exactly"), ArePlacementBundleDerivedInternalTraversalLinksEquivalent(
			Bundle.DerivedInternalTraversalLinks[0],
			Snapshot.DerivedInternalTraversalLinks[0]));
	}

	return true;
}

bool FLayoutRecursiveScheduleRewriteCompositeSnapshotCarriesLocalizedTraversalBridgesTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacementBundleTestOuter(TEXT("LayoutRewriteBundleCompositeTraversal"));

	UChunkStructureTemplate* LeafTemplate = CreateTemplate(
		Outer,
		TEXT("RewriteBundleTemplate_CompositeTraversal"),
		FIntVector(8, 8, 8));

	FLayoutInternalAccessLink RoomInternalLink;
	RoomInternalLink.FromTraversalChannel = LayoutGameplayTags::TraversalPrimary;
	RoomInternalLink.ToTraversalChannel = LayoutGameplayTags::TraversalSecondary;
	RoomInternalLink.bBidirectional = true;

	ULayoutModuleAsset* RoomLeaf = CreateModule(
		Outer,
		TEXT("RewriteBundleLeaf_Room"),
		LeafTemplate,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Interior
		},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, MakeTags({LayoutGameplayTags::TraversalSecondary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		FGameplayTagContainer(),
		{RoomInternalLink});

	ULayoutModuleAsset* WallLeaf = CreateModule(
		Outer,
		TEXT("RewriteBundleLeaf_Wall"),
		LeafTemplate,
		{
			ELayoutCellIntent::Boundary
		},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});

	ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("RewriteBundleCompositeTraversal"));
	{
		FLayoutCompositeModuleCell& FirstRoomCell = Composite->Cells.AddDefaulted_GetRef();
		FirstRoomCell.Module = RoomLeaf;
		FirstRoomCell.LocalCell = FIntVector(0, 0, 0);
		FirstRoomCell.RelativeYawRotationSteps = 0;

		FLayoutCompositeModuleCell& SecondRoomCell = Composite->Cells.AddDefaulted_GetRef();
		SecondRoomCell.Module = RoomLeaf;
		SecondRoomCell.LocalCell = FIntVector(1, 0, 0);
		SecondRoomCell.RelativeYawRotationSteps = 0;

		FLayoutCompositeModuleCell& WallCell = Composite->Cells.AddDefaulted_GetRef();
		WallCell.Module = WallLeaf;
		WallCell.LocalCell = FIntVector(2, 0, 0);
		WallCell.RelativeYawRotationSteps = 0;
	}

	const FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildCompositeModuleSnapshot(Composite, 1);
	TestTrue(TEXT("Composite traversal snapshot validates cleanly"), Snapshot.Validation.IsValid());
	TestEqual(TEXT("Composite traversal snapshot clears the leaf internal-link carrier"), Snapshot.InternalAccessLinks.Num(), 0);
	TestEqual(TEXT("Composite traversal snapshot carries one localized link per room leaf plus one glued bridge"), Snapshot.DerivedInternalTraversalLinks.Num(), 3);

	const FLayoutDerivedInternalTraversalLink* FirstLocalizedRoomInternalLink =
		FindPlacementBundleDerivedInternalTraversalLink(
			Snapshot.DerivedInternalTraversalLinks,
			FIntVector(0, 0, 0),
			LayoutGameplayTags::TraversalPrimary,
			FIntVector(0, 0, 0),
			LayoutGameplayTags::TraversalSecondary);
	const FLayoutDerivedInternalTraversalLink* SecondLocalizedRoomInternalLink =
		FindPlacementBundleDerivedInternalTraversalLink(
			Snapshot.DerivedInternalTraversalLinks,
			FIntVector(1, 0, 0),
			LayoutGameplayTags::TraversalPrimary,
			FIntVector(1, 0, 0),
			LayoutGameplayTags::TraversalSecondary);
	TestNotNull(TEXT("Composite traversal snapshot localizes the first room leaf's internal link"), FirstLocalizedRoomInternalLink);
	TestNotNull(TEXT("Composite traversal snapshot localizes the second room leaf's internal link"), SecondLocalizedRoomInternalLink);

	const FLayoutDerivedInternalTraversalLink* GluedRoomTraversalLink =
		FindPlacementBundleDerivedInternalTraversalLink(
			Snapshot.DerivedInternalTraversalLinks,
			FIntVector(0, 0, 0),
			LayoutGameplayTags::TraversalPrimary,
			FIntVector(1, 0, 0),
			LayoutGameplayTags::TraversalPrimary);
	TestNotNull(TEXT("Composite traversal snapshot carries the glued room-to-room traversal bridge"), GluedRoomTraversalLink);

	TestFalse(TEXT("Composite traversal snapshot does not invent traversal into the non-traversable wall cell"), Snapshot.DerivedInternalTraversalLinks.ContainsByPredicate(
		[](const FLayoutDerivedInternalTraversalLink& Link)
		{
			return Link.ToLocalCell == FIntVector(2, 0, 0) || Link.FromLocalCell == FIntVector(2, 0, 0);
		}));

	FLayoutRegionSolveRequest Request;
	Request.EffectiveSnapshotId = TEXT("RewriteBundleCompositeTraversalRequest");
	Request.ModuleCatalog.Modules.Add(Snapshot);

	const TArray<FPlacementCapabilityBundle> PlacementBundles = BuildModulePlacementBundles(Request);
	TestEqual(TEXT("Composite traversal request produces one placement bundle"), PlacementBundles.Num(), 1);
	if (PlacementBundles.Num() != 1)
	{
		return false;
	}

	const FPlacementCapabilityBundle& Bundle = PlacementBundles[0];
	TestEqual(TEXT("Composite traversal bundle preserves all derived traversal bridges"), Bundle.DerivedInternalTraversalLinks.Num(), Snapshot.DerivedInternalTraversalLinks.Num());
	TestFalse(TEXT("Composite traversal bundle still does not invent traversal into the wall cell"), Bundle.DerivedInternalTraversalLinks.ContainsByPredicate(
		[](const FLayoutDerivedInternalTraversalLink& Link)
		{
			return Link.ToLocalCell == FIntVector(2, 0, 0) || Link.FromLocalCell == FIntVector(2, 0, 0);
		}));

	return true;
}

bool FLayoutRecursiveScheduleRewriteCompositeSnapshotCanonicalizesEquivalentCellOrderTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacementBundleTestOuter(TEXT("LayoutRewriteBundleCompositeCanonicalOrder"));

	UChunkStructureTemplate* LeafTemplate = CreateTemplate(
		Outer,
		TEXT("RewriteBundleTemplate_CompositeCanonicalOrder"),
		FIntVector(8, 8, 8));

	ULayoutModuleAsset* EntryLeaf = CreateModule(
		Outer,
		TEXT("RewriteBundleLeaf_CanonicalEntry"),
		LeafTemplate,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Entry
		},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceEntry}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});

	ULayoutModuleAsset* InteriorLeaf = CreateModule(
		Outer,
		TEXT("RewriteBundleLeaf_CanonicalInterior"),
		LeafTemplate,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Interior
		},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});

	ULayoutCompositeModuleAsset* ForwardComposite = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("RewriteBundleCompositeCanonicalForward"));
	{
		FLayoutCompositeModuleCell& EntryCell = ForwardComposite->Cells.AddDefaulted_GetRef();
		EntryCell.Module = EntryLeaf;
		EntryCell.LocalCell = FIntVector(0, 0, 0);

		FLayoutCompositeModuleCell& InteriorCell = ForwardComposite->Cells.AddDefaulted_GetRef();
		InteriorCell.Module = InteriorLeaf;
		InteriorCell.LocalCell = FIntVector(1, 0, 0);
	}

	ULayoutCompositeModuleAsset* ReversedComposite = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("RewriteBundleCompositeCanonicalReversed"));
	{
		FLayoutCompositeModuleCell& InteriorCell = ReversedComposite->Cells.AddDefaulted_GetRef();
		InteriorCell.Module = InteriorLeaf;
		InteriorCell.LocalCell = FIntVector(1, 0, 0);

		FLayoutCompositeModuleCell& EntryCell = ReversedComposite->Cells.AddDefaulted_GetRef();
		EntryCell.Module = EntryLeaf;
		EntryCell.LocalCell = FIntVector(0, 0, 0);
	}

	const FLayoutModuleSolveSnapshot ForwardSnapshot = FLayoutProfileSolver::BuildCompositeModuleSnapshot(ForwardComposite, 1);
	const FLayoutModuleSolveSnapshot ReversedSnapshot = FLayoutProfileSolver::BuildCompositeModuleSnapshot(ReversedComposite, 1);

	TestTrue(TEXT("Forward-order composite snapshot validates cleanly"), ForwardSnapshot.Validation.IsValid());
	TestTrue(TEXT("Reversed-order composite snapshot validates cleanly"), ReversedSnapshot.Validation.IsValid());
	TestEqual(TEXT("Equivalent composites preserve the same occupied-cell order"), ForwardSnapshot.OccupiedLocalCells, ReversedSnapshot.OccupiedLocalCells);
	TestEqual(TEXT("Equivalent composites preserve the same role order"), ForwardSnapshot.Roles, ReversedSnapshot.Roles);
	TestEqual(TEXT("Equivalent composites preserve the same supported-intent order"), ForwardSnapshot.SupportedCellIntents, ReversedSnapshot.SupportedCellIntents);
	TestEqual(TEXT("Equivalent composites preserve the same root-supported-intent order"), ForwardSnapshot.RootSupportedCellIntents, ReversedSnapshot.RootSupportedCellIntents);
	TestEqual(TEXT("Equivalent composites preserve the same local face snapshot count"), ForwardSnapshot.GeneratedLocalCellFaceRules.Num(), ReversedSnapshot.GeneratedLocalCellFaceRules.Num());
	TestEqual(TEXT("Equivalent composites preserve the same derived traversal-link count"), ForwardSnapshot.DerivedInternalTraversalLinks.Num(), ReversedSnapshot.DerivedInternalTraversalLinks.Num());

	for (int32 SnapshotIndex = 0; SnapshotIndex < ForwardSnapshot.GeneratedLocalCellFaceRules.Num(); ++SnapshotIndex)
	{
		TestTrue(TEXT("Equivalent composites preserve the same ordered local face snapshots"), ArePlacementBundleLocalCellFaceRuleSnapshotsEquivalent(
			ForwardSnapshot.GeneratedLocalCellFaceRules[SnapshotIndex],
			ReversedSnapshot.GeneratedLocalCellFaceRules[SnapshotIndex]));
	}

	for (int32 LinkIndex = 0; LinkIndex < ForwardSnapshot.DerivedInternalTraversalLinks.Num(); ++LinkIndex)
	{
		TestTrue(TEXT("Equivalent composites preserve the same ordered derived traversal links"), ArePlacementBundleDerivedInternalTraversalLinksSemanticallyEquivalent(
			ForwardSnapshot.DerivedInternalTraversalLinks[LinkIndex],
			ReversedSnapshot.DerivedInternalTraversalLinks[LinkIndex]));
	}

	return true;
}

bool FLayoutRecursiveScheduleRewriteModuleSnapshotValidationPreservesMissingTemplateOwnerTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacementBundleTestOuter(TEXT("LayoutRewriteBundleMissingTemplateValidation"));

	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("RewriteBundleTemplate_MissingTemplate"),
		FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("RewriteBundleModule_MissingTemplate"),
		Template,
		{
			ELayoutCellIntent::Boundary
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));
	Module->Template = nullptr;

	const FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);

	const FLayoutValidationAssertionRecord* AssetValidationAssertion =
		FindPlacementBundleAssertion(Snapshot.ValidationAssertions, TEXT("ModuleSnapshot.AssetValidationPassed"));
	TestNotNull(TEXT("Snapshot emits the asset-validation assertion for missing template"), AssetValidationAssertion);
	if (AssetValidationAssertion != nullptr)
	{
		TestFalse(TEXT("Asset-validation assertion fails when the module is missing its template"), AssetValidationAssertion->bPassed);
		TestTrue(TEXT("Asset-validation assertion keeps the snapshot-side asset owner wording"), AssetValidationAssertion->FailureReason.Contains(TEXT("failed asset validation before snapshot solve setup")));
	}

	TestFalse(TEXT("Snapshot validation stays invalid when the source module fails asset validation"), Snapshot.Validation.IsValid());
	TestTrue(TEXT("Snapshot validation preserves the missing-template message from the asset owner"), Snapshot.Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("missing its chunk structure template"))
			&& Message.Message.Contains(TEXT("RewriteBundleModule_MissingTemplate"));
	}));

	return true;
}
