// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

#include "Layout/Support/LayoutTestUtilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutOfferDerivationBuildsEndpointAndSpanOffersTest,
	"PorismExtension.Layout.Solver.Offers.BuildsEndpointAndSpanOffers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutOfferDerivationBuildsEndpointAndSpanOffersTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_DerivedOffers"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_DerivedOffers"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Entry},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});

	const FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 2);
	TestTrue(TEXT("Snapshot validation succeeds"), Snapshot.Validation.IsValid());
	TestTrue(TEXT("Derived endpoint offers exist"), Snapshot.DerivedEndpointOffers.Num() > 0);
	TestTrue(TEXT("Derived span offers exist"), Snapshot.DerivedSpanOffers.Num() > 0);
	TestTrue(TEXT("Derived endpoint offer includes the entry exterior face"), Snapshot.DerivedEndpointOffers.ContainsByPredicate([](const FLayoutDerivedEndpointOffer& Offer)
	{
		return Offer.FaceDirection == ELayoutFaceDirection::PosX
			&& Offer.ConnectionTag == LayoutGameplayTags::FaceEntry
			&& Offer.TraversalChannels.HasTagExact(LayoutGameplayTags::TraversalPrimary);
	}));
	TestTrue(TEXT("Derived span offer includes the entry exterior face"), Snapshot.DerivedSpanOffers.ContainsByPredicate([](const FLayoutDerivedSpanOffer& Offer)
	{
		return Offer.FaceDirection == ELayoutFaceDirection::PosX
			&& Offer.ConnectionTag == LayoutGameplayTags::FaceEntry
			&& Offer.bSealsBoundary;
	}));
	TestTrue(TEXT("Proof records include derived offer proof"), Snapshot.ProofRecords.ContainsByPredicate([](const FLayoutProofRecord& ProofRecord)
	{
		return ProofRecord.ProofKind == ELayoutProofKind::DerivedOffer;
	}));
	TestTrue(TEXT("Proof records include derived span proof"), Snapshot.ProofRecords.ContainsByPredicate([](const FLayoutProofRecord& ProofRecord)
	{
		return ProofRecord.ProofKind == ELayoutProofKind::DerivedSpan;
	}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutOfferDerivationBuildsCompositeEndpointAndSpanOffersTest,
	"PorismExtension.Layout.Solver.Offers.BuildsCompositeEndpointAndSpanOffers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutOfferDerivationBuildsCompositeEndpointAndSpanOffersTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_CompositeOffers"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* EntryLeaf = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeOfferEntryLeaf"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Entry},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceEntry}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});
	ULayoutModuleAsset* BoundaryLeaf = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeOfferBoundaryLeaf"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});

	ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(GetTransientPackage(), TEXT("LayoutCompositeModule_DerivedOffers"));
	{
		FLayoutCompositeModuleCell& EntryCell = Composite->Cells.AddDefaulted_GetRef();
		EntryCell.Module = EntryLeaf;
		EntryCell.LocalCell = FIntVector(0, 0, 0);
		EntryCell.RelativeYawRotationSteps = 0;

		FLayoutCompositeModuleCell& BoundaryCell = Composite->Cells.AddDefaulted_GetRef();
		BoundaryCell.Module = BoundaryLeaf;
		BoundaryCell.LocalCell = FIntVector(1, 0, 0);
		BoundaryCell.RelativeYawRotationSteps = 0;
	}

	const FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildCompositeModuleSnapshot(Composite, 1);
	TestTrue(TEXT("Composite snapshot validation succeeds"), Snapshot.Validation.IsValid());
	TestTrue(TEXT("Composite derived endpoint offers exist"), Snapshot.DerivedEndpointOffers.Num() > 0);
	TestTrue(TEXT("Composite derived span offers exist"), Snapshot.DerivedSpanOffers.Num() > 0);
	TestTrue(TEXT("Composite endpoint offers preserve the explicit entry exterior face"), Snapshot.DerivedEndpointOffers.ContainsByPredicate([](const FLayoutDerivedEndpointOffer& Offer)
	{
		return Offer.LocalCell == FIntVector(0, 0, 0)
			&& Offer.FaceDirection == ELayoutFaceDirection::NegY
			&& Offer.ConnectionTag == LayoutGameplayTags::FaceEntry
			&& Offer.TraversalChannels.HasTagExact(LayoutGameplayTags::TraversalPrimary);
	}));
	TestTrue(TEXT("Composite endpoint offers preserve the far glued leaf exterior face"), Snapshot.DerivedEndpointOffers.ContainsByPredicate([](const FLayoutDerivedEndpointOffer& Offer)
	{
		return Offer.LocalCell == FIntVector(1, 0, 0)
			&& Offer.FaceDirection == ELayoutFaceDirection::PosX
			&& Offer.ConnectionTag == LayoutGameplayTags::FaceOpen
			&& Offer.TraversalChannels.HasTagExact(LayoutGameplayTags::TraversalPrimary);
	}));
	TestFalse(TEXT("Composite endpoint offers do not leak the glued internal face on the entry leaf"), Snapshot.DerivedEndpointOffers.ContainsByPredicate([](const FLayoutDerivedEndpointOffer& Offer)
	{
		return Offer.LocalCell == FIntVector(0, 0, 0) && Offer.FaceDirection == ELayoutFaceDirection::PosX;
	}));
	TestFalse(TEXT("Composite endpoint offers do not leak the glued internal face on the boundary leaf"), Snapshot.DerivedEndpointOffers.ContainsByPredicate([](const FLayoutDerivedEndpointOffer& Offer)
	{
		return Offer.LocalCell == FIntVector(1, 0, 0) && Offer.FaceDirection == ELayoutFaceDirection::NegX;
	}));
	TestTrue(TEXT("Composite span offers preserve the explicit entry exterior face"), Snapshot.DerivedSpanOffers.ContainsByPredicate([](const FLayoutDerivedSpanOffer& Offer)
	{
		return Offer.LocalCell == FIntVector(0, 0, 0)
			&& Offer.FaceDirection == ELayoutFaceDirection::NegY
			&& Offer.ConnectionTag == LayoutGameplayTags::FaceEntry;
	}));
	TestTrue(TEXT("Composite span offers preserve the far glued leaf exterior face"), Snapshot.DerivedSpanOffers.ContainsByPredicate([](const FLayoutDerivedSpanOffer& Offer)
	{
		return Offer.LocalCell == FIntVector(1, 0, 0)
			&& Offer.FaceDirection == ELayoutFaceDirection::PosX
			&& Offer.ConnectionTag == LayoutGameplayTags::FaceOpen;
	}));
	TestFalse(TEXT("Composite span offers do not leak the glued internal face on the entry leaf"), Snapshot.DerivedSpanOffers.ContainsByPredicate([](const FLayoutDerivedSpanOffer& Offer)
	{
		return Offer.LocalCell == FIntVector(0, 0, 0) && Offer.FaceDirection == ELayoutFaceDirection::PosX;
	}));
	TestFalse(TEXT("Composite span offers do not leak the glued internal face on the boundary leaf"), Snapshot.DerivedSpanOffers.ContainsByPredicate([](const FLayoutDerivedSpanOffer& Offer)
	{
		return Offer.LocalCell == FIntVector(1, 0, 0) && Offer.FaceDirection == ELayoutFaceDirection::NegX;
	}));
	TestTrue(TEXT("Composite proof records include derived offer proof"), Snapshot.ProofRecords.ContainsByPredicate([](const FLayoutProofRecord& ProofRecord)
	{
		return ProofRecord.ProofKind == ELayoutProofKind::DerivedOffer;
	}));
	TestTrue(TEXT("Composite proof records include derived span proof"), Snapshot.ProofRecords.ContainsByPredicate([](const FLayoutProofRecord& ProofRecord)
	{
		return ProofRecord.ProofKind == ELayoutProofKind::DerivedSpan;
	}));

	return true;
}
