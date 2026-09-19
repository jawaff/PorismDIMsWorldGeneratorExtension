// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Testing/LayoutProfileJsonFixture.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionScheduleSolverFacade.h"
#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;
	const FString CoordinatorFallbackMessageSubstring =
		TEXT("fell back to the legacy recursive body after coordinator failure");

	UObject* CreatePartitionSeamTestOuter(const TCHAR* BaseName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return CreatePackage(*PackageName);
	}

	ULayoutModuleAsset* CreateSharedWallModule(UObject* Outer, const TCHAR* Prefix)
	{
		UChunkStructureTemplate* Template = CreateTemplate(Outer, *FString::Printf(TEXT("%s_Template"), Prefix), FIntVector(8, 8, 8));
		return CreateModule(
			Outer,
			*FString::Printf(TEXT("%s_Module"), Prefix),
			Template,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Entry, ELayoutCellIntent::Interior},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary}),
				FGameplayTagContainer(),
				FGameplayTagContainer()));
	}

	ULayoutRegionContentSetAsset* CreateModuleContentSet(
		UObject* Outer,
		const TCHAR* Name,
		ULayoutModuleAsset* Module,
		const TArray<FLayoutClosureProviderIntent>& ClosureProviderIntents,
		const TArray<FLayoutSeamProviderIntent>& SeamProviderIntents)
	{
		FLayoutRegionContentEntry Entry;
		Entry.EntryId = FName(Name);
		Entry.ContentKind = ELayoutRegionContentKind::Module;
		Entry.Weight = 1;
		Entry.ModuleSettings.Module = Module;
		Entry.ClosureProviderIntents = ClosureProviderIntents;
		Entry.SeamProviderIntents = SeamProviderIntents;
		return CreateRegionContentSet(Outer, Name, {Entry});
	}

	FLayoutRegionSolveRequest BuildSuppliedCellRequest(
		ULayoutRegionContentSetAsset* ContentSet,
		ULayoutProfileAsset* Profile,
		const int32 Seed,
		const FString& RegionPath,
		const FIntPoint& FootprintSize,
		const TArray<FIntVector>& Cells)
	{
		FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(ContentSet, Profile, Seed, RegionPath);
		/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
		Request.FootprintSize = FootprintSize;
		for (const FIntVector& Cell : Cells)
		{
			FLayoutPlannedCell& PlannedCell = Request.PlannedCells.AddDefaulted_GetRef();
			PlannedCell.Cell = Cell;
			PlannedCell.Intent = ELayoutCellIntent::Boundary;
		}
		return Request;
	}

	int32 CountPlacementsAtWorldCell(const FLayoutSolveResult& SolveResult, const FIntVector& WorldCell)
	{
		return SolveResult.Placements.FilterByPredicate([&WorldCell](const FLayoutPlacedModule& Placement)
		{
			return Placement.Cell == WorldCell;
		}).Num();
	}

	void AddOuterPerimeterClosure(ULayoutProfileAsset* Profile)
	{
		FLayoutClosureRequirement& ClosureRequirement = Profile->ClosureRequirements.AddDefaulted_GetRef();
		ClosureRequirement.ClosureId = TEXT("OuterPerimeter");
		ClosureRequirement.BoundsPolicy.Mode = ELayoutBoundsPolicyMode::SolvedFootprint;
		ClosureRequirement.BoundsPolicy.InsetCells = 0;
		ClosureRequirement.BoundsPolicy.MinLevel = 0;
		ClosureRequirement.BoundsPolicy.MaxLevel = 0;
		ClosureRequirement.MinThicknessCells = 1;
			}

	FLayoutSeamProviderIntent MakeSeamIntent(
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

	bool LoadFixtureAndSolveRegionTree(
		FAutomationTestBase& Test,
		const TCHAR* FixtureName,
		const TCHAR* RelativeFixturePath,
		const TCHAR* RootRegionPath,
		const int32 Seed,
		FLayoutRegionSolveScheduleResult& OutScheduleResult)
	{
		const FString FixturePath = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir() / RelativeFixturePath);
		FString Json;
		Test.TestTrue(
			*FString::Printf(TEXT("%s fixture file exists"), FixtureName),
			FPaths::FileExists(FixturePath));
		Test.TestTrue(
			*FString::Printf(TEXT("%s fixture loads"), FixtureName),
			FFileHelper::LoadFileToString(Json, *FixturePath));
		if (Json.IsEmpty())
		{
			return false;
		}

		UObject* Outer = CreatePartitionSeamTestOuter(FixtureName);
		FLayoutProfileJsonFixtureAssets ImportedAssets;
		TArray<FString> ImportIssues;
		Test.TestTrue(
			*FString::Printf(TEXT("%s fixture imports"), FixtureName),
			FLayoutProfileJsonFixture::ImportFromString(Json, Outer, ImportedAssets, ImportIssues));
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s fixture imports without issues"), FixtureName),
			ImportIssues.Num(),
			0))
		{
			for (const FString& Issue : ImportIssues)
			{
				Test.AddInfo(Issue);
			}
		}
		Test.TestNotNull(
			*FString::Printf(TEXT("%s profile reconstructed"), FixtureName),
			ImportedAssets.Profile.Get());
		Test.TestNotNull(
			*FString::Printf(TEXT("%s content set reconstructed"), FixtureName),
			ImportedAssets.ContentSet.Get());
		if (ImportedAssets.Profile == nullptr || ImportedAssets.ContentSet == nullptr)
		{
			return false;
		}

		const FLayoutRegionSolveRequest RootRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ImportedAssets.Profile.Get(),
			Seed,
			RootRegionPath);
		OutScheduleResult = FLayoutProfileSolver::SolveRegionTree(RootRequest);
		if (!OutScheduleResult.bSucceeded)
		{
			Test.AddError(OutScheduleResult.FailureReason);
			for (const FLayoutValidationMessage& Message : OutScheduleResult.MergedSolveResult.Messages)
			{
				Test.AddInfo(Message.Message);
			}
		}

		return OutScheduleResult.bSucceeded;
	}

	bool LoadFixtureAndBuildRootRequest(
		FAutomationTestBase& Test,
		const TCHAR* FixtureName,
		const TCHAR* RelativeFixturePath,
		const TCHAR* RootRegionPath,
		const int32 Seed,
		FLayoutRegionSolveRequest& OutRootRequest)
	{
		const FString FixturePath = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir() / RelativeFixturePath);
		FString Json;
		Test.TestTrue(
			*FString::Printf(TEXT("%s fixture file exists"), FixtureName),
			FPaths::FileExists(FixturePath));
		Test.TestTrue(
			*FString::Printf(TEXT("%s fixture loads"), FixtureName),
			FFileHelper::LoadFileToString(Json, *FixturePath));
		if (Json.IsEmpty())
		{
			return false;
		}

		UObject* Outer = CreatePartitionSeamTestOuter(FixtureName);
		FLayoutProfileJsonFixtureAssets ImportedAssets;
		TArray<FString> ImportIssues;
		Test.TestTrue(
			*FString::Printf(TEXT("%s fixture imports"), FixtureName),
			FLayoutProfileJsonFixture::ImportFromString(Json, Outer, ImportedAssets, ImportIssues));
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s fixture imports without issues"), FixtureName),
			ImportIssues.Num(),
			0))
		{
			for (const FString& Issue : ImportIssues)
			{
				Test.AddInfo(Issue);
			}
		}
		Test.TestNotNull(
			*FString::Printf(TEXT("%s profile reconstructed"), FixtureName),
			ImportedAssets.Profile.Get());
		Test.TestNotNull(
			*FString::Printf(TEXT("%s content set reconstructed"), FixtureName),
			ImportedAssets.ContentSet.Get());
		if (ImportedAssets.Profile == nullptr || ImportedAssets.ContentSet == nullptr)
		{
			return false;
		}

		OutRootRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ImportedAssets.Profile.Get(),
			Seed,
			RootRegionPath);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPartitionSeamMixedHeightOverlapDirectionsTest,
	"PorismExtension.Layout.Solver.Seams.MixedHeightOverlapDirections",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPartitionSeamMixedHeightSiblingAuthoritativeSeamTest,
	"PorismExtension.Layout.Solver.Seams.MixedHeightSiblingAuthoritativeSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPartitionSeamAuthoritativeDoorPreservesPassiveAnchorTest,
	"PorismExtension.Layout.Solver.Seams.AuthoritativeDoorPreservesPassiveAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPartitionSeamMixedHeightOverlapDirectionsTest::RunTest(const FString& Parameters)
{
	ELayoutFaceDirection SchedulerCandidateFace = ELayoutFaceDirection::PosZ;
	ELayoutFaceDirection SchedulerExistingFace = ELayoutFaceDirection::PosZ;
	const bool bSchedulerDetectedOverlap = FLayoutProfileSolver::DebugTryGetSiblingSharedOverlapFaceDirections(
		FIntVector(1, 0, 0),
		FIntPoint(2, 2),
		FIntVector(0, 0, 1),
		FIntPoint(2, 2),
		SchedulerCandidateFace,
		SchedulerExistingFace);
	TestTrue(TEXT("Recursive scheduling detects same-world overlap even when sibling local Z differs"), bSchedulerDetectedOverlap);
	TestEqual(TEXT("Scheduler overlap gives the upper sibling the outward PosX seam face"), SchedulerCandidateFace, ELayoutFaceDirection::PosX);
	TestEqual(TEXT("Scheduler overlap gives the tall sibling the inward NegX seam face"), SchedulerExistingFace, ELayoutFaceDirection::NegX);

	ELayoutFaceDirection ClosureFirstFace = ELayoutFaceDirection::PosZ;
	ELayoutFaceDirection ClosureSecondFace = ELayoutFaceDirection::PosZ;
	const bool bClosureDetectedOverlap = FLayoutProfileSolver::DebugTryGetClosureSharedOverlapFaceDirections(
		FIntVector(1, 0, 0),
		FIntPoint(2, 2),
		FIntVector(0, 0, 1),
		FIntPoint(2, 2),
		ClosureFirstFace,
		ClosureSecondFace);
	TestTrue(TEXT("Merged seam auditing detects same-world overlap even when sibling local Z differs"), bClosureDetectedOverlap);
	TestEqual(TEXT("Closure overlap gives the first sibling the outward PosX seam face"), ClosureFirstFace, ELayoutFaceDirection::PosX);
	TestEqual(TEXT("Closure overlap gives the second sibling the inward NegX seam face"), ClosureSecondFace, ELayoutFaceDirection::NegX);
	return bSchedulerDetectedOverlap
		&& bClosureDetectedOverlap
		&& SchedulerCandidateFace == ELayoutFaceDirection::PosX
		&& SchedulerExistingFace == ELayoutFaceDirection::NegX
		&& ClosureFirstFace == ELayoutFaceDirection::PosX
		&& ClosureSecondFace == ELayoutFaceDirection::NegX;
}

bool FLayoutPartitionSeamMixedHeightSiblingAuthoritativeSeamTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePartitionSeamTestOuter(TEXT("MixedHeightSiblingAuthoritativeSeam"));
	ULayoutModuleAsset* SharedWallModule = CreateSharedWallModule(Outer, TEXT("MixedHeightSibling"));
	const TArray<FLayoutSeamProviderIntent> SeamProviderIntents =
	{
		MakeSeamIntent(TEXT("SharedWall"), LayoutGameplayTags::InterfacePartitionSolid, true, true)
	};
	ULayoutRegionContentSetAsset* ContentSet = CreateModuleContentSet(
		Outer,
		TEXT("MixedHeightSiblingContent"),
		SharedWallModule,
		{},
		SeamProviderIntents);
	ULayoutProfileAsset* UpperProfile = CreateProfile(
		Outer,
		TEXT("MixedHeightUpperProfile"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	ULayoutProfileAsset* TallProfile = CreateProfile(
		Outer,
		TEXT("MixedHeightTallProfile"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		2,
		0,
		false);
	UpperProfile->ContentSet = ContentSet;
	TallProfile->ContentSet = ContentSet;
	if (!TestNotNull(TEXT("Mixed-height authoritative seam content set is valid"), ContentSet)
		|| !TestNotNull(TEXT("Mixed-height authoritative seam upper profile is valid"), UpperProfile)
		|| !TestNotNull(TEXT("Mixed-height authoritative seam tall profile is valid"), TallProfile))
	{
		return false;
	}

	TArray<FIntVector> UpperCells;
	for (int32 Y = 0; Y < 2; ++Y)
	{
		for (int32 X = 0; X < 2; ++X)
		{
			UpperCells.Add(FIntVector(X, Y, 0));
		}
	}

	TArray<FIntVector> TallCells;
	for (int32 Z = 0; Z < 2; ++Z)
	{
		for (int32 Y = 0; Y < 2; ++Y)
		{
			for (int32 X = 0; X < 2; ++X)
			{
				TallCells.Add(FIntVector(X, Y, Z));
			}
		}
	}

	FLayoutRegionSolveRequest UpperRequest = BuildSuppliedCellRequest(
		ContentSet,
		UpperProfile,
		7001,
		TEXT("MixedHeightSiblingUpper"),
		FIntPoint(2, 2),
		UpperCells);
	UpperRequest.RegionCellOffset = FIntVector(0, 0, 1);

	FLayoutRegionSolveRequest TallRequest = BuildSuppliedCellRequest(
		ContentSet,
		TallProfile,
		7002,
		TEXT("MixedHeightSiblingTall"),
		FIntPoint(2, 2),
		TallCells);
	TallRequest.RegionCellOffset = FIntVector(1, 0, 0);

	FLayoutRegionSolveScheduleRequest ScheduleRequest;
	ScheduleRequest.RegionRequests = {UpperRequest, TallRequest};
	FLayoutPartitionSeamRecord& SeamRecord = ScheduleRequest.PlannedPartitionSeams.AddDefaulted_GetRef();
	SeamRecord.ParentRegionDebugPath = TEXT("MixedHeightSiblingRoot");
	SeamRecord.OwnerRegionDebugPath = UpperRequest.RegionDebugPath;
	SeamRecord.PassiveRegionDebugPath = TallRequest.RegionDebugPath;
	SeamRecord.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
	SeamRecord.OwnerFaceDirection = ELayoutFaceDirection::PosX;
	SeamRecord.PassiveFaceDirection = ELayoutFaceDirection::NegX;
	SeamRecord.OwnerStartCell = FIntVector(1, 0, 1);
	SeamRecord.OwnerEndCell = FIntVector(1, 1, 1);
	SeamRecord.PassiveStartCell = FIntVector(1, 0, 1);
	SeamRecord.PassiveEndCell = FIntVector(1, 1, 1);
	SeamRecord.SegmentCount = 2;
	SeamRecord.SeamId = TEXT("MixedHeightSiblingRoot.MixedHeightSiblingUpper.MixedHeightSiblingTall");
	ScheduleRequest.bPlannedPartitionSeamsAreAuthoritative = true;

	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionsSynchronously(ScheduleRequest);
	if (!TestTrue(TEXT("Mixed-height authoritative seam schedule succeeds"), ScheduleResult.bSucceeded))
	{
		AddError(ScheduleResult.FailureReason);
		return false;
	}

	const FLayoutRegionSolveResult* TallRegionResult = ScheduleResult.RegionResults.FindByPredicate([](const FLayoutRegionSolveResult& RegionResult)
	{
		return RegionResult.RegionDebugPath == TEXT("MixedHeightSiblingTall");
	});
	if (!TestNotNull(TEXT("Mixed-height authoritative seam resolves the tall region result"), TallRegionResult))
	{
		return false;
	}

	TestEqual(
		TEXT("Mixed-height authoritative seam suppresses the passive tall-region overlap cells"),
		TallRegionResult->SolveResult.Placements.Num(),
		6);
	TestEqual(
		TEXT("Merged solve keeps exactly one placement at the first overlapping upper seam cell"),
		CountPlacementsAtWorldCell(ScheduleResult.MergedSolveResult, FIntVector(1, 0, 1)),
		1);
	TestEqual(
		TEXT("Merged solve keeps exactly one placement at the second overlapping upper seam cell"),
		CountPlacementsAtWorldCell(ScheduleResult.MergedSolveResult, FIntVector(1, 1, 1)),
		1);
	TestTrue(
		TEXT("Merged solve keeps the explicit mixed-height sibling seam record"),
		ScheduleResult.MergedSolveResult.PartitionSeams.ContainsByPredicate([](const FLayoutPartitionSeamRecord& CandidateSeam)
		{
			return CandidateSeam.OwnerRegionDebugPath == TEXT("MixedHeightSiblingUpper")
				&& CandidateSeam.PassiveRegionDebugPath == TEXT("MixedHeightSiblingTall")
				&& CandidateSeam.OwnerStartCell == FIntVector(1, 0, 1)
				&& CandidateSeam.OwnerEndCell == FIntVector(1, 1, 1)
				&& CandidateSeam.PassiveStartCell == FIntVector(1, 0, 1)
				&& CandidateSeam.PassiveEndCell == FIntVector(1, 1, 1);
		}));
	return true;
}

bool FLayoutPartitionSeamAuthoritativeDoorPreservesPassiveAnchorTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePartitionSeamTestOuter(TEXT("AuthoritativeDoorPreservesPassiveAnchor"));
	ULayoutModuleAsset* DoorModule = CreateSharedWallModule(Outer, TEXT("AuthoritativeDoor"));
	ULayoutRegionContentSetAsset* ContentSet = CreateModuleContentSet(
		Outer,
		TEXT("AuthoritativeDoorContent"),
		DoorModule,
		{},
		{MakeSeamIntent(TEXT("SharedDoor"), LayoutGameplayTags::InterfacePartitionDoor, true, true)});
	ULayoutProfileAsset* OwnerProfile = CreateProfile(
		Outer, TEXT("AuthoritativeDoorOwnerProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	ULayoutProfileAsset* PassiveProfile = CreateProfile(
		Outer, TEXT("AuthoritativeDoorPassiveProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	OwnerProfile->ContentSet = ContentSet;
	PassiveProfile->ContentSet = ContentSet;

	FLayoutRegionSolveRequest OwnerRequest = BuildSuppliedCellRequest(
		ContentSet, OwnerProfile, 7101, TEXT("AuthoritativeDoorOwner"), FIntPoint(1, 1), {FIntVector::ZeroValue});
	FLayoutRegionSolveRequest PassiveRequest = BuildSuppliedCellRequest(
		ContentSet, PassiveProfile, 7102, TEXT("AuthoritativeDoorPassive"), FIntPoint(1, 1), {FIntVector::ZeroValue});
	PassiveRequest.RegionCellOffset = FIntVector(1, 0, 0);
	PassiveRequest.PlannedCells[0].Intent = ELayoutCellIntent::Entry;
	FLayoutCommittedEndpointAnchor& PassiveAnchor = PassiveRequest.CommittedEndpointAnchors.AddDefaulted_GetRef();
	PassiveAnchor.CommitmentId = TEXT("AuthoritativeDoorPassiveAnchor");
	PassiveAnchor.LocalCell = FIntVector::ZeroValue;
	PassiveAnchor.FaceDirection = ELayoutFaceDirection::NegX;
	PassiveAnchor.ConnectionTag = LayoutGameplayTags::FaceOpen;
	PassiveAnchor.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
	PassiveAnchor.TraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);

	FLayoutRegionSolveScheduleRequest ScheduleRequest;
	ScheduleRequest.RegionRequests = {OwnerRequest, PassiveRequest};
	ScheduleRequest.bPlannedPartitionSeamsAreAuthoritative = true;
	FLayoutPartitionSeamRecord& Seam = ScheduleRequest.PlannedPartitionSeams.AddDefaulted_GetRef();
	Seam.SeamId = TEXT("AuthoritativeDoorSeam");
	Seam.ParentRegionDebugPath = OwnerRequest.RegionDebugPath;
	Seam.OwnerRegionDebugPath = OwnerRequest.RegionDebugPath;
	Seam.PassiveRegionDebugPath = PassiveRequest.RegionDebugPath;
	Seam.InterfaceFamily = LayoutGameplayTags::InterfacePartitionDoor;
	Seam.OwnerFaceDirection = ELayoutFaceDirection::PosX;
	Seam.PassiveFaceDirection = ELayoutFaceDirection::NegX;
	Seam.OwnerStartCell = FIntVector(0, 0, 0);
	Seam.OwnerEndCell = Seam.OwnerStartCell;
	Seam.PassiveStartCell = FIntVector(1, 0, 0);
	Seam.PassiveEndCell = Seam.PassiveStartCell;
	Seam.SegmentCount = 1;

	const FLayoutRegionSolveScheduleResult Result =
		FLayoutProfileSolver::SolveRegionsSynchronously(ScheduleRequest);
	if (!TestTrue(TEXT("Authoritative shared-door schedule succeeds"), Result.bSucceeded))
	{
		AddError(Result.FailureReason);
		return false;
	}
	const FLayoutRegionSolveResult* PassiveResult = Result.RegionResults.FindByPredicate(
		[](const FLayoutRegionSolveResult& Region)
		{
			return Region.RegionDebugPath == TEXT("AuthoritativeDoorPassive");
		});
	if (!TestNotNull(TEXT("Shared-door schedule keeps passive region result"), PassiveResult))
	{
		return false;
	}
	TestEqual(TEXT("Shared-door passive endpoint anchor survives suppression"), PassiveResult->CommittedEndpointAnchors.Num(), 1);
	TestEqual(TEXT("Shared-door passive entry placement survives for endpoint coverage"), PassiveResult->SolveResult.Placements.Num(), 1);
	TestTrue(TEXT("Merged result preserves authoritative shared-door seam id"), Result.MergedSolveResult.PartitionSeams.ContainsByPredicate(
		[](const FLayoutPartitionSeamRecord& Candidate)
		{
			return Candidate.SeamId == TEXT("AuthoritativeDoorSeam");
		}));
	return true;
}
