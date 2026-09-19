// Copyright 2026 Spotted Loaf Studio

#include "Layout/Testing/LayoutProfileJsonFixture.h"

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace
{
	/** Imports one transient fixture for perimeter-child integration coverage. */
	bool LoadPerimeterFixture(
		FAutomationTestBase& Test,
		FLayoutProfileJsonFixtureAssets& OutAssets)
	{
		const FString FixturePath = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir()
			/ TEXT("Plugins/PorismDIMsWorldGeneratorExtension/Source/PorismDIMsWorldGeneratorExtensionTests/Private/Layout/Fixtures/Data/DA_Profile_Castle_DA_ContentSet_Castle_MultiChildForcedSeamShare.json"));
		FString Json;
		if (!Test.TestTrue(TEXT("Perimeter fixture exists"), FPaths::FileExists(FixturePath))
			|| !Test.TestTrue(TEXT("Perimeter fixture loads"), FFileHelper::LoadFileToString(Json, *FixturePath)))
		{
			return false;
		}

		const FString PackageName = FString::Printf(
			TEXT("/Temp/LayoutPerimeterChildComposition_%s"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		TArray<FString> ImportIssues;
		if (!Test.TestTrue(
			TEXT("Perimeter fixture imports"),
			FLayoutProfileJsonFixture::ImportFromString(
				Json,
				CreatePackage(*PackageName),
				OutAssets,
				ImportIssues)))
		{
			return false;
		}
		for (const FString& ImportIssue : ImportIssues)
		{
			Test.AddError(ImportIssue);
		}
		return Test.TestEqual(TEXT("Perimeter fixture imports without issues"), ImportIssues.Num(), 0)
			&& OutAssets.Profile != nullptr
			&& OutAssets.ContentSet != nullptr;
	}

	/** Builds one exact 8x8 flat parent request from mutated transient assets. */
	bool BuildPerimeterRootRequest(
		FAutomationTestBase& Test,
		const FLayoutProfileJsonFixtureAssets& Assets,
		FLayoutRegionSolveRequest& OutRequest)
	{
		FString FailureReason;
		const bool bBuilt = FLayoutProfileJsonFixture::BuildImportedRootRequest(
			Assets,
			-149679109,
			TEXT("PerimeterChildComposition"),
			OutRequest,
			FLayoutRootSolveBudgetSettings(),
			NAME_None,
			NAME_None,
			0,
			&FailureReason);
		if (!Test.TestTrue(TEXT("Perimeter root request builds"), bBuilt))
		{
			Test.AddError(FailureReason);
		}
		return bBuilt;
	}

	/** Configures one exact-four counted child demand in the requested parent zone. */
	bool BuildFourZoneChildRequest(
		FAutomationTestBase& Test,
		const ELayoutPlacementZone RequiredZone,
		FLayoutRegionSolveRequest& OutRequest)
	{
		FLayoutProfileJsonFixtureAssets Assets;
		if (!LoadPerimeterFixture(Test, Assets))
		{
			return false;
		}

		Assets.Profile->MinimumFootprintInCells = FIntPoint(8, 8);
		Assets.Profile->MaximumFootprintInCells = FIntPoint(8, 8);
		Assets.Profile->EntryCountMode = ELayoutCountConstraintMode::None;
		Assets.Profile->EntryCount = 0;
		Assets.Profile->MinEntryCount = 0;
		Assets.Profile->MaxEntryCount = 0;
		Assets.Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::None;
		Assets.Profile->VerticalAccessCount = 0;
		Assets.Profile->MinVerticalAccessCount = 0;
		Assets.Profile->MaxVerticalAccessCount = 0;
		Assets.Profile->bRequireAllTraversalChannelsReachable = false;
		if (!Test.TestEqual(
			TEXT("Fixture exposes one child feature requirement"),
			Assets.Profile->ZoneFeatureRequirements.Num(),
			1))
		{
			return false;
		}
		FLayoutZoneFeatureRequirement& Requirement = Assets.Profile->ZoneFeatureRequirements[0];
		Requirement.RequirementId = TEXT("CountedZoneChild");
		Requirement.Zone = RequiredZone;
		Requirement.MinCount = 4;
		Requirement.MaxCount = 4;

		FLayoutRegionContentEntry* ChildEntry = Assets.ContentSet->Entries.FindByPredicate(
			[](const FLayoutRegionContentEntry& Entry)
			{
				return Entry.ContentKind == ELayoutRegionContentKind::ChildRegion;
			});
		if (!Test.TestNotNull(TEXT("Fixture exposes counted child entry"), ChildEntry))
		{
			return false;
		}
		ChildEntry->EntryId = TEXT("ZoneChild");
		// Requirement zone must constrain the selected child value even when provider entry allows any zone.
		ChildEntry->ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Any;
		ULayoutProfileAsset* const ChildProfile = ChildEntry->ChildRegionSettings.RegionProfile;
		if (!Test.TestNotNull(TEXT("Counted child entry references profile"), ChildProfile))
		{
			return false;
		}
		ChildProfile->EntryCountMode = ELayoutCountConstraintMode::None;
		ChildProfile->EntryCount = 0;
		ChildProfile->MinEntryCount = 0;
		ChildProfile->MaxEntryCount = 0;
		// This placement-only fixture removes child entries. Optional traversal is profile-local,
		// so disabling it on the parent cannot discharge an entryless child's strict policy.
		ChildProfile->bRequireAllTraversalChannelsReachable = false;
		return BuildPerimeterRootRequest(Test, Assets, OutRequest);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPerimeterChildrenTowersOccupyAllFourCornersTest,
	"PorismExtension.Layout.Solver.PerimeterChildren.TowersOccupyAllFourCorners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPerimeterChildrenTowersOccupyAllFourCornersTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	if (!BuildFourZoneChildRequest(*this, ELayoutPlacementZone::Corner, Request))
	{
		return false;
	}
	Request.ExecutionSettings.MaxSolveDurationSeconds = 10.0f;
	const FLayoutRegionSolveScheduleResult ScheduleResult =
		FLayoutProfileSolver::SolveRegionTree(Request);
	if (!TestTrue(TEXT("Four-corner tower fixture solves"), ScheduleResult.bSucceeded))
	{
		AddError(ScheduleResult.FailureReason);
		return false;
	}

	int32 ChildCount = 0;
	uint8 CoveredCornerMask = 0;
	for (const FLayoutRegionSolveResult& RegionResult : ScheduleResult.RegionResults)
	{
		if (!RegionResult.RegionDebugPath.StartsWith(Request.RegionDebugPath + TEXT("/")))
		{
			continue;
		}
		++ChildCount;
		const FIntPoint ChildFootprint = RegionResult.SolveResult.FootprintSize;
		const int32 MinX = RegionResult.RegionCellOffset.X;
		const int32 MinY = RegionResult.RegionCellOffset.Y;
		const int32 MaxX = MinX + ChildFootprint.X - 1;
		const int32 MaxY = MinY + ChildFootprint.Y - 1;
		if (MinX == 0 && MinY == 0)
		{
			CoveredCornerMask |= 1 << 0;
		}
		if (MaxX == Request.FootprintSize.X - 1 && MinY == 0)
		{
			CoveredCornerMask |= 1 << 1;
		}
		if (MaxX == Request.FootprintSize.X - 1 && MaxY == Request.FootprintSize.Y - 1)
		{
			CoveredCornerMask |= 1 << 2;
		}
		if (MinX == 0 && MaxY == Request.FootprintSize.Y - 1)
		{
			CoveredCornerMask |= 1 << 3;
		}
	}

	TestEqual(TEXT("Exactly four tower child regions solve"), ChildCount, 4);
	return TestEqual(TEXT("Tower children occupy all four parent corners"), CoveredCornerMask, uint8(0x0f));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPerimeterChildrenCountedEdgeChildrenCoverAllFourSidesTest,
	"PorismExtension.Layout.Solver.PerimeterChildren.CountedEdgeChildrenCoverAllFourSides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPerimeterChildrenCountedEdgeChildrenCoverAllFourSidesTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	if (!BuildFourZoneChildRequest(*this, ELayoutPlacementZone::Edge, Request))
	{
		return false;
	}
	Request.ExecutionSettings.MaxSolveDurationSeconds = 10.0f;
	const FLayoutRegionSolveScheduleResult ScheduleResult =
		FLayoutProfileSolver::SolveRegionTree(Request);
	if (!TestTrue(TEXT("Four-edge counted-child fixture solves"), ScheduleResult.bSucceeded))
	{
		AddError(ScheduleResult.FailureReason);
		return false;
	}

	int32 ChildCount = 0;
	uint8 CoveredEdgeMask = 0;
	for (const FLayoutRegionSolveResult& RegionResult : ScheduleResult.RegionResults)
	{
		if (!RegionResult.RegionDebugPath.StartsWith(Request.RegionDebugPath + TEXT("/")))
		{
			continue;
		}
		++ChildCount;
		const FIntPoint ChildFootprint = RegionResult.SolveResult.FootprintSize;
		const int32 MinX = RegionResult.RegionCellOffset.X;
		const int32 MinY = RegionResult.RegionCellOffset.Y;
		const int32 MaxX = MinX + ChildFootprint.X - 1;
		const int32 MaxY = MinY + ChildFootprint.Y - 1;
		const int32 ParentMaxX = Request.FootprintSize.X - 1;
		const int32 ParentMaxY = Request.FootprintSize.Y - 1;
		if (MinY == 0 && MaxX > 0 && MinX < ParentMaxX)
		{
			CoveredEdgeMask |= 1 << 0;
		}
		if (MaxX == ParentMaxX && MaxY > 0 && MinY < ParentMaxY)
		{
			CoveredEdgeMask |= 1 << 1;
		}
		if (MaxY == ParentMaxY && MaxX > 0 && MinX < ParentMaxX)
		{
			CoveredEdgeMask |= 1 << 2;
		}
		if (MinX == 0 && MaxY > 0 && MinY < ParentMaxY)
		{
			CoveredEdgeMask |= 1 << 3;
		}
	}

	TestEqual(TEXT("Exactly four edge child regions solve"), ChildCount, 4);
	return TestEqual(TEXT("Counted Edge children cover all four edge components"), CoveredEdgeMask, uint8(0x0f));
}
