// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildContactWalkableAuditDefersSettledChildFaceTest,
	"PorismExtension.Layout.Solver.ChildContactWalkableAudit.DefersSettledChildFace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildContactWalkableAuditDefersSettledChildFaceTest::RunTest(const FString& Parameters)
{
	const FString PackageName = FString::Printf(
		TEXT("/Temp/LayoutChildContactWalkableAudit_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UObject* Outer = CreatePackage(*PackageName);
	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("ChildContactWalkableAuditTemplate"),
		FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("ChildContactWalkableAuditModule"),
		Template,
		{ELayoutCellIntent::Boundary},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});
	Module->MinWalkableFaces = 1;

	FLayoutModuleSolveSnapshot Snapshot =
		FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	TestTrue(TEXT("Child-contact module snapshot is valid"), Snapshot.Validation.IsValid());
	if (!Snapshot.Validation.IsValid())
	{
		return false;
	}
	Snapshot.SourceCompositeModule = NewObject<ULayoutCompositeModuleAsset>(
		Outer,
		TEXT("ChildContactWalkableAuditCompositeCarrier"));
	Snapshot.SourceModule = nullptr;

	LayoutProfileSolverInternal::FSolveContext Context;
	Context.Result.FootprintSize = FIntPoint(1, 1);
	Context.ModuleSnapshots.Add(Snapshot);
	Context.PlannedCellIntents.Add(FIntVector::ZeroValue, ELayoutCellIntent::Boundary);
	Context.ChildReservationCells.Add(FIntVector(1, 0, 0));

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant =
		Context.Variants.AddDefaulted_GetRef();
	Variant.ModuleSnapshotIndex = 0;
	Variant.ModuleDebugName = Snapshot.DebugName;
	Variant.YawRotationSteps = 0;
	Variant.WorldFaceRules = Snapshot.EffectiveFaceRules;
	Variant.WorldSpanOffers = Snapshot.DerivedSpanOffers;
	Variant.ClosureProviderIntents = Snapshot.ClosureProviderIntents;
	Variant.TraversalChannels = Snapshot.TraversalChannels;
	Variant.InternalAccessLinks = Snapshot.InternalAccessLinks;
	Variant.VerticalAccessContracts = Snapshot.DerivedVerticalAccessContracts;
	Variant.Roles = Snapshot.Roles;
	Variant.SupportedCellIntents = Snapshot.SupportedCellIntents;
	Variant.MinTraversableNeighborFaces = Snapshot.MinTraversableNeighborFaces;

	LayoutProfileSolverInternal::FSolveCandidate Candidate;
	Candidate.YawRotationSteps = 0;
	Candidate.VariantIndex = 0;
	Candidate.bEmpty = false;

	LayoutProfileSolverInternal::RefreshCompiledFaceInterfacesForTests(Context);
	FString FailureReason;
	const bool bCompatible = LayoutProfileSolverInternal::IsCandidateCompatibleForTests(
		Context,
		FIntVector::ZeroValue,
		Candidate,
		FailureReason);

	TestTrue(
		TEXT("Settled child contact remains a possible walkable neighbor until child-face audit"),
		bCompatible);
	if (!bCompatible)
	{
		AddError(FailureReason);
	}
	return bCompatible;
}
