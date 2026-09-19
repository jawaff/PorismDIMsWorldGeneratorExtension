// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionSchedulePlacementBridgeTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamOwnershipUsesCertifiedRunAndDeterministicTiesTest,
	"PorismExtension.Layout.Solver.SeamOwnership.UsesCertifiedRunAndDeterministicTies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamOwnershipAllowsChildOwnedParentChildSeamTest,
	"PorismExtension.Layout.Solver.SeamOwnership.AllowsChildOwnedParentChildSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamOwnershipRequiresOnePlacedJunctionProviderTest,
	"PorismExtension.Layout.Solver.SeamOwnership.RequiresOnePlacedJunctionProvider",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamOwnershipDirectChildEntryDoesNotCreateStructuralSeamTest,
	"PorismExtension.Layout.Solver.SeamOwnership.DirectChildEntryDoesNotCreateStructuralSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamOwnershipSharedShellSkipsExternalSupportTest,
	"PorismExtension.Layout.Solver.SeamOwnership.SharedShellSkipsExternalSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamOwnershipRequiresExactCandidatePairTest,
	"PorismExtension.Layout.Solver.SeamOwnership.RequiresExactCandidatePair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamOwnershipFiltersCandidateDomainsByJunctionUsageTest,
	"PorismExtension.Layout.Solver.SeamOwnership.FiltersCandidateDomainsByJunctionUsage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSeamOwnershipUsesCertifiedRunAndDeterministicTiesTest::RunTest(const FString& Parameters)
{
	using namespace LayoutRegionScheduleSolverPrivate;

	TArray<FSharedParentChildFace> CertifiedFaces;
	for (int32 Y = 0; Y < 3; ++Y)
	{
		FSharedParentChildFace& Face = CertifiedFaces.AddDefaulted_GetRef();
		Face.ParentCell = FIntVector(0, Y, 0);
		Face.ChildLocalCell = Face.ParentCell;
		Face.FaceDirection = ELayoutFaceDirection::PosX;
		Face.InterfaceFamily = Y == 1
			? LayoutGameplayTags::InterfacePartitionDoor
			: LayoutGameplayTags::InterfacePartitionSolid;
		Face.ReciprocalDomainWitnessId = FLayoutId(*FString::Printf(TEXT("Exact.%d"), Y));
	}
	RefreshCertifiedSharedParentChildFaceRunLengths(CertifiedFaces);
	TestEqual(TEXT("Child-owned Door splits preceding Solid run"),
		CertifiedFaces[0].ParentOwnerSupportRunLength, 1);
	TestEqual(TEXT("Child-owned Door splits following Solid run"),
		CertifiedFaces[2].ParentOwnerSupportRunLength, 1);
	CertifiedFaces[1].InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
	RefreshCertifiedSharedParentChildFaceRunLengths(CertifiedFaces);
	TestEqual(TEXT("Contiguous exact Solid faces form one certified run"),
		CertifiedFaces[1].ParentOwnerSupportRunLength, 3);

	FString Owner;
	FString Passive;
	TestTrue(TEXT("Longer certified child run resolves owner"), LayoutProfileSolverInternal::TryChooseCommittedSeamRunOwnerForTests(
		TEXT("Parent"), TEXT("Parent"), true, 2, false, TEXT("Child"), true, 4, false, Owner, Passive));
	TestEqual(TEXT("Longer child run owns seam"), Owner, FString(TEXT("Child")));

	TestTrue(TEXT("Equal parent-child run resolves owner"), LayoutProfileSolverInternal::TryChooseCommittedSeamRunOwnerForTests(
		TEXT("Parent"), TEXT("Parent"), true, 4, false, TEXT("Child"), true, 4, false, Owner, Passive));
	TestEqual(TEXT("Equal parent-child run prefers parent"), Owner, FString(TEXT("Parent")));

	TestTrue(TEXT("Equal sibling run resolves owner"), LayoutProfileSolverInternal::TryChooseCommittedSeamRunOwnerForTests(
		TEXT("Parent"), TEXT("Sibling.B"), true, 3, false, TEXT("Sibling.A"), true, 3, false, Owner, Passive));
	TestEqual(TEXT("Equal sibling run uses stable region identity"), Owner, FString(TEXT("Sibling.A")));

	TestTrue(TEXT("Sole door anchor resolves owner"), LayoutProfileSolverInternal::TryChooseCommittedSeamRunOwnerForTests(
		TEXT("Parent"), TEXT("Parent"), true, 8, false, TEXT("Child"), true, 1, true, Owner, Passive));
	TestEqual(TEXT("Sole door anchor outranks longer support"), Owner, FString(TEXT("Child")));

	TArray<FLayoutPartitionSeamRecord> RunSegments;
	for (int32 Y = 0; Y < 3; ++Y)
	{
		FLayoutPartitionSeamRecord& Segment = RunSegments.AddDefaulted_GetRef();
		Segment.SeamId = FLayoutId(*FString::Printf(TEXT("Raw.%d"), Y));
		Segment.ParentRegionDebugPath = TEXT("Parent");
		Segment.OwnerRegionDebugPath = TEXT("Child");
		Segment.PassiveRegionDebugPath = TEXT("Sibling");
		Segment.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
		Segment.OwnerFaceDirection = ELayoutFaceDirection::PosX;
		Segment.PassiveFaceDirection = ELayoutFaceDirection::NegX;
		Segment.OwnerStartCell = FIntVector(0, Y, 0);
		Segment.OwnerEndCell = Segment.OwnerStartCell;
		Segment.PassiveStartCell = FIntVector(1, Y, 0);
		Segment.PassiveEndCell = Segment.PassiveStartCell;
		Segment.SegmentCount = 1;
	}
	LayoutProfileSolverInternal::MergeCommittedPartitionSeamSegmentsIntoRunsForTests(RunSegments);
	TestEqual(TEXT("Continuous committed segments freeze as one run"), RunSegments.Num(), 1);
	if (RunSegments.Num() == 1)
	{
		TestEqual(TEXT("Merged run keeps every segment"), RunSegments[0].SegmentCount, 3);
		TestEqual(TEXT("Merged run starts deterministically"), RunSegments[0].OwnerStartCell, FIntVector(0, 0, 0));
		TestEqual(TEXT("Merged run ends deterministically"), RunSegments[0].OwnerEndCell, FIntVector(0, 2, 0));
	}
	return true;
}

bool FLayoutSeamOwnershipAllowsChildOwnedParentChildSeamTest::RunTest(const FString& Parameters)
{
	FLayoutChildCapabilityEnvelope ParentEnvelope;
	FLayoutChildCapabilitySeam& ParentCapability = ParentEnvelope.SeamCapabilities.AddDefaulted_GetRef();
	ParentCapability.CapabilityId = TEXT("Parent.Accept");
	ParentCapability.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
	ParentCapability.FaceDirection = ELayoutFaceDirection::NegX;
	ParentCapability.bCanOwnSeam = false;
	ParentCapability.bCanAcceptSeam = true;

	FLayoutChildCapabilityEnvelope ChildEnvelope;
	FLayoutChildCapabilitySeam& ChildCapability = ChildEnvelope.SeamCapabilities.AddDefaulted_GetRef();
	ChildCapability.CapabilityId = TEXT("Child.Own");
	ChildCapability.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
	ChildCapability.FaceDirection = ELayoutFaceDirection::NegX;
	ChildCapability.bCanOwnSeam = true;
	ChildCapability.bCanAcceptSeam = false;

	TArray<FLayoutPartitionSeamRecord> Seams;
	FString FailureReason;
	TestTrue(FString::Printf(TEXT("Child-owned parent-child seam builds: %s"), *FailureReason),
		LayoutProfileSolverInternal::TryBuildCommittedParentChildSeamForTests(
			ParentEnvelope, ChildEnvelope, Seams, FailureReason));
	TestEqual(TEXT("One committed seam emitted"), Seams.Num(), 1);
	if (Seams.Num() == 1)
	{
		TestEqual(TEXT("Child can own parent-child seam"), Seams[0].OwnerRegionDebugPath, FString(TEXT("CommittedParentChild")));
		TestEqual(TEXT("Parent becomes passive"), Seams[0].PassiveRegionDebugPath, FString(TEXT("CommittedParent")));
	}
	return true;
}

bool FLayoutSeamOwnershipDirectChildEntryDoesNotCreateStructuralSeamTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutPartitionSeamRecord> Seams;
	FString FailureReason;
	TestTrue(FString::Printf(TEXT("Direct child Entry handoff builds partition seams: %s"), *FailureReason),
		LayoutProfileSolverInternal::TryBuildPartitionSeamsForDirectChildEntryForTests(Seams, FailureReason));
	TestEqual(TEXT("Direct child Entry uses endpoint and traversal contracts without a structural seam"), Seams.Num(), 0);
	return true;
}

bool FLayoutSeamOwnershipSharedShellSkipsExternalSupportTest::RunTest(const FString& Parameters)
{
	FLayoutPlannedCell ChildBoundary;
	ChildBoundary.Cell = FIntVector(1, 1, 0);
	ChildBoundary.Intent = ELayoutCellIntent::Boundary;
	TArray<FLayoutSolveBoundaryPoint> BoundaryPoints;
	LayoutProfileSolverInternal::AppendSyntheticParentSupportBoundaryPointsForTests(
		{ChildBoundary},
		{FIntVector(2, 1, 0)},
		false,
		BoundaryPoints);

	if (!TestEqual(TEXT("Exclusive child cell builds one parent-facing support point"), BoundaryPoints.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Boundary support faces parent neighbor"), BoundaryPoints[0].FaceDirection, ELayoutFaceDirection::PosX);

	BoundaryPoints.Reset();
	FLayoutSolveBoundaryPoint& EndpointBoundary = BoundaryPoints.AddDefaulted_GetRef();
	EndpointBoundary.LocalCell = FIntVector(2, 1, 0);
	EndpointBoundary.FaceDirection = ELayoutFaceDirection::NegX;
	EndpointBoundary.ConnectionTag = LayoutGameplayTags::FaceEntry;
	EndpointBoundary.ConnectedTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
	LayoutProfileSolverInternal::AppendSyntheticParentSupportBoundaryPointsForTests(
		{ChildBoundary},
		{FIntVector(2, 1, 0)},
		false,
		BoundaryPoints);
	TestEqual(TEXT("Specific endpoint boundary suppresses opposite synthetic support on the same edge"),
		BoundaryPoints.Num(),
		1);
	TestTrue(TEXT("Specific endpoint keeps traversal authority"),
		BoundaryPoints[0].ConnectedTraversalChannels.HasTagExact(LayoutGameplayTags::TraversalPrimary));

	BoundaryPoints.Reset();
	LayoutProfileSolverInternal::AppendSyntheticParentSupportBoundaryPointsForTests(
		{ChildBoundary},
		{FIntVector(2, 1, 0)},
		true,
		BoundaryPoints);
	TestTrue(TEXT("Retained shared-shell cell does not masquerade as external child support"),
		BoundaryPoints.IsEmpty());
	return true;
}

bool FLayoutSeamOwnershipRequiresExactCandidatePairTest::RunTest(const FString& Parameters)
{
	auto MakeSolidModule = [](const FLayoutId SnapshotId)
	{
		FLayoutModuleSolveSnapshot Module;
		Module.SnapshotId = SnapshotId;
		Module.BoundsCells = FIntVector(1, 1, 1);
		Module.AllowedYawRotationSteps = {0};
		FLayoutFaceRule FaceRule;
		FaceRule.Direction = ELayoutFaceDirection::PosX;
		FaceRule.ConnectionTag = LayoutGameplayTags::FaceSolid;
		FaceRule.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceSolid);
		FaceRule.OccupancyPolicy = ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor;
		Module.EffectiveFaceRules.SetRule(FaceRule);
		return Module;
	};
	auto AddSolidSeamEvidence = [](FLayoutModuleSolveSnapshot& Module,
		const bool bCanOwn, const bool bCanAccept)
	{
		FLayoutSeamProviderIntent& Intent = Module.SeamProviderIntents.AddDefaulted_GetRef();
		Intent.SeamIntentId = FName(*FString::Printf(TEXT("%s.Intent"), *Module.SnapshotId.ToString()));
		Intent.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
		Intent.bCanOwnSeam = bCanOwn;
		Intent.bCanAcceptSeam = bCanAccept;
		FLayoutDerivedSpanOffer& Span = Module.DerivedSpanOffers.AddDefaulted_GetRef();
		Span.SpanOfferId = FLayoutId(*FString::Printf(TEXT("%s.Span"), *Module.SnapshotId.ToString()));
		Span.LocalCell = FIntVector::ZeroValue;
		Span.FaceDirection = ELayoutFaceDirection::PosX;
		Span.ConnectionTag = LayoutGameplayTags::FaceSolid;
		Span.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceSolid);
	};

	FLayoutChildCapabilityEnvelope ParentEnvelope;
	FLayoutChildCapabilitySeam& ParentCapability = ParentEnvelope.SeamCapabilities.AddDefaulted_GetRef();
	ParentCapability.CapabilityId = TEXT("Parent.GlobalOwner");
	ParentCapability.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
	ParentCapability.FaceDirection = ELayoutFaceDirection::PosX;
	ParentCapability.bCanOwnSeam = true;
	ParentCapability.bCanAcceptSeam = false;
	FLayoutChildCapabilityEnvelope ChildEnvelope;
	FLayoutChildCapabilitySeam& ChildCapability = ChildEnvelope.SeamCapabilities.AddDefaulted_GetRef();
	ChildCapability.CapabilityId = TEXT("Child.GlobalAccept");
	ChildCapability.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
	ChildCapability.FaceDirection = ELayoutFaceDirection::PosX;
	ChildCapability.bCanOwnSeam = false;
	ChildCapability.bCanAcceptSeam = true;

	TestTrue(TEXT("Region-wide envelopes expose a provisional seam"),
		LayoutProfileSolverInternal::TryResolveSharedSeamCapabilitiesForTests(
			ParentEnvelope,
			ELayoutFaceDirection::PosX,
			ChildEnvelope,
			ELayoutFaceDirection::PosX,
			LayoutGameplayTags::InterfacePartitionSolid));

	FLayoutModuleSolveSnapshot ParentModule = MakeSolidModule(TEXT("Parent.NoSeam"));
	FLayoutModuleSolveSnapshot ChildModule = MakeSolidModule(TEXT("Child.NoSeam"));
	bool bParentCanOwn = false;
	bool bChildCanOwn = false;
	FLayoutId WitnessId;
	TestFalse(TEXT("Region-wide capability cannot certify modules with no seam evidence"),
		LayoutProfileSolverInternal::TryCertifyExactSharedSeamCandidatePairForTests(
			ParentModule,
			0,
			ChildModule,
			0,
			ELayoutFaceDirection::PosX,
			LayoutGameplayTags::InterfacePartitionSolid,
			false,
			bParentCanOwn,
			bChildCanOwn,
			WitnessId));

	AddSolidSeamEvidence(ParentModule, true, false);
	AddSolidSeamEvidence(ChildModule, false, true);
	TestTrue(TEXT("Exact reciprocal module pair certifies"),
		LayoutProfileSolverInternal::TryCertifyExactSharedSeamCandidatePairForTests(
			ParentModule,
			0,
			ChildModule,
			0,
			ELayoutFaceDirection::PosX,
			LayoutGameplayTags::InterfacePartitionSolid,
			false,
			bParentCanOwn,
			bChildCanOwn,
			WitnessId));
	TestTrue(TEXT("Exact pair preserves parent ownership"), bParentCanOwn);
	TestFalse(TEXT("Exact pair does not invent child ownership"), bChildCanOwn);
	TestFalse(TEXT("Exact pair records stable reciprocal witness"), WitnessId.IsNone());

	const auto AuthorSameSurfaceExteriorFace = [](FLayoutModuleSolveSnapshot& Module)
	{
		FLayoutFaceRule* FaceRule = Module.EffectiveFaceRules.FindRule(ELayoutFaceDirection::PosX);
		check(FaceRule != nullptr);
		FaceRule->AllowedConnectionTags.Reset();
		FaceRule->AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
		Module.DerivedSpanOffers[0].AllowedConnectionTags.Reset();
		Module.DerivedSpanOffers[0].AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
	};
	AuthorSameSurfaceExteriorFace(ParentModule);
	AuthorSameSurfaceExteriorFace(ChildModule);
	TestTrue(TEXT("Same-surface seam does not require duplicate faces to accept each other as neighbors"),
		LayoutProfileSolverInternal::TryCertifyExactSharedSeamCandidatePairForTests(
			ParentModule,
			0,
			ChildModule,
			0,
			ELayoutFaceDirection::PosX,
			LayoutGameplayTags::InterfacePartitionSolid,
			false,
			bParentCanOwn,
			bChildCanOwn,
			WitnessId));

	auto ConvertToDoorEvidence = [](FLayoutModuleSolveSnapshot& Module,
		const bool bCanOwn, const bool bCanAccept)
	{
		Module.SeamProviderIntents[0].InterfaceFamily =
			LayoutGameplayTags::InterfacePartitionDoor;
		Module.SeamProviderIntents[0].bCanOwnSeam = bCanOwn;
		Module.SeamProviderIntents[0].bCanAcceptSeam = bCanAccept;
		Module.DerivedSpanOffers[0].ConnectionTag = LayoutGameplayTags::FaceEntry;
		Module.DerivedSpanOffers[0].AllowedConnectionTags.Reset();
		Module.DerivedSpanOffers[0].AllowedConnectionTags.AddTag(
			LayoutGameplayTags::FaceEntry);
		FLayoutFaceRule DoorFace;
		DoorFace.Direction = ELayoutFaceDirection::PosX;
		DoorFace.ConnectionTag = LayoutGameplayTags::FaceEntry;
		DoorFace.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceEntry);
		DoorFace.OccupancyPolicy = ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor;
		Module.EffectiveFaceRules.SetRule(DoorFace);
	};
	ConvertToDoorEvidence(ParentModule, false, true);
	ConvertToDoorEvidence(ChildModule, true, false);
	TestTrue(TEXT("Exact child Entry pair certifies child-owned Door"),
		LayoutProfileSolverInternal::TryCertifyExactSharedSeamCandidatePairForTests(
			ParentModule,
			0,
			ChildModule,
			0,
			ELayoutFaceDirection::PosX,
			LayoutGameplayTags::InterfacePartitionDoor,
			true,
			bParentCanOwn,
			bChildCanOwn,
			WitnessId));
	TestFalse(TEXT("Child Entry prevents parent Door ownership"), bParentCanOwn);
	TestTrue(TEXT("Child Entry keeps exact Door ownership"), bChildCanOwn);
	return true;
}

bool FLayoutSeamOwnershipRequiresOnePlacedJunctionProviderTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest OwnerRequest;
	OwnerRequest.RegionDebugPath = TEXT("Owner");
	FLayoutRegionContentEntrySolveSnapshot& Entry = OwnerRequest.ContentSetSnapshot.Entries.AddDefaulted_GetRef();
	Entry.EntryId = TEXT("JunctionEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSnapshotIndex = 0;
	FLayoutModuleSolveSnapshot& Module = OwnerRequest.ModuleCatalog.Modules.AddDefaulted_GetRef();
	Module.SnapshotId = TEXT("JunctionModule");
	Module.BoundsCells = FIntVector(1, 1, 1);
	Module.AllowedYawRotationSteps = {0};
	FLayoutSeamProviderIntent& Intent = Module.SeamProviderIntents.AddDefaulted_GetRef();
	Intent.SeamIntentId = TEXT("JunctionOwner");
	Intent.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
	Intent.bCanOwnSeam = true;
	Intent.bCanAcceptSeam = false;
	FLayoutDerivedSpanOffer& ContinuingOffer = Module.DerivedSpanOffers.AddDefaulted_GetRef();
	ContinuingOffer.SpanOfferId = TEXT("Continuing");
	ContinuingOffer.LocalCell = FIntVector::ZeroValue;
	ContinuingOffer.FaceDirection = ELayoutFaceDirection::PosX;
	ContinuingOffer.bSealsBoundary = true;
	FLayoutDerivedSpanOffer& BranchOffer = Module.DerivedSpanOffers.AddDefaulted_GetRef();
	BranchOffer.SpanOfferId = TEXT("Branch");
	BranchOffer.LocalCell = FIntVector::ZeroValue;
	BranchOffer.FaceDirection = ELayoutFaceDirection::PosY;
	BranchOffer.bSealsBoundary = true;

	FLayoutRegionSolveScheduleResult ScheduleResult;
	FLayoutRegionSolveResult& OwnerResult = ScheduleResult.RegionResults.AddDefaulted_GetRef();
	OwnerResult.RegionDebugPath = TEXT("Owner");
	FLayoutPlacedModule& Placement = OwnerResult.SolveResult.Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector::ZeroValue;
	Placement.SourceContentEntryId = Entry.EntryId;
	Placement.ModuleSnapshotId = Module.SnapshotId;
	Placement.ModuleSnapshotIndex = 0;
	Placement.BundleBoundsCells = FIntVector(1, 1, 1);
	Placement.OccupiedLocalCells = {FIntVector::ZeroValue};

	TArray<FLayoutPartitionSeamRecord> Seams;
	FLayoutPartitionSeamRecord& ContinuingSeam = Seams.AddDefaulted_GetRef();
	ContinuingSeam.SeamId = TEXT("Continue");
	ContinuingSeam.OwnerRegionDebugPath = TEXT("Owner");
	ContinuingSeam.PassiveRegionDebugPath = TEXT("PassiveA");
	ContinuingSeam.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
	ContinuingSeam.OwnerFaceDirection = ELayoutFaceDirection::PosX;
	ContinuingSeam.OwnerStartCell = FIntVector::ZeroValue;
	ContinuingSeam.OwnerEndCell = FIntVector::ZeroValue;
	ContinuingSeam.SegmentCount = 1;
	FLayoutPartitionSeamRecord& BranchSeam = Seams.AddDefaulted_GetRef();
	BranchSeam = ContinuingSeam;
	BranchSeam.SeamId = TEXT("Branch");
	BranchSeam.PassiveRegionDebugPath = TEXT("PassiveB");
	BranchSeam.OwnerFaceDirection = ELayoutFaceDirection::PosY;

	FLayoutOwnedSeamJunctionRequirement Requirement;
	Requirement.JunctionRequirementId = TEXT("Junction");
	Requirement.OwnerRegionDebugPath = TEXT("Owner");
	Requirement.JunctionCell = FIntVector::ZeroValue;
	Requirement.ContinuingSeamId = ContinuingSeam.SeamId;
	Requirement.BranchSeamId = BranchSeam.SeamId;
	Requirement.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
	Requirement.ContinuingOwnerFaceDirection = ELayoutFaceDirection::PosX;
	Requirement.BranchOwnerFaceDirection = ELayoutFaceDirection::PosY;

	TMap<FString, const FLayoutRegionSolveRequest*> RequestsByPath;
	RequestsByPath.Add(TEXT("Owner"), &OwnerRequest);
	TMap<FString, int32> ResultIndexByPath;
	ResultIndexByPath.Add(TEXT("Owner"), 0);
	FString FailureReason;
	TestTrue(FString::Printf(TEXT("One placed module realizes both junction faces: %s"), *FailureReason),
		LayoutProfileSolverInternal::ValidateOwnedSeamJunctionRequirements(
			{Requirement}, Seams, RequestsByPath, ResultIndexByPath, ScheduleResult, FailureReason));

	const FLayoutDerivedSpanOffer SavedBranchOffer =
		OwnerRequest.ModuleCatalog.Modules[0].DerivedSpanOffers[1];
	OwnerRequest.ModuleCatalog.Modules[0].DerivedSpanOffers.RemoveAt(1);
	TestFalse(TEXT("Split junction support rejects"),
		LayoutProfileSolverInternal::ValidateOwnedSeamJunctionRequirements(
			{Requirement}, Seams, RequestsByPath, ResultIndexByPath, ScheduleResult, FailureReason));
	TestTrue(TEXT("Failure names one owner placement"), FailureReason.Contains(TEXT("one owner placement")));
	Intent.JunctionUsage = ELayoutSeamJunctionUsage::NonJunctionOnly;
	OwnerRequest.ModuleCatalog.Modules[0].DerivedSpanOffers.Add(SavedBranchOffer);
	TestFalse(TEXT("Non-junction-only provider rejects junction placement"),
		LayoutProfileSolverInternal::ValidateOwnedSeamJunctionRequirements(
			{Requirement}, Seams, RequestsByPath, ResultIndexByPath, ScheduleResult, FailureReason));
	Intent.JunctionUsage = ELayoutSeamJunctionUsage::JunctionOnly;
	TestTrue(TEXT("Junction-only provider admits junction placement"),
		LayoutProfileSolverInternal::ValidateOwnedSeamJunctionRequirements(
			{Requirement}, Seams, RequestsByPath, ResultIndexByPath, ScheduleResult, FailureReason));
	return true;
}

bool FLayoutSeamOwnershipFiltersCandidateDomainsByJunctionUsageTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.RegionDebugPath = TEXT("Owner");
	const auto AddModule = [&Request](
		const FLayoutId SnapshotId,
		const ELayoutSeamJunctionUsage JunctionUsage)
	{
		FLayoutModuleSolveSnapshot& Module = Request.ModuleCatalog.Modules.AddDefaulted_GetRef();
		Module.SnapshotId = SnapshotId;
		Module.BoundsCells = FIntVector(1, 1, 1);
		Module.AllowedYawRotationSteps = {0};
		FLayoutSeamProviderIntent& Intent = Module.SeamProviderIntents.AddDefaulted_GetRef();
		Intent.SeamIntentId = FName(*FString::Printf(TEXT("%s.Intent"), *SnapshotId.ToString()));
		Intent.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
		Intent.JunctionUsage = JunctionUsage;
		Intent.bCanOwnSeam = true;
		for (const ELayoutFaceDirection FaceDirection :
			{ELayoutFaceDirection::PosX, ELayoutFaceDirection::PosY})
		{
			FLayoutDerivedSpanOffer& Offer = Module.DerivedSpanOffers.AddDefaulted_GetRef();
			Offer.SpanOfferId = FLayoutId(*FString::Printf(
				TEXT("%s.%d"),
				*SnapshotId.ToString(),
				static_cast<int32>(FaceDirection)));
			Offer.LocalCell = FIntVector::ZeroValue;
			Offer.FaceDirection = FaceDirection;
			Offer.bSealsBoundary = true;
		}
	};
	AddModule(TEXT("Ordinary"), ELayoutSeamJunctionUsage::NonJunctionOnly);
	AddModule(TEXT("Junction"), ELayoutSeamJunctionUsage::JunctionOnly);
	AddModule(TEXT("Both"), ELayoutSeamJunctionUsage::Both);

	FLayoutPartitionSeamRecord ContinuingSeam;
	ContinuingSeam.SeamId = TEXT("Continue");
	ContinuingSeam.OwnerRegionDebugPath = TEXT("Owner");
	ContinuingSeam.PassiveRegionDebugPath = TEXT("PassiveA");
	ContinuingSeam.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
	ContinuingSeam.OwnerFaceDirection = ELayoutFaceDirection::PosX;
	ContinuingSeam.OwnerStartCell = FIntVector::ZeroValue;
	ContinuingSeam.OwnerEndCell = FIntVector(1, 0, 0);
	ContinuingSeam.SegmentCount = 2;
	FLayoutPartitionSeamRecord BranchSeam = ContinuingSeam;
	BranchSeam.SeamId = TEXT("Branch");
	BranchSeam.PassiveRegionDebugPath = TEXT("PassiveB");
	BranchSeam.OwnerFaceDirection = ELayoutFaceDirection::PosY;
	BranchSeam.OwnerEndCell = FIntVector::ZeroValue;
	BranchSeam.SegmentCount = 1;

	FString FailureReason;
	TestTrue(FString::Printf(TEXT("Ordinary seam domain filters: %s"), *FailureReason),
		LayoutProfileSolverInternal::ApplyOwnedSeamUsageRestrictionsForTests(
			TEXT("Owner"), {ContinuingSeam}, Request, FailureReason));
	if (!TestEqual(TEXT("Ordinary seam keeps two candidates"),
		Request.CandidateDomainRestrictions[0].AllowedCandidates.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("Ordinary seam keeps NonJunctionOnly"),
		Request.CandidateDomainRestrictions[0].AllowedCandidates.ContainsByPredicate(
			[](const FLayoutCandidateVariantIdentity& Candidate)
			{
				return Candidate.ModuleSnapshotId == TEXT("Ordinary");
			}));
	TestFalse(TEXT("Ordinary seam excludes JunctionOnly"),
		Request.CandidateDomainRestrictions[0].AllowedCandidates.ContainsByPredicate(
			[](const FLayoutCandidateVariantIdentity& Candidate)
			{
				return Candidate.ModuleSnapshotId == TEXT("Junction");
			}));

	Request.CandidateDomainRestrictions.Reset();
	TestTrue(FString::Printf(TEXT("Junction seam domain filters: %s"), *FailureReason),
		LayoutProfileSolverInternal::ApplyOwnedSeamUsageRestrictionsForTests(
			TEXT("Owner"), {ContinuingSeam, BranchSeam}, Request, FailureReason));
	if (!TestEqual(TEXT("Junction seam keeps two candidates"),
		Request.CandidateDomainRestrictions[0].AllowedCandidates.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("Junction seam keeps JunctionOnly"),
		Request.CandidateDomainRestrictions[0].AllowedCandidates.ContainsByPredicate(
			[](const FLayoutCandidateVariantIdentity& Candidate)
			{
				return Candidate.ModuleSnapshotId == TEXT("Junction");
			}));
	TestFalse(TEXT("Junction seam excludes NonJunctionOnly"),
		Request.CandidateDomainRestrictions[0].AllowedCandidates.ContainsByPredicate(
			[](const FLayoutCandidateVariantIdentity& Candidate)
			{
				return Candidate.ModuleSnapshotId == TEXT("Ordinary");
			}));

	for (FLayoutModuleSolveSnapshot& Module : Request.ModuleCatalog.Modules)
	{
		Module.SeamProviderIntents[0].JunctionUsage =
			ELayoutSeamJunctionUsage::NonJunctionOnly;
	}
	Request.CandidateDomainRestrictions.Reset();
	TestFalse(TEXT("Missing junction provider rejects before proof"),
		LayoutProfileSolverInternal::ApplyOwnedSeamUsageRestrictionsForTests(
			TEXT("Owner"), {ContinuingSeam, BranchSeam}, Request, FailureReason));
	TestTrue(TEXT("Empty-domain diagnostic names adjacency class"),
		FailureReason.Contains(TEXT("adjacency=")));
	TestTrue(TEXT("Empty-domain diagnostic names excluded providers"),
		FailureReason.Contains(TEXT("excludedProviders=[")));

	FLayoutRegionSolveRequest ParentCatalogRequest = Request;
	ParentCatalogRequest.CandidateDomainRestrictions.Reset();
	for (FLayoutModuleSolveSnapshot& Module : ParentCatalogRequest.ModuleCatalog.Modules)
	{
		Module.SeamProviderIntents[0].JunctionUsage = ELayoutSeamJunctionUsage::Both;
	}
	FLayoutRegionSolveRequest ChildRequest = Request;
	ChildRequest.RegionDebugPath = TEXT("Child");
	ChildRequest.CandidateDomainRestrictions.Reset();
	FLayoutPartitionSeamRecord ChildContinuingSeam = ContinuingSeam;
	ChildContinuingSeam.ParentRegionDebugPath = TEXT("Owner");
	ChildContinuingSeam.OwnerRegionDebugPath = TEXT("Child");
	ChildContinuingSeam.OwnerStartCell = FIntVector(5, 0, 0);
	ChildContinuingSeam.OwnerEndCell = FIntVector(6, 0, 0);
	FLayoutPartitionSeamRecord ChildBranchSeam = BranchSeam;
	ChildBranchSeam.ParentRegionDebugPath = TEXT("Owner");
	ChildBranchSeam.OwnerRegionDebugPath = TEXT("Child");
	ChildBranchSeam.OwnerStartCell = FIntVector(5, 0, 0);
	ChildBranchSeam.OwnerEndCell = FIntVector(5, 0, 0);
	TestFalse(TEXT("Parent catalog cannot substitute for missing child junction provider"),
		LayoutProfileSolverInternal::ApplyChildOwnedSeamUsageRestrictionsForTests(
			TEXT("Owner"),
			TEXT("Child"),
			FIntVector(5, 0, 0),
			{ChildContinuingSeam, ChildBranchSeam},
			ParentCatalogRequest,
			ChildRequest,
			FailureReason));
	ChildRequest.CandidateDomainRestrictions.Reset();
	ChildRequest.ModuleCatalog.Modules[1].SeamProviderIntents[0].JunctionUsage =
		ELayoutSeamJunctionUsage::JunctionOnly;
	TestTrue(FString::Printf(TEXT("Child catalog supplies child-owned junction: %s"), *FailureReason),
		LayoutProfileSolverInternal::ApplyChildOwnedSeamUsageRestrictionsForTests(
			TEXT("Owner"),
			TEXT("Child"),
			FIntVector(5, 0, 0),
			{ChildContinuingSeam, ChildBranchSeam},
			ParentCatalogRequest,
			ChildRequest,
			FailureReason));
	return true;
}
