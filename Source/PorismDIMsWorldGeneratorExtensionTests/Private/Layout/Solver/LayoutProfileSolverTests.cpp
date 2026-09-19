// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"

#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Async/LayoutBackgroundSolveCancellation.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionRequestSnapshotBuilder.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "Tasks/Task.h"

namespace
{
	using namespace PorismLayoutTestUtilities;

	UObject* CreateLayoutProfileSolverTestOuter(const TCHAR* BaseName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return CreatePackage(*PackageName);
	}

	/** Builds test-local ascent-only VA content without changing shared universal fixture semantics. */
	ULayoutProfileAsset* CreateProfileWithOneSidedVerticalAccessContentSet(
		UObject* Outer,
		const TCHAR* Name,
		const FIntPoint FootprintSize,
		const int32 LevelCount)
	{
		ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
			Outer,
			Name,
			FootprintSize,
			FootprintSize,
			LevelCount,
			1,
			false);
		FLayoutRegionContentEntry* const VerticalAccessEntry = Profile->ContentSet->Entries.FindByPredicate(
			[](const FLayoutRegionContentEntry& Entry)
			{
				return Entry.ModuleSettings.Module != nullptr
					&& Entry.ModuleSettings.Module->SupportsIntent(ELayoutCellIntent::VerticalAccess);
			});
		check(VerticalAccessEntry != nullptr);
		VerticalAccessEntry->ModuleSettings.Module->FaceRules.NegZ = MakeConnectionFaceRule(
			ELayoutFaceDirection::NegZ,
			LayoutGameplayTags::FaceSolid,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
		return Profile;
	}

	FLayoutId BuildRequestModuleBundleId(
		const FLayoutRegionSolveRequest& Request,
		const int32 ModuleIndex)
	{
		return FLayoutId(*FString::Printf(
			TEXT("%s.Bundle.%d.%s"),
			*Request.EffectiveSnapshotId.ToString(),
			ModuleIndex,
			*Request.ModuleCatalog.Modules[ModuleIndex].SnapshotId.ToString()));
	}

	TArray<FLayoutFaceRule> BuildOrientedBoundaryFaces(
		const bool bOpenPosX,
		const bool bOpenNegX,
		const bool bOpenPosY,
		const bool bOpenNegY,
		const bool bOpenTop,
		const FGameplayTagContainer& HorizontalWalkableAreas = FGameplayTagContainer(),
		const FGameplayTagContainer& TopWalkableAreas = FGameplayTagContainer(),
		const FGameplayTagContainer& BottomWalkableAreas = FGameplayTagContainer())
	{
		auto MakeHorizontal = [&](const ELayoutFaceDirection Direction, const bool bOpenToInterior)
		{
			return MakeFaceRule(
				Direction,
				MakeTags({bOpenToInterior ? LayoutGameplayTags::FaceOpen : LayoutGameplayTags::FaceSolid}),
				MakeTags({bOpenToInterior ? LayoutGameplayTags::FaceOpen : LayoutGameplayTags::FaceSolid, LayoutGameplayTags::FaceEntry}),
				bOpenToInterior ? ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor : ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor,
				bOpenToInterior ? HorizontalWalkableAreas : FGameplayTagContainer());
		};

		return {
			MakeHorizontal(ELayoutFaceDirection::PosX, bOpenPosX),
			MakeHorizontal(ELayoutFaceDirection::NegX, bOpenNegX),
			MakeHorizontal(ELayoutFaceDirection::PosY, bOpenPosY),
			MakeHorizontal(ELayoutFaceDirection::NegY, bOpenNegY),
			MakeFaceRule(
				ELayoutFaceDirection::PosZ,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				bOpenTop ? ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor : ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor,
				TopWalkableAreas),
			MakeFaceRule(
				ELayoutFaceDirection::NegZ,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				BottomWalkableAreas)
		};
	}

	FLayoutFaceRule MakeYawLockedFaceRule(
		const ELayoutFaceDirection Direction,
		const FGameplayTagContainer& ConnectionTags,
		const FGameplayTagContainer& AllowedConnectionTags,
		const ELayoutFaceOccupancyPolicy OccupancyPolicy,
		const FGameplayTagContainer& ConnectedTraversalChannels = FGameplayTagContainer())
	{
		FLayoutFaceRule Rule = MakeFaceRule(Direction, ConnectionTags, AllowedConnectionTags, OccupancyPolicy, ConnectedTraversalChannels);
		Rule.bRequireMatchingYawWithFilledNeighbor = true;
		return Rule;
	}




	/** Adds a permissive module so topology tests exercise VerticalAccess host selection, not content-set admission. */
	void AddUniversalVerticalAccessTestModule(
		FLayoutRegionSolveRequest& Request,
		UObject* Outer,
		const TCHAR* Name)
	{
		const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
		const FGameplayTagContainer TraversalTags = MakeTags({LayoutGameplayTags::TraversalPrimary});
		UChunkStructureTemplate* Template = CreateTemplate(
			Outer,
			*FString::Printf(TEXT("%s_Template"), Name),
			FIntVector(16, 16, 16));
		ULayoutModuleAsset* FillModule = CreateModule(
			Outer,
			*FString::Printf(TEXT("%s_Fill"), Name),
			Template,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior},
			BuildFilledCubeFaces(
				OpenTags, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				OpenTags, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				TraversalTags));
		ULayoutModuleAsset* VerticalAccessModule = CreateModule(
			Outer,
			*FString::Printf(TEXT("%s_VerticalAccess"), Name),
			Template,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::VerticalAccess},
			BuildFilledCubeFaces(
				OpenTags, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				OpenTags, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				TraversalTags, TraversalTags, TraversalTags));
		FLayoutRegionContentEntry FillEntry;
		FillEntry.EntryId = FName(*FString::Printf(TEXT("%s_FillEntry"), Name));
		FillEntry.ContentKind = ELayoutRegionContentKind::Module;
		FillEntry.ModuleSettings.Module = FillModule;
		FillEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Any;
		FLayoutRegionContentEntry VerticalAccessEntry;
		VerticalAccessEntry.EntryId = FName(*FString::Printf(TEXT("%s_VerticalAccessEntry"), Name));
		VerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
		VerticalAccessEntry.ModuleSettings.Module = VerticalAccessModule;
		VerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Any;
		Request.ContentSetSnapshot = FLayoutProfileSolver::BuildContentSetSnapshot(
			CreateRegionContentSet(Outer, *FString::Printf(TEXT("%s_ContentSet"), Name), {FillEntry, VerticalAccessEntry}));
		Request.ModuleCatalog.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(FillModule, 0));
		Request.ModuleCatalog.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(VerticalAccessModule, 1));
	}

	TArray<FLayoutFaceRule> BuildStressPatternFaces(
		const FGameplayTagContainer& PosXTags,
		const FGameplayTagContainer& NegXTags,
		const FGameplayTagContainer& PosYTags,
		const FGameplayTagContainer& NegYTags)
	{
		const FGameplayTagContainer WalkwayTags = MakeTags({LayoutGameplayTags::FaceOpen});
		return {
			MakeFaceRule(ELayoutFaceDirection::PosX, PosXTags, PosXTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, NegXTags, NegXTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, PosYTags, PosYTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, NegYTags, NegYTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
		};
	}


	TArray<FLayoutFaceRule> BuildRouteStressFaces(
		const FGameplayTagContainer& PosXWalkable,
		const FGameplayTagContainer& NegXWalkable,
		const FGameplayTagContainer& PosYWalkable,
		const FGameplayTagContainer& NegYWalkable,
		const FGameplayTagContainer& PosZWalkable = FGameplayTagContainer(),
		const FGameplayTagContainer& NegZWalkable = FGameplayTagContainer())
	{
		const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
		const FGameplayTagContainer OpenAndSolidTags = MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceSolid});
		const FGameplayTagContainer WalkwayTags = MakeTags({LayoutGameplayTags::FaceOpen});
		return {
			MakeFaceRule(ELayoutFaceDirection::PosX, OpenTags, OpenAndSolidTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PosXWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegX, OpenTags, OpenAndSolidTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, NegXWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, OpenTags, OpenAndSolidTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PosYWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegY, OpenTags, OpenAndSolidTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, NegYWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosZ, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, PosZWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegZ, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, NegZWalkable)
		};
	}


	TArray<FLayoutPlannedCell> BuildRouteStressPlannedCells(const int32 Width, const int32 Height)
	{
		TArray<FLayoutPlannedCell> PlannedCells;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				FLayoutPlannedCell& PlannedCell = PlannedCells.AddDefaulted_GetRef();
				PlannedCell.Cell = FIntVector(X, Y, 0);
				if (X == Width / 2 && Y == 0)
				{
					PlannedCell.Intent = ELayoutCellIntent::Entry;
				}
				else if (X == Width - 2 && Y == Height - 2)
				{
					PlannedCell.Intent = ELayoutCellIntent::VerticalAccess;
				}
				else if (X == Width / 2 && Y == Height / 2)
				{
					PlannedCell.Intent = ELayoutCellIntent::Core;
				}
				else if (X == 0 || X == Width - 1 || Y == 0 || Y == Height - 1)
				{
					PlannedCell.Intent = ELayoutCellIntent::Boundary;
				}
				else
				{
					PlannedCell.Intent = ELayoutCellIntent::Interior;
				}
			}
		}
		return PlannedCells;
	}

	bool WaitForCancelableSolveStart(
		TSharedRef<FThreadSafeBool, ESPMode::ThreadSafe> Started,
		const int32 MaxWaitCount = 200)
	{
		for (int32 WaitIndex = 0; WaitIndex < MaxWaitCount && !static_cast<bool>(*Started); ++WaitIndex)
		{
			FPlatformProcess::Sleep(0.005f);
		}
		return static_cast<bool>(*Started);
	}

	bool WaitForCancelableSolveFinish(
		TSharedRef<FThreadSafeBool, ESPMode::ThreadSafe> Finished,
		const int32 MaxWaitCount = 500)
	{
		for (int32 WaitIndex = 0; WaitIndex < MaxWaitCount && !static_cast<bool>(*Finished); ++WaitIndex)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		return static_cast<bool>(*Finished);
	}

	bool WaitForCancellationCheckpointHit(
		TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> Counter,
		const int32 MinHits = 1,
		const int32 MaxWaitCount = 200)
	{
		for (int32 WaitIndex = 0; WaitIndex < MaxWaitCount && Counter->GetValue() < MinHits; ++WaitIndex)
		{
			FPlatformProcess::Sleep(0.005f);
		}
		return Counter->GetValue() >= MinHits;
	}


	bool SolveResultsHaveMatchingPlacementsInternal(
		const FLayoutSolveResult& Left,
		const FLayoutSolveResult& Right,
		const bool bRequireMatchingSeed)
	{
		if (Left.bSucceeded != Right.bSucceeded
			|| (bRequireMatchingSeed && Left.Seed != Right.Seed)
			|| Left.FootprintSize != Right.FootprintSize
			|| Left.PlannedCells.Num() != Right.PlannedCells.Num()
			|| Left.Placements.Num() != Right.Placements.Num()
			|| Left.ExportedEntryCells.Num() != Right.ExportedEntryCells.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < Left.PlannedCells.Num(); ++Index)
		{
			if (Left.PlannedCells[Index].Cell != Right.PlannedCells[Index].Cell
				|| Left.PlannedCells[Index].Intent != Right.PlannedCells[Index].Intent)
			{
				return false;
			}
		}

		for (int32 Index = 0; Index < Left.Placements.Num(); ++Index)
		{
			if (Left.Placements[Index].Cell != Right.Placements[Index].Cell
				|| Left.Placements[Index].Intent != Right.Placements[Index].Intent
				|| Left.Placements[Index].Module != Right.Placements[Index].Module
				|| Left.Placements[Index].YawRotationSteps != Right.Placements[Index].YawRotationSteps)
			{
				return false;
			}
		}

		for (int32 Index = 0; Index < Left.ExportedEntryCells.Num(); ++Index)
		{
			if (Left.ExportedEntryCells[Index] != Right.ExportedEntryCells[Index])
			{
				return false;
			}
		}

		return true;
	}

	bool SolveResultsHaveMatchingPlacements(const FLayoutSolveResult& Left, const FLayoutSolveResult& Right)
	{
		return SolveResultsHaveMatchingPlacementsInternal(Left, Right, true);
	}

	bool SolveResultsHaveMatchingPlacementsIgnoringSeed(const FLayoutSolveResult& Left, const FLayoutSolveResult& Right)
	{
		return SolveResultsHaveMatchingPlacementsInternal(Left, Right, false);
	}

	bool SolveResultsHaveMatchingPlacementsIgnoringYaw(
		const FLayoutSolveResult& Left,
		const FLayoutSolveResult& Right)
	{
		if (Left.bSucceeded != Right.bSucceeded
			|| Left.FootprintSize != Right.FootprintSize
			|| Left.PlannedCells.Num() != Right.PlannedCells.Num()
			|| Left.Placements.Num() != Right.Placements.Num()
			|| Left.ExportedEntryCells.Num() != Right.ExportedEntryCells.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < Left.PlannedCells.Num(); ++Index)
		{
			if (Left.PlannedCells[Index].Cell != Right.PlannedCells[Index].Cell
				|| Left.PlannedCells[Index].Intent != Right.PlannedCells[Index].Intent)
			{
				return false;
			}
		}

		for (int32 Index = 0; Index < Left.Placements.Num(); ++Index)
		{
			if (Left.Placements[Index].Cell != Right.Placements[Index].Cell
				|| Left.Placements[Index].Intent != Right.Placements[Index].Intent
				|| Left.Placements[Index].Module != Right.Placements[Index].Module)
			{
				return false;
			}
		}

		for (int32 Index = 0; Index < Left.ExportedEntryCells.Num(); ++Index)
		{
			if (Left.ExportedEntryCells[Index] != Right.ExportedEntryCells[Index])
			{
				return false;
			}
		}

		return true;
	}

	int32 CountIndexedDomainBits(const TArray<uint64>& Bits)
	{
		int32 Count = 0;
		for (uint64 Word : Bits)
		{
			while (Word != 0)
			{
				Word &= Word - 1;
				++Count;
			}
		}

		return Count;
	}

	bool IsIndexedCandidateBitSet(const TArray<uint64>& Bits, const int32 CandidateIndex)
	{
		if (CandidateIndex < 0)
		{
			return false;
		}

		const int32 WordIndex = CandidateIndex / 64;
		const int32 BitIndex = CandidateIndex % 64;
		return Bits.IsValidIndex(WordIndex) && ((Bits[WordIndex] & (uint64(1) << BitIndex)) != 0);
	}

	const FLayoutIndexedCandidateCompatibility* FindCompatibilityRow(
		const FLayoutIndexedDomainSnapshot& Snapshot,
		const int32 CandidateIndex,
		const ELayoutFaceDirection Direction)
	{
		return Snapshot.CompatibilityRows.FindByPredicate([CandidateIndex, Direction](const FLayoutIndexedCandidateCompatibility& Row)
		{
			return Row.SourceCandidateIndex == CandidateIndex && Row.Direction == Direction;
		});
	}









}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverDeterministicVerticalColumnTest,
	"PorismExtension.Layout.Solver.Profile.DeterministicVerticalColumn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverDeterministicVerticalColumnTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_DeterministicVertical"), FIntPoint(1, 1), FIntPoint(1, 1), 2, 1, true);

	const FLayoutSolveResult FirstResult = FLayoutProfileSolver::Solve(Profile, 11);
	const FLayoutSolveResult SecondResult = FLayoutProfileSolver::Solve(Profile, 11);

	if (!FirstResult.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("First deterministic vertical solve failed: %s"), *FirstResult.FailureReason));
	}

	if (!SecondResult.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Second deterministic vertical solve failed: %s"), *SecondResult.FailureReason));
	}

	TestTrue(TEXT("Deterministic vertical layout solves successfully"), FirstResult.bSucceeded);
	TestTrue(TEXT("Repeated solve with the same seed also succeeds"), SecondResult.bSucceeded);
	TestEqual(TEXT("Solved placement count matches the expected stacked footprint"), FirstResult.Placements.Num(), 2);
	TestEqual(TEXT("Repeated solve produces the same placement count"), SecondResult.Placements.Num(), FirstResult.Placements.Num());

	bool bFoundEntry = false;
	bool bFoundBoundary = false;
	for (int32 Index = 0; Index < FirstResult.Placements.Num(); ++Index)
	{
		const FLayoutPlacedModule& FirstPlacement = FirstResult.Placements[Index];
		const FLayoutPlacedModule& SecondPlacement = SecondResult.Placements[Index];

		TestEqual(TEXT("Placement cell is deterministic"), SecondPlacement.Cell, FirstPlacement.Cell);
		TestEqual(TEXT("Placement intent is deterministic"), SecondPlacement.Intent, FirstPlacement.Intent);
		TestEqual(TEXT("Placement module is deterministic"), SecondPlacement.Module, FirstPlacement.Module);
		TestEqual(TEXT("Placement yaw rotation is deterministic"), SecondPlacement.YawRotationSteps, FirstPlacement.YawRotationSteps);

		bFoundEntry |= FirstPlacement.Intent == ELayoutCellIntent::Entry;
		bFoundBoundary |= FirstPlacement.Intent == ELayoutCellIntent::Boundary;
	}

	TestTrue(TEXT("Solved layout includes one entry cell"), bFoundEntry);
	TestTrue(TEXT("Solved layout includes one upper boundary cell"), bFoundBoundary);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverSingleValidStateKeepsPlacementsAcrossSeedChoiceTest,
	"PorismExtension.Layout.Solver.Profile.SingleValidStateKeepsPlacementsAcrossSeedChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverSingleValidStateKeepsPlacementsAcrossSeedChoiceTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(GetTransientPackage(), TEXT("LayoutProfile_SingleValidState"), FIntPoint(1, 1), FIntPoint(1, 1), 2, 1, true);
	TestEqual(TEXT("Single-state harness uses profile-owned generic, Entry, and vertical-access entries"), Profile->ContentSet->Entries.Num(), 3);

	constexpr int32 ValidationSeedCount = 8;
	const FLayoutSolveResult BaselineResult = FLayoutProfileSolver::Solve(Profile, 0);
	TestTrue(TEXT("Baseline single-state solve succeeds"), BaselineResult.bSucceeded);

	for (int32 Seed = 1; Seed < ValidationSeedCount; ++Seed)
	{
		const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, Seed);
		TestTrue(FString::Printf(TEXT("Single-state solve succeeds for validation seed %d"), Seed), Result.bSucceeded);
		TestTrue(
			FString::Printf(TEXT("Single-state solve keeps the same placement set for validation seed %d"), Seed),
			SolveResultsHaveMatchingPlacementsIgnoringYaw(BaselineResult, Result));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverBuildsImmutableModuleAndProfileSnapshotsTest,
	"PorismExtension.Layout.Solver.Profile.BuildsImmutableModuleAndProfileSnapshots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverBuildsImmutableModuleAndProfileSnapshotsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("LayoutProfile_Snapshot"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		1,
		false);
	ULayoutModuleAsset* SourceModule = Profile->ContentSet->Entries[0].ModuleSettings.Module;
	const TArray<ELayoutCellIntent> ExpectedIntents = SourceModule->GetEffectiveSupportedCellIntents();
	const TArray<int32> ExpectedYawSteps = SourceModule->GetEffectiveYawRotationSteps();
	const FIntVector ExpectedCellSize = SourceModule->GetEffectiveCellSizeInBlocks();

	const FLayoutModuleCatalog ModuleCatalog = FLayoutProfileSolver::BuildModuleCatalog(Profile->ContentSet);
	const FLayoutProfileSolveSnapshot ProfileSnapshot = FLayoutProfileSolver::BuildProfileSnapshot(Profile);

	TestTrue(TEXT("Module-set snapshot validation succeeds"), ModuleCatalog.Validation.IsValid());
	TestEqual(TEXT("Module-set snapshot copies the generic, entry, and vertical-access modules"), ModuleCatalog.Modules.Num(), 3);
	if (ModuleCatalog.Modules.Num() != 3)
	{
		return false;
	}
	TestEqual(TEXT("Module snapshot copies role-derived supported cell intents"), ModuleCatalog.Modules[0].SupportedCellIntents, ExpectedIntents);
	TestTrue(TEXT("Module snapshot supports copied boundary intent"), ModuleCatalog.Modules[0].SupportsIntent(ELayoutCellIntent::Boundary));
	TestEqual(TEXT("Module snapshot copies effective yaw steps"), ModuleCatalog.Modules[0].AllowedYawRotationSteps, ExpectedYawSteps);
	TestEqual(TEXT("Module snapshot copies cell size"), ModuleCatalog.Modules[0].CellSizeInBlocks, ExpectedCellSize);

	TestTrue(TEXT("Profile snapshot validation succeeds"), ProfileSnapshot.Validation.IsValid());
	TestEqual(TEXT("Profile snapshot copies minimum footprint"), ProfileSnapshot.MinimumFootprintInCells, Profile->MinimumFootprintInCells);

	SourceModule->Roles.Reset();
	SetModuleTemplateSize(SourceModule, FIntVector(32, 32, 32));
	Profile->MinimumFootprintInCells = FIntPoint(9, 9);
	TestTrue(TEXT("Module snapshot remains independent after source role mutation"), ModuleCatalog.Modules[0].SupportsIntent(ELayoutCellIntent::Boundary));
	TestEqual(TEXT("Module snapshot keeps original cell size after source mutation"), ModuleCatalog.Modules[0].CellSizeInBlocks, ExpectedCellSize);
	TestEqual(TEXT("Profile snapshot remains independent after source mutation"), ProfileSnapshot.MinimumFootprintInCells, FIntPoint(2, 2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverReportsPropagationStatsTest,
	"PorismExtension.Layout.Solver.Profile.ReportsPropagationStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverReportsPropagationStatsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_PropagationStats"), FIntPoint(3, 2), FIntPoint(3, 2), 1, 1, false);

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 93);

	TestTrue(TEXT("Stats test solve succeeds"), Result.bSucceeded);
	TestTrue(TEXT("Propagation run count is recorded"), Result.PropagationStats.PropagationRunCount > 0);
	TestTrue(TEXT("Propagation pass count is recorded"), Result.PropagationStats.PropagationPassCount >= Result.PropagationStats.PropagationRunCount);
	TestTrue(TEXT("Propagation queue pops are recorded"), Result.PropagationStats.ArcQueuePopCount > 0);
	TestTrue(TEXT("Propagation support checks are recorded"), Result.PropagationStats.SupportCheckCount > 0);
	TestTrue(TEXT("Propagation time is non-negative"), Result.PropagationStats.PropagationSeconds >= 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverArcConsistencyIsDeterministicForSeedsTest,
	"PorismExtension.Layout.Solver.Profile.ArcConsistencyIsDeterministicForSeeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverArcConsistencyIsDeterministicForSeedsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_ArcDeterminism"), FIntPoint(3, 2), FIntPoint(3, 2), 1, 1, false);

	for (const int32 Seed : {11, 17, 23})
	{
		const FLayoutSolveResult FirstResult = FLayoutProfileSolver::Solve(Profile, Seed);
		const FLayoutSolveResult SecondResult = FLayoutProfileSolver::Solve(Profile, Seed);

		TestTrue(FString::Printf(TEXT("First arc-consistency solve succeeds for seed %d"), Seed), FirstResult.bSucceeded);
		TestTrue(FString::Printf(TEXT("Second arc-consistency solve succeeds for seed %d"), Seed), SecondResult.bSucceeded);
		TestTrue(FString::Printf(TEXT("Arc-consistency solve is deterministic for seed %d"), Seed), SolveResultsHaveMatchingPlacements(FirstResult, SecondResult));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverPropagationStressStatsTest,
	"PorismExtension.Layout.Solver.Profile.PropagationStressStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverPropagationStressStatsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_PropagationStress"), FIntPoint(5, 5), FIntPoint(5, 5), 1, 0, false);
	UObject* Outer = CreateLayoutProfileSolverTestOuter(TEXT("LayoutContentSet_PropagationStress"));
	const auto OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
	const auto SolidTags = MakeTags({LayoutGameplayTags::FaceSolid});
	const TArray<ELayoutCellIntent> AllStressIntents = {ELayoutCellIntent::Boundary, ELayoutCellIntent::Entry, ELayoutCellIntent::Core, ELayoutCellIntent::Interior};
	const TArray<ELayoutCellIntent> BoundaryStressIntents = {ELayoutCellIntent::Boundary, ELayoutCellIntent::Entry};
	const TArray<ELayoutCellIntent> InteriorStressIntents = {ELayoutCellIntent::Core, ELayoutCellIntent::Interior};
	TArray<FLayoutRegionContentEntry> Entries;
	// Preserve the six authored propagation patterns and weights from the removed module-set fixture.
	const auto AddPattern = [&](const TCHAR* Name, const TArray<ELayoutCellIntent>& Intents,
		const TArray<FLayoutFaceRule>& Faces, const int32 Weight)
	{
		FLayoutRegionContentEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.EntryId = FName(Name);
		Entry.ContentKind = ELayoutRegionContentKind::Module;
		Entry.Weight = Weight;
		Entry.ModuleSettings.Module = CreateModule(Outer, Name,
			CreateTemplate(Outer, *FString::Printf(TEXT("LayoutTemplate_%s"), Name), FIntVector(16, 16, 16)),
			Intents, Faces);
	};
	AddPattern(TEXT("LayoutModule_StressOpenRoom"), AllStressIntents, BuildStressPatternFaces(OpenTags, OpenTags, OpenTags, OpenTags), 8);
	AddPattern(TEXT("LayoutModule_StressSolidRoom"), BoundaryStressIntents, BuildStressPatternFaces(SolidTags, SolidTags, SolidTags, SolidTags), 5);
	AddPattern(TEXT("LayoutModule_StressHallX"), InteriorStressIntents, BuildStressPatternFaces(OpenTags, OpenTags, SolidTags, SolidTags), 13);
	AddPattern(TEXT("LayoutModule_StressHallY"), InteriorStressIntents, BuildStressPatternFaces(SolidTags, SolidTags, OpenTags, OpenTags), 11);
	AddPattern(TEXT("LayoutModule_StressCorner"), AllStressIntents, BuildStressPatternFaces(OpenTags, SolidTags, OpenTags, SolidTags), 7);
	AddPattern(TEXT("LayoutModule_StressInverseCorner"), AllStressIntents, BuildStressPatternFaces(SolidTags, OpenTags, SolidTags, OpenTags), 9);
	Profile->ContentSet = CreateRegionContentSet(Outer, TEXT("PropagationStressContentSet"), Entries);
	const FLayoutSolverExecutionSettings ExecutionSettings = MakeSolverExecutionSettings(250000, 10.0f);

	constexpr int32 SeedCount = 24;
	int32 SucceededCount = 0;
	double TotalSolveSeconds = 0.0;
	double TotalPropagationSeconds = 0.0;
	double MaxPropagationSeconds = 0.0;
	int64 TotalArcQueuePops = 0;
	int64 MaxArcQueuePops = 0;
	int64 TotalSupportChecks = 0;
	int64 MaxSupportChecks = 0;
	int64 TotalCandidateRemovals = 0;
	int64 MaxCandidateRemovals = 0;
	int64 TotalBacktracks = 0;
	int64 MaxBacktracks = 0;

	for (int32 SeedOffset = 0; SeedOffset < SeedCount; ++SeedOffset)
	{
		const int32 Seed = 1000 + SeedOffset;
		const double SolveStartSeconds = FPlatformTime::Seconds();
		const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, Seed, ExecutionSettings);
		const double SolveSeconds = FPlatformTime::Seconds() - SolveStartSeconds;
		TotalSolveSeconds += SolveSeconds;

		if (!Result.bSucceeded)
		{
			AddError(FString::Printf(TEXT("Propagation stress solve failed for seed %d: %s"), Seed, *Result.FailureReason));
			continue;
		}

		++SucceededCount;
		TotalPropagationSeconds += Result.PropagationStats.PropagationSeconds;
		MaxPropagationSeconds = FMath::Max(MaxPropagationSeconds, Result.PropagationStats.PropagationSeconds);
		TotalArcQueuePops += Result.PropagationStats.ArcQueuePopCount;
		MaxArcQueuePops = FMath::Max<int64>(MaxArcQueuePops, Result.PropagationStats.ArcQueuePopCount);
		TotalSupportChecks += Result.PropagationStats.SupportCheckCount;
		MaxSupportChecks = FMath::Max<int64>(MaxSupportChecks, Result.PropagationStats.SupportCheckCount);
		TotalCandidateRemovals += Result.PropagationStats.CandidateRemovalCount;
		MaxCandidateRemovals = FMath::Max<int64>(MaxCandidateRemovals, Result.PropagationStats.CandidateRemovalCount);
		TotalBacktracks += Result.PropagationStats.BacktrackCount;
		MaxBacktracks = FMath::Max<int64>(MaxBacktracks, Result.PropagationStats.BacktrackCount);
	}

	const double Divisor = FMath::Max(1, SucceededCount);
	AddInfo(FString::Printf(
		TEXT("Propagation stress stats: seeds=%d succeeded=%d avgSolveMs=%.3f avgPropagationMs=%.3f maxPropagationMs=%.3f avgArcPops=%.1f maxArcPops=%lld avgSupportChecks=%.1f maxSupportChecks=%lld avgRemovals=%.1f maxRemovals=%lld avgBacktracks=%.1f maxBacktracks=%lld"),
		SeedCount,
		SucceededCount,
		(TotalSolveSeconds / static_cast<double>(SeedCount)) * 1000.0,
		(TotalPropagationSeconds / Divisor) * 1000.0,
		MaxPropagationSeconds * 1000.0,
		static_cast<double>(TotalArcQueuePops) / Divisor,
		MaxArcQueuePops,
		static_cast<double>(TotalSupportChecks) / Divisor,
		MaxSupportChecks,
		static_cast<double>(TotalCandidateRemovals) / Divisor,
		MaxCandidateRemovals,
		static_cast<double>(TotalBacktracks) / Divisor,
		MaxBacktracks));

	TestEqual(TEXT("Every propagation stress seed solves"), SucceededCount, SeedCount);
	TestTrue(TEXT("Propagation stress records queue pops"), TotalArcQueuePops > 0);
	TestTrue(TEXT("Propagation stress records support checks"), TotalSupportChecks > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverPropagationStressCancellationTest,
	"PorismExtension.Layout.Solver.Profile.PropagationStressCancellationStopsLongBranchLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverRoutedTraversalCancellationTest,
	"PorismExtension.Layout.Solver.Profile.RoutedTraversalCancellationStopsBacktrackingLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverRoutedTraversalStressConnectsEntryToStairTest,
	"PorismExtension.Layout.Solver.Profile.RoutedTraversalStressConnectsEntryToStair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverPropagationStressCancellationTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FThreadSafeBool, ESPMode::ThreadSafe> Started = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
	const TSharedRef<FThreadSafeBool, ESPMode::ThreadSafe> Finished = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
	const TSharedRef<FLayoutSolveResult, ESPMode::ThreadSafe> SharedResult = MakeShared<FLayoutSolveResult, ESPMode::ThreadSafe>();
	const TSharedRef<FThreadSafeBool, ESPMode::ThreadSafe> CancellationGate = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
	const TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> CheckpointCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	FLayoutProfileSolver::SetCancellationCheckpointGateForTesting(CancellationGate);
	FLayoutProfileSolver::SetCancellationCheckpointCounterForTesting(CheckpointCounter);

	UE::Tasks::FTask Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Started, Finished, SharedResult]()
	{
		*Started = true;
		SharedResult->bSucceeded = FLayoutProfileSolver::RunCancellationCheckpointProbeForTesting(512);
		*Finished = true;
	});

	TestTrue(TEXT("Propagation-stress checkpoint test observes worker start"), WaitForCancelableSolveStart(Started));
	TestTrue(TEXT("Propagation-stress checkpoint test reaches a gated solver checkpoint"), WaitForCancellationCheckpointHit(CheckpointCounter));
	TestFalse(TEXT("Propagation-stress checkpoint test keeps the worker blocked while the gate stays closed"), static_cast<bool>(*Finished));
	*CancellationGate = true;
	TestTrue(TEXT("Propagation-stress checkpoint test finishes after the gate opens"), WaitForCancelableSolveFinish(Finished));
	Task.Wait();
	FLayoutProfileSolver::SetCancellationCheckpointGateForTesting(nullptr);
	FLayoutProfileSolver::SetCancellationCheckpointCounterForTesting(nullptr);

	TestTrue(TEXT("Propagation-stress checkpoint probe succeeds once the gate releases"), SharedResult->bSucceeded);
	return true;
}

bool FLayoutProfileSolverRoutedTraversalCancellationTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FThreadSafeBool, ESPMode::ThreadSafe> Started = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
	const TSharedRef<FThreadSafeBool, ESPMode::ThreadSafe> Finished = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
	const TSharedRef<FLayoutSolveResult, ESPMode::ThreadSafe> SharedResult = MakeShared<FLayoutSolveResult, ESPMode::ThreadSafe>();
	const TSharedRef<FThreadSafeBool, ESPMode::ThreadSafe> CancellationGate = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
	const TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> CheckpointCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	FLayoutProfileSolver::SetCancellationCheckpointGateForTesting(CancellationGate);
	FLayoutProfileSolver::SetCancellationCheckpointCounterForTesting(CheckpointCounter);

	UE::Tasks::FTask Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Started, Finished, SharedResult]()
	{
		*Started = true;
		SharedResult->bSucceeded = FLayoutProfileSolver::RunCancellationCheckpointProbeForTesting(1024);
		*Finished = true;
	});

	TestTrue(TEXT("Routed-traversal checkpoint test observes worker start"), WaitForCancelableSolveStart(Started));
	TestTrue(TEXT("Routed-traversal checkpoint test reaches a gated solver checkpoint"), WaitForCancellationCheckpointHit(CheckpointCounter));
	TestFalse(TEXT("Routed-traversal checkpoint test keeps the worker blocked while the gate stays closed"), static_cast<bool>(*Finished));
	*CancellationGate = true;
	TestTrue(TEXT("Routed-traversal checkpoint test finishes after the gate opens"), WaitForCancelableSolveFinish(Finished));
	Task.Wait();
	FLayoutProfileSolver::SetCancellationCheckpointGateForTesting(nullptr);
	FLayoutProfileSolver::SetCancellationCheckpointCounterForTesting(nullptr);

	TestTrue(TEXT("Routed-traversal checkpoint probe succeeds once the gate releases"), SharedResult->bSucceeded);
	return true;
}

bool FLayoutProfileSolverRoutedTraversalStressConnectsEntryToStairTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_RoutedTraversalStress"), FIntPoint(5, 5), FIntPoint(5, 5), 1, 1, false);
	const FLayoutSolverExecutionSettings ExecutionSettings = MakeSolverExecutionSettings(250000, 10.0f);

	TArray<FLayoutPlannedCell> PlannedCells;
	for (int32 Y = 0; Y < 5; ++Y)
	{
		for (int32 X = 0; X < 5; ++X)
		{
			FLayoutPlannedCell& PlannedCell = PlannedCells.AddDefaulted_GetRef();
			PlannedCell.Cell = FIntVector(X, Y, 0);
			if (X == 2 && Y == 0)
			{
				PlannedCell.Intent = ELayoutCellIntent::Entry;
			}
			else if (X == 3 && Y == 3)
			{
				PlannedCell.Intent = ELayoutCellIntent::VerticalAccess;
			}
			else if (X == 2 && Y == 2)
			{
				PlannedCell.Intent = ELayoutCellIntent::Core;
			}
			else if (X == 0 || X == 4 || Y == 0 || Y == 4)
			{
				PlannedCell.Intent = ELayoutCellIntent::Boundary;
			}
			else
			{
				PlannedCell.Intent = ELayoutCellIntent::Interior;
			}
		}
	}

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 4242, FIntPoint(5, 5), PlannedCells, ExecutionSettings);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Routed traversal stress solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Representative routed traversal layout solves"), Result.bSucceeded);
	TestTrue(TEXT("Routed traversal stress should avoid runaway backtracking"), Result.PropagationStats.BacktrackCount < 1000);

	const FName DeadEndName(TEXT("LayoutModule_RouteStressDeadEnd"));
	const TSet<FIntVector> RequiredRouteCorridor = {
		FIntVector(2, 1, 0),
		FIntVector(2, 2, 0),
		FIntVector(2, 3, 0),
		FIntVector(3, 2, 0)
	};
	for (const FLayoutPlacedModule& Placement : Result.Placements)
	{
		if (RequiredRouteCorridor.Contains(Placement.Cell))
		{
			TestNotEqual(
				FString::Printf(TEXT("Route corridor cell %s rejects attractive dead-end module"), *Placement.Cell.ToString()),
				Placement.Module != nullptr ? Placement.Module->GetFName() : NAME_None,
				DeadEndName);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverAllowsStructuredInteriorFillOffRequiredRoutesTest,
	"PorismExtension.Layout.Solver.Profile.AllowsStructuredInteriorFillOffRequiredRoutes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverAllowsStructuredInteriorFillOffRequiredRoutesTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_StructuredInteriorFill"), FIntPoint(5, 5), FIntPoint(5, 5), 1, 1, false);
	Profile->bRequireAllTraversalChannelsReachable = true;
	const FLayoutSolverExecutionSettings ExecutionSettings = MakeSolverExecutionSettings(250000, 10.0f);

	TArray<FLayoutPlannedCell> PlannedCells;
	for (int32 Y = 0; Y < 5; ++Y)
	{
		for (int32 X = 0; X < 5; ++X)
		{
			FLayoutPlannedCell& PlannedCell = PlannedCells.AddDefaulted_GetRef();
			PlannedCell.Cell = FIntVector(X, Y, 0);
			if (X == 2 && Y == 0)
			{
				PlannedCell.Intent = ELayoutCellIntent::Entry;
			}
			else if (X == 3 && Y == 3)
			{
				PlannedCell.Intent = ELayoutCellIntent::VerticalAccess;
			}
			else if (X == 2 && Y == 2)
			{
				PlannedCell.Intent = ELayoutCellIntent::Core;
			}
			else if (X == 0 || X == 4 || Y == 0 || Y == 4)
			{
				PlannedCell.Intent = ELayoutCellIntent::Boundary;
			}
			else
			{
				PlannedCell.Intent = ELayoutCellIntent::Interior;
			}
		}
	}

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 5151, FIntPoint(5, 5), PlannedCells, ExecutionSettings);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Structured interior fill solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Structured interior fill layout solves"), Result.bSucceeded);

	const TSet<FIntVector> RequiredRouteCorridor = {
		FIntVector(2, 1, 0),
		FIntVector(2, 2, 0),
		FIntVector(2, 3, 0),
		FIntVector(3, 2, 0)
	};
	const TSet<FName> StructuredFillNames = {
		FName(TEXT("LayoutModule_RouteStressHallway")),
		FName(TEXT("LayoutModule_RouteStressTJunction")),
		FName(TEXT("LayoutModule_RouteStressWallFloor"))
	};

	bool bFoundStructuredOffRouteInterior = false;
	for (const FLayoutPlacedModule& Placement : Result.Placements)
	{
		if (Placement.Module == nullptr)
		{
			continue;
		}

		if (!RequiredRouteCorridor.Contains(Placement.Cell) && StructuredFillNames.Contains(Placement.Module->GetFName()))
		{
			bFoundStructuredOffRouteInterior = true;
		}
	}

	TestTrue(TEXT("Non-route reachable interior fill can use wall/hall/junction modules instead of always maximizing open traversal faces"), bFoundStructuredOffRouteInterior);
	if (!bFoundStructuredOffRouteInterior)
	{
		TMap<FName, int32> PlacementCounts;
		for (const FLayoutPlacedModule& Placement : Result.Placements)
		{
			if (Placement.Module != nullptr)
			{
				++PlacementCounts.FindOrAdd(Placement.Module->GetFName());
			}
		}
		for (const TPair<FName, int32>& PlacementCount : PlacementCounts)
		{
			AddInfo(FString::Printf(TEXT("Structured fill placement count: %s=%d"), *PlacementCount.Key.ToString(), PlacementCount.Value));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverRoutesAcrossTraversalChannelBridgeTest,
	"PorismExtension.Layout.Solver.Profile.RoutesAcrossTraversalChannelBridge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverRoutesAcrossTraversalChannelBridgeTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_ChannelBridge"), FIntPoint(3, 1), FIntPoint(3, 1), 1, 0, false);
	Profile->bRequireAllTraversalChannelsReachable = true;
	const FLayoutSolverExecutionSettings ExecutionSettings = MakeSolverExecutionSettings(250000, 10.0f);

	TArray<FLayoutPlannedCell> PlannedCells;
	FLayoutPlannedCell& EntryCell = PlannedCells.AddDefaulted_GetRef();
	EntryCell.Cell = FIntVector(0, 0, 0);
	EntryCell.Intent = ELayoutCellIntent::Entry;
	FLayoutPlannedCell& BridgeCell = PlannedCells.AddDefaulted_GetRef();
	BridgeCell.Cell = FIntVector(1, 0, 0);
	BridgeCell.Intent = ELayoutCellIntent::Interior;
	FLayoutPlannedCell& SecondaryAccessCell = PlannedCells.AddDefaulted_GetRef();
	SecondaryAccessCell.Cell = FIntVector(2, 0, 0);
	SecondaryAccessCell.Intent = ELayoutCellIntent::VerticalAccess;

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 6262, FIntPoint(3, 1), PlannedCells, ExecutionSettings);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Traversal channel bridge solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Route prepass can connect Primary entry traversal to a Secondary access endpoint"), Result.bSucceeded);
	TestTrue(TEXT("Junction cell uses the module that bridges Primary and Secondary through InternalAccessLinks"), Result.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell == FIntVector(1, 0, 0)
			&& Placement.Module != nullptr
			&& Placement.Module->GetFName() == FLayoutId(TEXT("LayoutModule_ChannelBridgeJunction"));
	}));
	TestFalse(TEXT("Higher-weight unlinked transition candidate is rejected because it cannot bridge route traversal channels"), Result.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell == FIntVector(1, 0, 0)
			&& Placement.Module != nullptr
			&& Placement.Module->GetFName() == FLayoutId(TEXT("LayoutModule_ChannelBridgeUnlinked"));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverRoutedThreeLevelProfilesRemainBoundedTest,
	"PorismExtension.Layout.Solver.Profile.RoutedThreeLevelProfilesRemainBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverRoutedThreeLevelProfilesRemainBoundedTest::RunTest(const FString& Parameters)
{
	struct FThreeLevelCase
	{
		const TCHAR* Name = TEXT("");
		FIntPoint MinimumFootprint = FIntPoint::ZeroValue;
		FIntPoint MaximumFootprint = FIntPoint::ZeroValue;
		int32 Seed = 0;
		bool bUpperBoundaryOnly = false;
		bool bAllowEmptyInterior = false;
		int32 MaxEmptyCellCount = 0;
		int32 MaxExpectedBacktracks = 0;
	};

	const TArray<FThreeLevelCase> Cases = {
		// Keep the default suite focused on functional three-level routing
		// coverage. Larger perf sweeps belong in dedicated profiling passes.
		{TEXT("5x5Full"), FIntPoint(5, 5), FIntPoint(5, 5), 201, false, false, 0, 7500},
		{TEXT("6x6Full"), FIntPoint(6, 6), FIntPoint(6, 6), 202, false, false, 0, 8000},
		{TEXT("6x6UpperBoundaryOnly"), FIntPoint(6, 6), FIntPoint(6, 6), 203, true, false, 0, 8000},
		{TEXT("6x6EmptyInterior"), FIntPoint(6, 6), FIntPoint(6, 6), 204, false, true, 8, 8000},
		{TEXT("Variable5To6EmptyInterior"), FIntPoint(5, 5), FIntPoint(6, 6), 205, false, true, 10, 9000}
	};

	for (const FThreeLevelCase& TestCase : Cases)
	{
		UObject* Outer = CreateLayoutProfileSolverTestOuter(*FString::Printf(TEXT("LayoutProfile_RoutedThreeLevel_%s"), TestCase.Name));
		ULayoutProfileAsset* Profile = CreateProfile(
			Outer,
			*FString::Printf(TEXT("LayoutProfile_RoutedThreeLevel_%s"), TestCase.Name),
			TestCase.MinimumFootprint,
			TestCase.MaximumFootprint,
			3,
			1,
			TestCase.bUpperBoundaryOnly,
			TestCase.bAllowEmptyInterior,
			TestCase.MaxEmptyCellCount);
		Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
		Profile->VerticalAccessCount = 1;
		Profile->MinVerticalAccessCount = 1;
		Profile->MaxVerticalAccessCount = 1;
		Profile->bRequireAllTraversalChannelsReachable = true;

		const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(
			Profile,
			TestCase.Seed,
			MakeSolverExecutionSettings(500000, 10.0f));
		if (!Result.bSucceeded)
		{
			AddInfo(FString::Printf(TEXT("Routed three-level case '%s' failed: %s"), TestCase.Name, *Result.FailureReason));
		}

		TestTrue(FString::Printf(TEXT("Routed three-level case '%s' solves"), TestCase.Name), Result.bSucceeded);
		TestEqual(FString::Printf(TEXT("Routed three-level case '%s' keeps expected level count"), TestCase.Name), Profile->LevelCount, 3);
		TestTrue(
			FString::Printf(TEXT("Routed three-level case '%s' keeps backtracking bounded. Backtracks=%d"), TestCase.Name, Result.PropagationStats.BacktrackCount),
			Result.PropagationStats.BacktrackCount <= TestCase.MaxExpectedBacktracks);

		if (Result.bSucceeded)
		{
			TSet<int32> SolvedLevels;
			int32 VerticalAccessPlacementCount = 0;
			for (const FLayoutPlacedModule& Placement : Result.Placements)
			{
				SolvedLevels.Add(Placement.Cell.Z);
				if (Placement.Intent == ELayoutCellIntent::VerticalAccess)
				{
					++VerticalAccessPlacementCount;
				}
			}

			TestTrue(FString::Printf(TEXT("Routed three-level case '%s' places modules on all levels"), TestCase.Name), SolvedLevels.Num() == 3);
			TestTrue(FString::Printf(TEXT("Routed three-level case '%s' places at least one vertical-access transition"), TestCase.Name), VerticalAccessPlacementCount >= 2);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverRoutedEmptyInteriorKeepsRequiredPathFilledTest,
	"PorismExtension.Layout.Solver.Profile.RoutedEmptyInteriorKeepsRequiredPathFilled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverRoutedEmptyInteriorKeepsRequiredPathFilledTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_RoutedEmptyInteriorPath"), FIntPoint(5, 5), FIntPoint(5, 5), 1, 1, false, true, 8);
	Profile->bRequireAllTraversalChannelsReachable = true;
	const FLayoutSolverExecutionSettings ExecutionSettings = MakeSolverExecutionSettings(250000, 10.0f);

	TArray<FLayoutPlannedCell> PlannedCells;
	for (int32 Y = 0; Y < 5; ++Y)
	{
		for (int32 X = 0; X < 5; ++X)
		{
			FLayoutPlannedCell& PlannedCell = PlannedCells.AddDefaulted_GetRef();
			PlannedCell.Cell = FIntVector(X, Y, 0);
			if (X == 2 && Y == 0)
			{
				PlannedCell.Intent = ELayoutCellIntent::Entry;
			}
			else if (X == 2 && Y == 3)
			{
				PlannedCell.Intent = ELayoutCellIntent::VerticalAccess;
			}
			else if (X == 2 && Y == 2)
			{
				PlannedCell.Intent = ELayoutCellIntent::Core;
			}
			else if (X == 0 || X == 4 || Y == 0 || Y == 4)
			{
				PlannedCell.Intent = ELayoutCellIntent::Boundary;
			}
			else
			{
				PlannedCell.Intent = ELayoutCellIntent::Interior;
			}
		}
	}

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 303, FIntPoint(5, 5), PlannedCells, ExecutionSettings);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Routed empty interior solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Routed empty-interior layout solves"), Result.bSucceeded);

	const TSet<FIntVector> RequiredRouteCells = {
		FIntVector(2, 1, 0),
		FIntVector(2, 2, 0),
		FIntVector(2, 3, 0)
	};
	for (const FIntVector& RequiredCell : RequiredRouteCells)
	{
		const bool bHasPlacement = Result.Placements.ContainsByPredicate([RequiredCell](const FLayoutPlacedModule& Placement)
		{
			return Placement.Cell == RequiredCell && Placement.Module != nullptr;
		});
		TestTrue(FString::Printf(TEXT("Required route cell %s remains filled even when empty interior is allowed"), *RequiredCell.ToString()), bHasPlacement);
	}

	TestTrue(TEXT("Empty interior route solve keeps backtracking bounded"), Result.PropagationStats.BacktrackCount < 1000);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverStandaloneRegionRequestMatchesDirectSolveTest,
	"PorismExtension.Layout.Solver.Profile.StandaloneRegionRequestMatchesDirectSolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverStandaloneRegionRequestMatchesDirectSolveTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_RequestParity"), FIntPoint(2, 2), FIntPoint(2, 2), 1, 1, false);

	const FLayoutSolveResult DirectResult = FLayoutProfileSolver::Solve(Profile, 91);
	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 91, TEXT("Test/Standalone"));
	const FLayoutRegionSolveResult RegionResult = FLayoutProfileSolver::SolveRegion(Request);

	if (!DirectResult.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Direct solve failure: %s"), *DirectResult.FailureReason));
		for (const FLayoutValidationMessage& Message : DirectResult.Messages)
		{
			AddInfo(FString::Printf(TEXT("Direct solve message: %s"), *Message.Message));
		}
	}
	if (!RegionResult.SolveResult.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Standalone request failure: %s"), *RegionResult.SolveResult.FailureReason));
		for (const FLayoutValidationMessage& Message : RegionResult.SolveResult.Messages)
		{
			AddInfo(FString::Printf(TEXT("Standalone request message: %s"), *Message.Message));
		}
	}

	TestEqual(TEXT("Region result preserves debug path"), RegionResult.RegionDebugPath, FString(TEXT("Test/Standalone")));
	TestTrue(TEXT("Direct solve succeeds"), DirectResult.bSucceeded);
	TestTrue(TEXT("Region request solve succeeds"), RegionResult.SolveResult.bSucceeded);
	TestTrue(TEXT("Region request solve uses request snapshots after source module mutation"), SolveResultsHaveMatchingPlacements(DirectResult, RegionResult.SolveResult));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverSolvesRoleOnlyModulesTest,
	"PorismExtension.Layout.Solver.Profile.SolvesRoleOnlyModules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverSolvesRoleOnlyModulesTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutTemplate_RoleOnly"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("LayoutModule_RoleOnly"),
		Template,
		{},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceSolid}),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));
	Module->Roles = {ELayoutModuleRole::Interior};

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_RoleOnly"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);

	TArray<FLayoutPlannedCell> PlannedCells;
	FLayoutPlannedCell& PlannedCell = PlannedCells.AddDefaulted_GetRef();
	PlannedCell.Cell = FIntVector(0, 0, 0);
	PlannedCell.Intent = ELayoutCellIntent::Core;

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 107, FIntPoint(1, 1), PlannedCells);

	TestTrue(TEXT("Role-only module solve succeeds"), Result.bSucceeded);
	TestEqual(TEXT("Role-only solve places one module"), Result.Placements.Num(), 1);
	if (Result.Placements.Num() == 1)
	{
		TestEqual(TEXT("Role-only solve uses the role-only module"), Result.Placements[0].Module.Get(), Module);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverOverrideRegionRequestMatchesPlannedCellSolveTest,
	"PorismExtension.Layout.Solver.Profile.OverrideRegionRequestMatchesPlannedCellSolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverOverrideRegionRequestMatchesPlannedCellSolveTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_OverrideRequestParity"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);

	TArray<FLayoutPlannedCell> PlannedCells;
	FLayoutPlannedCell& PlannedCell = PlannedCells.AddDefaulted_GetRef();
	PlannedCell.Cell = FIntVector(0, 0, 0);
	PlannedCell.Intent = ELayoutCellIntent::Core;

	const FLayoutSolveResult DirectResult = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 97, FIntPoint(1, 1), PlannedCells);

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 97, TEXT("Test/Override"));
	/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	Request.FootprintSize = FIntPoint(1, 1);
	Request.PlannedCells = PlannedCells;
	const FLayoutRegionSolveResult RegionResult = FLayoutProfileSolver::SolveRegion(Request);

	TestTrue(TEXT("Planned-cell solve succeeds"), DirectResult.bSucceeded);
	TestTrue(TEXT("Override region request solve succeeds"), RegionResult.SolveResult.bSucceeded);
	TestTrue(TEXT("Override region request solve matches planned-cell placements"), SolveResultsHaveMatchingPlacements(DirectResult, RegionResult.SolveResult));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverRetainsLowerPrefixAfterMiddleLevelFailureTest,
	"PorismExtension.Layout.Solver.Profile.RetainsLowerPrefixAfterMiddleLevelFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverRetainsLowerPrefixAfterMiddleLevelFailureTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	ULayoutModuleAsset* InteriorModule = CreateModule(
		Outer,
		TEXT("LayoutModule_RetainedLowerPrefix"),
		CreateTemplate(Outer, TEXT("LayoutTemplate_RetainedLowerPrefix"), FIntVector(16, 16, 16)),
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));

	FLayoutRegionSolveRequest Request;
	Request.RegionDebugPath = TEXT("Test/RetainedLowerPrefix");
	Request.FootprintSize = FIntPoint(1, 1);
	Request.Seed = 109;
	Request.ProfileSnapshot.LevelCount = 3;
	Request.ModuleCatalog.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(InteriorModule, 0));
	for (int32 Z = 0; Z < 3; ++Z)
	{
		FLayoutPlannedCell& Cell = Request.PrecomputedPlannedCells.AddDefaulted_GetRef();
		Cell.Cell = FIntVector(0, 0, Z);
		Cell.Intent = Z == 1 ? ELayoutCellIntent::Entry : ELayoutCellIntent::Interior;
	}

	const FLayoutRegionSolveResult Result = FLayoutProfileSolver::SolveRegion(Request);
	TestFalse(TEXT("Full multi-level solve remains rejected"), Result.SolveResult.bSucceeded);
	TestEqual(TEXT("Largest solved lower prefix retains one placement"), Result.SolveResult.Placements.Num(), 1);
	if (Result.SolveResult.Placements.Num() == 1)
	{
		TestEqual(TEXT("Retained placement belongs to level below failed middle level"),
			Result.SolveResult.Placements[0].Cell,
			FIntVector(0, 0, 0));
	}

	const FLayoutRegionSolveScheduleResult ScheduledResult = FLayoutProfileSolver::SolveRegionTree(Request);
	TestFalse(TEXT("Scheduled full multi-level solve remains rejected"), ScheduledResult.bSucceeded);
	TestEqual(TEXT("Scheduled failure retains lower-prefix placement"),
		ScheduledResult.MergedSolveResult.Placements.Num(),
		1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverSchedulerOrdersIndependentRegionsByDebugPathTest,
	"PorismExtension.Layout.Solver.Profile.SchedulerOrdersIndependentRegionsByDebugPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverSchedulerOrdersIndependentRegionsByDebugPathTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(GetTransientPackage(), TEXT("LayoutProfile_SchedulerIndependent"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);

	FLayoutRegionSolveScheduleRequest ScheduleRequest;
	ScheduleRequest.RegionRequests.Add(FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 11, TEXT("Region/B")));
	ScheduleRequest.RegionRequests.Add(FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 13, TEXT("Region/A")));

	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionsSynchronously(ScheduleRequest);

	TestTrue(TEXT("Independent region schedule succeeds"), ScheduleResult.bSucceeded);
	TestEqual(TEXT("Both independent regions execute"), ScheduleResult.RegionResults.Num(), 2);
	TestEqual(TEXT("Lexically first region executes first"), ScheduleResult.RegionResults[0].RegionDebugPath, FString(TEXT("Region/A")));
	TestEqual(TEXT("Lexically second region executes second"), ScheduleResult.RegionResults[1].RegionDebugPath, FString(TEXT("Region/B")));
	TestEqual(TEXT("Merged result contains both placements"), ScheduleResult.MergedSolveResult.Placements.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverSchedulerHonorsDependenciesBeforeDebugPathTest,
	"PorismExtension.Layout.Solver.Profile.SchedulerHonorsDependenciesBeforeDebugPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverSchedulerHonorsDependenciesBeforeDebugPathTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(GetTransientPackage(), TEXT("LayoutProfile_SchedulerDependency"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);

	FLayoutRegionSolveScheduleRequest ScheduleRequest;
	ScheduleRequest.RegionRequests.Add(FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 17, TEXT("Region/A")));
	ScheduleRequest.RegionRequests.Add(FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 19, TEXT("Region/Z")));

	FLayoutRegionSolveDependency& Dependency = ScheduleRequest.Dependencies.AddDefaulted_GetRef();
	Dependency.PrerequisiteRegionDebugPath = TEXT("Region/Z");
	Dependency.DependentRegionDebugPath = TEXT("Region/A");

	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionsSynchronously(ScheduleRequest);

	TestTrue(TEXT("Dependent region schedule succeeds"), ScheduleResult.bSucceeded);
	TestEqual(TEXT("Both dependent regions execute"), ScheduleResult.RegionResults.Num(), 2);
	TestEqual(TEXT("Prerequisite region executes before lexically earlier dependent"), ScheduleResult.RegionResults[0].RegionDebugPath, FString(TEXT("Region/Z")));
	TestEqual(TEXT("Dependent region executes after prerequisite"), ScheduleResult.RegionResults[1].RegionDebugPath, FString(TEXT("Region/A")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverSchedulerReportsDependencyCyclesTest,
	"PorismExtension.Layout.Solver.Profile.SchedulerReportsDependencyCycles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverSchedulerReportsDependencyCyclesTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(GetTransientPackage(), TEXT("LayoutProfile_SchedulerCycle"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);

	FLayoutRegionSolveScheduleRequest ScheduleRequest;
	ScheduleRequest.RegionRequests.Add(FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 23, TEXT("Region/A")));
	ScheduleRequest.RegionRequests.Add(FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 29, TEXT("Region/B")));

	FLayoutRegionSolveDependency& FirstDependency = ScheduleRequest.Dependencies.AddDefaulted_GetRef();
	FirstDependency.PrerequisiteRegionDebugPath = TEXT("Region/A");
	FirstDependency.DependentRegionDebugPath = TEXT("Region/B");

	FLayoutRegionSolveDependency& SecondDependency = ScheduleRequest.Dependencies.AddDefaulted_GetRef();
	SecondDependency.PrerequisiteRegionDebugPath = TEXT("Region/B");
	SecondDependency.DependentRegionDebugPath = TEXT("Region/A");

	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionsSynchronously(ScheduleRequest);

	TestFalse(TEXT("Cyclic region schedule fails"), ScheduleResult.bSucceeded);
	TestTrue(TEXT("Cyclic region schedule reports a failure reason"), !ScheduleResult.FailureReason.IsEmpty());
	TestEqual(TEXT("No cyclic region executes"), ScheduleResult.RegionResults.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverDerivesStableRegionSeedsTest,
	"PorismExtension.Layout.Solver.Profile.DerivesStableRegionSeeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverDerivesStableRegionSeedsTest::RunTest(const FString& Parameters)
{
	const int32 FirstSeed = FLayoutProfileSolver::DeriveRegionSeed(
			12345,
			FIntVector(7, -3, 0),
			TEXT("PlanningContext/Primary"),
		TEXT("Region/Interior/A"),
		TEXT("LayoutProfile_Room"));
	const int32 MatchingSeed = FLayoutProfileSolver::DeriveRegionSeed(
			12345,
			FIntVector(7, -3, 0),
			TEXT("PlanningContext/Primary"),
		TEXT("Region/Interior/A"),
		TEXT("LayoutProfile_Room"));
	const int32 DifferentRegionSeed = FLayoutProfileSolver::DeriveRegionSeed(
			12345,
			FIntVector(7, -3, 0),
			TEXT("PlanningContext/Primary"),
		TEXT("Region/Interior/B"),
		TEXT("LayoutProfile_Room"));
	const int32 DifferentLocationSeed = FLayoutProfileSolver::DeriveRegionSeed(
			12345,
			FIntVector(8, -3, 0),
			TEXT("PlanningContext/Primary"),
		TEXT("Region/Interior/A"),
		TEXT("LayoutProfile_Room"));

	TestEqual(TEXT("Region seed derivation is stable for identical inputs"), MatchingSeed, FirstSeed);
	TestNotEqual(TEXT("Region debug path contributes to derived seed"), DifferentRegionSeed, FirstSeed);
	TestNotEqual(TEXT("Layout location contributes to derived seed"), DifferentLocationSeed, FirstSeed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverRegionExportsExteriorEntryBoundaryPointTest,
	"PorismExtension.Layout.Solver.Profile.RegionExportsExteriorEntryBoundaryPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverRegionExportsExteriorEntryBoundaryPointTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutTemplate_BoundaryEntry"), FIntVector(16, 16, 16));
	const FGameplayTagContainer GroundWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});
	ULayoutModuleAsset* EntryModule = CreateModule(
		Outer,
		TEXT("LayoutModule_BoundaryEntry"),
		Template,
		{ELayoutCellIntent::Entry},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceEntry}), MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		},
		GroundWalkable);
	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_BoundaryEntry"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 1, false);

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 31, TEXT("Region/ExteriorEntry"));
	/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	Request.FootprintSize = FIntPoint(1, 1);
	FLayoutPlannedCell& PlannedCell = Request.PlannedCells.AddDefaulted_GetRef();
	PlannedCell.Cell = FIntVector(0, 0, 0);
	PlannedCell.Intent = ELayoutCellIntent::Entry;

	const FLayoutRegionSolveResult RegionResult = FLayoutProfileSolver::SolveRegion(Request);

	TestTrue(TEXT("Exterior-entry region solve succeeds"), RegionResult.SolveResult.bSucceeded);
	const FLayoutSolveBoundaryPoint* EntryBoundary = RegionResult.ExportedBoundaryPoints.FindByPredicate([](const FLayoutSolveBoundaryPoint& BoundaryPoint)
	{
		const bool bIsHorizontalFace =
			BoundaryPoint.FaceDirection == ELayoutFaceDirection::PosX
			|| BoundaryPoint.FaceDirection == ELayoutFaceDirection::NegX
			|| BoundaryPoint.FaceDirection == ELayoutFaceDirection::PosY
			|| BoundaryPoint.FaceDirection == ELayoutFaceDirection::NegY;
		return BoundaryPoint.LocalCell == FIntVector(0, 0, 0)
			&& bIsHorizontalFace
			&& BoundaryPoint.ConnectionTag == LayoutGameplayTags::FaceEntry
			&& BoundaryPoint.ConnectedTraversalChannels.HasTagExact(LayoutGameplayTags::TraversalPrimary)
			&& BoundaryPoint.SourceRegionDebugPath == TEXT("Region/ExteriorEntry");
	});
	TestTrue(TEXT("Exterior entry face exports a traversable boundary point"), EntryBoundary != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverRegionConsumesIncomingVerticalBoundaryPointTest,
	"PorismExtension.Layout.Solver.Profile.RegionConsumesIncomingVerticalBoundaryPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverRegionConsumesIncomingVerticalBoundaryPointTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(16, 16, 16);
	const FGameplayTagContainer WalkwayTags = MakeTags({LayoutGameplayTags::FaceOpen});
	const FGameplayTagContainer SolidTags = MakeTags({LayoutGameplayTags::FaceSolid});
	const FGameplayTagContainer UpperWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});

	UChunkStructureTemplate* LowerTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_IncomingBoundaryLower"), CellSize);
	ULayoutModuleAsset* LowerModule = CreateModule(
		Outer,
		TEXT("LayoutModule_IncomingBoundaryLower"),
		LowerTemplate,
		{},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, WalkwayTags, WalkwayTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, UpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegZ, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		UpperWalkable);
	LowerModule->Roles = {ELayoutModuleRole::VerticalAccess};

	UChunkStructureTemplate* UpperTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_IncomingBoundaryUpper"), CellSize);
	ULayoutModuleAsset* UpperModule = CreateModule(
		Outer,
		TEXT("LayoutModule_IncomingBoundaryUpper"),
		UpperTemplate,
		{ELayoutCellIntent::Boundary},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, WalkwayTags, WalkwayTags, ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor, UpperWalkable)
		},
		UpperWalkable);

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_IncomingVerticalBoundary"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, true);

	FLayoutRegionSolveRequest LowerRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 149, TEXT("Region/Lower"));
	/* !LowerRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	LowerRequest.FootprintSize = FIntPoint(1, 1);
	FLayoutPlannedCell& LowerCell = LowerRequest.PlannedCells.AddDefaulted_GetRef();
	LowerCell.Cell = FIntVector(0, 0, 0);
	LowerCell.Intent = ELayoutCellIntent::VerticalAccess;

	const FLayoutRegionSolveResult LowerResult = FLayoutProfileSolver::SolveRegion(LowerRequest);
	TestTrue(TEXT("Lower region solve succeeds"), LowerResult.SolveResult.bSucceeded);
	const FLayoutSolveBoundaryPoint* VerticalBoundary = LowerResult.ExportedBoundaryPoints.FindByPredicate([](const FLayoutSolveBoundaryPoint& BoundaryPoint)
	{
		return BoundaryPoint.LocalCell == FIntVector(0, 0, 0)
			&& BoundaryPoint.FaceDirection == ELayoutFaceDirection::PosZ
			&& BoundaryPoint.ConnectedTraversalChannels.HasTagExact(LayoutGameplayTags::TraversalPrimary);
	});
	TestTrue(TEXT("Lower region exports a traversable vertical boundary point"), VerticalBoundary != nullptr);
	if (VerticalBoundary == nullptr)
	{
		return false;
	}

	FLayoutRegionSolveRequest UpperRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 151, TEXT("Region/Upper"));
	/* !UpperRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	UpperRequest.FootprintSize = FIntPoint(1, 1);
	FLayoutPlannedCell& UpperCell = UpperRequest.PlannedCells.AddDefaulted_GetRef();
	UpperCell.Cell = FIntVector(0, 0, 1);
	UpperCell.Intent = ELayoutCellIntent::Boundary;
	UpperRequest.IncomingBoundaryPoints.Add(*VerticalBoundary);

	const FLayoutRegionSolveResult UpperResult = FLayoutProfileSolver::SolveRegion(UpperRequest);
	if (!UpperResult.SolveResult.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Upper region incoming-boundary solve failed: %s"), *UpperResult.SolveResult.FailureReason));
	}

	TestTrue(TEXT("Upper region consumes the lower vertical boundary as its filled traversal neighbor"), UpperResult.SolveResult.bSucceeded);
	TestEqual(TEXT("Upper region places one module"), UpperResult.SolveResult.Placements.Num(), 1);
	if (UpperResult.SolveResult.Placements.Num() == 1)
	{
		TestEqual(TEXT("Upper region placed the landing module"), UpperResult.SolveResult.Placements[0].Module.Get(), UpperModule);
	}

	FLayoutRegionSolveRequest ScheduledLowerRequest = LowerRequest;
	FLayoutRegionSolveRequest ScheduledUpperRequest = UpperRequest;
	ScheduledUpperRequest.IncomingBoundaryPoints.Reset();

	FLayoutRegionSolveScheduleRequest ScheduleRequest;
	ScheduleRequest.RegionRequests.Add(ScheduledUpperRequest);
	ScheduleRequest.RegionRequests.Add(ScheduledLowerRequest);
	FLayoutRegionSolveDependency& Dependency = ScheduleRequest.Dependencies.AddDefaulted_GetRef();
	Dependency.PrerequisiteRegionDebugPath = TEXT("Region/Lower");
	Dependency.DependentRegionDebugPath = TEXT("Region/Upper");

	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionsSynchronously(ScheduleRequest);
	if (!ScheduleResult.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Scheduled incoming-boundary solve failed: %s"), *ScheduleResult.FailureReason));
	}

	TestTrue(TEXT("Scheduler forwards prerequisite exported boundaries to the dependent region"), ScheduleResult.bSucceeded);
	TestEqual(TEXT("Scheduled solve executes both regions"), ScheduleResult.RegionResults.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverRegionConsumesIncomingExteriorBoundaryAsEntryRootTest,
	"PorismExtension.Layout.Solver.Profile.RegionConsumesIncomingExteriorBoundaryAsEntryRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverRegionConsumesIncomingExteriorBoundaryAsEntryRootTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FGameplayTagContainer GroundWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});
	const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
	const FGameplayTagContainer SolidTags = MakeTags({LayoutGameplayTags::FaceSolid});

	ULayoutModuleAsset* EntryModule = CreateModule(
		Outer,
		TEXT("LayoutModule_IncomingExteriorEntryRoot"),
		CreateTemplate(Outer, TEXT("LayoutTemplate_IncomingExteriorEntryRoot"), FIntVector(16, 16, 16)),
		{ELayoutCellIntent::Entry},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, OpenTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		GroundWalkable);

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_IncomingExteriorEntryRoot"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	Profile->bRequireAllTraversalChannelsReachable = true;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 163, TEXT("Region/IncomingExterior"));
	/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	Request.FootprintSize = FIntPoint(1, 1);
	FLayoutPlannedCell& PlannedCell = Request.PlannedCells.AddDefaulted_GetRef();
	PlannedCell.Cell = FIntVector(0, 0, 0);
	PlannedCell.Intent = ELayoutCellIntent::Entry;

	FLayoutSolveBoundaryPoint& ExteriorBoundary = Request.IncomingBoundaryPoints.AddDefaulted_GetRef();
	ExteriorBoundary.LocalCell = FIntVector(-1, 0, 0);
	ExteriorBoundary.FaceDirection = ELayoutFaceDirection::PosX;
	ExteriorBoundary.ConnectionTag = LayoutGameplayTags::FaceEntry;
	ExteriorBoundary.ConnectedTraversalChannels = GroundWalkable;
	ExteriorBoundary.bRepresentsFilledNeighbor = false;
	ExteriorBoundary.SourceRegionDebugPath = TEXT("World/Exterior");
	ExteriorBoundary.SourceCell = ExteriorBoundary.LocalCell;

	const FLayoutRegionSolveResult RegionResult = FLayoutProfileSolver::SolveRegion(Request);
	if (!RegionResult.SolveResult.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Incoming exterior boundary solve failed: %s"), *RegionResult.SolveResult.FailureReason));
	}

	TestTrue(TEXT("Incoming empty exterior boundary can seed entry reachability"), RegionResult.SolveResult.bSucceeded);
	TestEqual(TEXT("Incoming exterior boundary solve places one module"), RegionResult.SolveResult.Placements.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverBuildsIndexedDomainsForStandaloneRequestTest,
	"PorismExtension.Layout.Solver.Profile.BuildsIndexedDomainsForStandaloneRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverBuildsIndexedDomainsForStandaloneRequestTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_IndexedStandalone"), FIntPoint(2, 2), FIntPoint(2, 2), 1, 1, false);

	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 37, TEXT("Indexed/Standalone"));
	const FLayoutIndexedDomainSnapshot IndexedSnapshot = FLayoutProfileSolver::BuildIndexedDomainSnapshot(Request);
	const FLayoutSolveResult SolveResult = FLayoutProfileSolver::SolveRegion(Request).SolveResult;

	TestTrue(TEXT("Indexed domain snapshot succeeds"), IndexedSnapshot.bSucceeded);
	TestTrue(TEXT("Reference solve succeeds"), SolveResult.bSucceeded);
	TestEqual(TEXT("Indexed domains match planned-cell count"), IndexedSnapshot.CellDomains.Num(), SolveResult.PlannedCells.Num());
	TestTrue(TEXT("Indexed candidate universe is non-empty"), IndexedSnapshot.Candidates.Num() > 0);

	for (const FLayoutIndexedCellDomain& CellDomain : IndexedSnapshot.CellDomains)
	{
		TestEqual(TEXT("Candidate bit count matches ordered candidate count"), CountIndexedDomainBits(CellDomain.CandidateBits), CellDomain.OrderedCandidateIndices.Num());
		for (const int32 CandidateIndex : CellDomain.OrderedCandidateIndices)
		{
			TestTrue(TEXT("Ordered candidate index is valid"), IndexedSnapshot.Candidates.IsValidIndex(CandidateIndex));
		}
	}

	return true;
}









IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverMatchesYawForVerticalContinuationFacesTest,
	"PorismExtension.Layout.Solver.Profile.MatchesYawForVerticalContinuationFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverMatchesYawForVerticalContinuationFacesTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(8, 8, 8);
	UChunkStructureTemplate* BaseTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_YawLockedBase"), CellSize);
	UChunkStructureTemplate* UpperTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_YawLockedUpper"), CellSize);
	const FGameplayTagContainer WalkwayTags = MakeTags({LayoutGameplayTags::FaceOpen});
	const FGameplayTagContainer SolidTags = MakeTags({LayoutGameplayTags::FaceSolid});

	ULayoutModuleAsset* Base = CreateModule(
		Outer,
		TEXT("LayoutModule_YawLockedBase"),
		BaseTemplate,
		{ELayoutCellIntent::Entry},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeYawLockedFaceRule(ELayoutFaceDirection::PosZ, WalkwayTags, WalkwayTags, ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});

	ULayoutModuleAsset* Upper = CreateModule(
		Outer,
		TEXT("LayoutModule_YawLockedUpper"),
		UpperTemplate,
		{ELayoutCellIntent::Boundary},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeYawLockedFaceRule(ELayoutFaceDirection::NegZ, WalkwayTags, WalkwayTags, ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor)
		});

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_YawLockedVertical"), FIntPoint(1, 1), FIntPoint(1, 1), 2, 0, true);

	TArray<FLayoutPlannedCell> PlannedCells;
	FLayoutPlannedCell& BaseCell = PlannedCells.AddDefaulted_GetRef();
	BaseCell.Cell = FIntVector(0, 0, 0);
	BaseCell.Intent = ELayoutCellIntent::Entry;
	FLayoutPlannedCell& UpperCell = PlannedCells.AddDefaulted_GetRef();
	UpperCell.Cell = FIntVector(0, 0, 1);
	UpperCell.Intent = ELayoutCellIntent::Boundary;

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 73, FIntPoint(1, 1), PlannedCells);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Yaw-locked vertical solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Solver accepts yaw-locked vertical continuations when a matching rotation exists"), Result.bSucceeded);
	TestEqual(TEXT("Yaw-locked vertical solve places both levels"), Result.Placements.Num(), 2);

	int32 BaseYaw = INDEX_NONE;
	int32 UpperYaw = INDEX_NONE;
	for (const FLayoutPlacedModule& Placement : Result.Placements)
	{
		if (Placement.Cell == FIntVector(0, 0, 0))
		{
			BaseYaw = Placement.YawRotationSteps;
		}
		else if (Placement.Cell == FIntVector(0, 0, 1))
		{
			UpperYaw = Placement.YawRotationSteps;
		}
	}

	TestTrue(TEXT("Base yaw was captured"), BaseYaw != INDEX_NONE);
	TestTrue(TEXT("Upper yaw was captured"), UpperYaw != INDEX_NONE);
	TestEqual(TEXT("Yaw-locked vertical continuation keeps stacked modules phase-aligned"), UpperYaw, BaseYaw);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverSquareModulesIgnoreAuthoredYawMismatchForVerticalContinuationFacesTest,
	"PorismExtension.Layout.Solver.Profile.SquareModulesIgnoreAuthoredYawMismatchForVerticalContinuationFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverSquareModulesIgnoreAuthoredYawMismatchForVerticalContinuationFacesTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(8, 8, 8);
	UChunkStructureTemplate* BaseTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_YawMismatchBase"), CellSize);
	UChunkStructureTemplate* UpperTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_YawMismatchUpper"), CellSize);
	const FGameplayTagContainer WalkwayTags = MakeTags({LayoutGameplayTags::FaceOpen});
	const FGameplayTagContainer SolidTags = MakeTags({LayoutGameplayTags::FaceSolid});

	ULayoutModuleAsset* Base = CreateModule(
		Outer,
		TEXT("LayoutModule_YawMismatchBase"),
		BaseTemplate,
		{ELayoutCellIntent::Entry},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeYawLockedFaceRule(ELayoutFaceDirection::PosZ, WalkwayTags, WalkwayTags, ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});

	ULayoutModuleAsset* Upper = CreateModule(
		Outer,
		TEXT("LayoutModule_YawMismatchUpper"),
		UpperTemplate,
		{ELayoutCellIntent::Boundary},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, SolidTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeYawLockedFaceRule(ELayoutFaceDirection::NegZ, WalkwayTags, WalkwayTags, ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor)
		});

	const FLayoutValidationResult ValidationResult = FLayoutValidationResult();
	TestTrue(TEXT("Module-set validation ignores authored yaw mismatch on square-cell vertical continuations"), ValidationResult.IsValid());

	bool bFoundYawLockedPartnerMessage = false;
	for (const FLayoutValidationMessage& Message : ValidationResult.Messages)
	{
		bFoundYawLockedPartnerMessage |= Message.Message.Contains(TEXT("compatible rotated partner face"));
	}

	TestFalse(TEXT("Validation no longer reports missing yaw-compatible rotated partners for square-cell modules"), bFoundYawLockedPartnerMessage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverRejectsUnreachableUpperWalkTest,
	"PorismExtension.Layout.Solver.Profile.RejectsUnreachableUpperWalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverRejectsUnreachableUpperWalkTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_VerticalReject"), FIntPoint(1, 1), FIntPoint(1, 1), 2, 1, true);
	Profile->bRequireAllTraversalChannelsReachable = true;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 5);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Unreachable upper-walk solve failed with reason: %s"), *Result.FailureReason));
	}

	TestFalse(TEXT("Solver rejects upper walkable areas that are not reachable from the entry root"), Result.bSucceeded);
	TestTrue(TEXT("Failure reason mentions walkable-area reachability or entry-root failure"),
		Result.FailureReason.Contains(TEXT("walkable"))
		|| Result.FailureReason.Contains(TEXT("reachable"))
		|| Result.FailureReason.Contains(TEXT("entry root")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverIgnoresUnrequiredUnreachableWalkTest,
	"PorismExtension.Layout.Solver.Profile.IgnoresUnrequiredUnreachableWalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverIgnoresUnrequiredUnreachableWalkTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_VerticalAdvisory"), FIntPoint(1, 1), FIntPoint(1, 1), 2, 1, true);
	Profile->bRequireAllTraversalChannelsReachable = false;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 5);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Advisory unreachable-walk solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Solver allows disconnected walkable metadata when strict reachability is disabled"), Result.bSucceeded);

	bool bFoundReachabilityWarning = false;
	for (const FLayoutValidationMessage& Message : Result.Messages)
	{
		bFoundReachabilityWarning |= Message.Severity == ELayoutValidationSeverity::Warning
			&& Message.Message.Contains(TEXT("unreachable"));
	}

	TestFalse(TEXT("Solver does not warn about unrequired disconnected walkable metadata"), bFoundReachabilityWarning);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverAcceptsReachableUpperWalkTest,
	"PorismExtension.Layout.Solver.Profile.AcceptsReachableUpperWalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverAcceptsReachableUpperWalkTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_VerticalAccept"), FIntPoint(1, 1), FIntPoint(1, 1), 2, 1, true);

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 5);
	TestTrue(TEXT("Solver accepts vertical layouts when the base module provides authored stair access"), Result.bSucceeded);
	TestEqual(TEXT("Reachable vertical solve places both levels"), Result.Placements.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverDistributesEntriesAcrossBoundaryLoopTest,
	"PorismExtension.Layout.Solver.Profile.DistributesEntriesAcrossBoundaryLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverDistributesEntriesAcrossBoundaryLoopTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_EntryBoundaryLoop"), FIntPoint(3, 3), FIntPoint(3, 3), 1, 4, false);

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 29);

	int32 EntryCount = 0;
	bool bFoundEntryAwayFromYZero = false;
	for (const FLayoutPlannedCell& PlannedCell : Result.PlannedCells)
	{
		if (PlannedCell.Intent == ELayoutCellIntent::Entry)
		{
			++EntryCount;
			bFoundEntryAwayFromYZero |= PlannedCell.Cell.Y != 0;
		}
	}

	TestEqual(TEXT("Planner reserves the requested number of boundary entries"), EntryCount, 4);
	TestTrue(TEXT("Entries are not limited to the Y=0 edge"), bFoundEntryAwayFromYZero);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverPrefersNonCornerEntriesTest,
	"PorismExtension.Layout.Solver.Profile.PrefersNonCornerEntries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverPrefersNonCornerEntriesTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_NonCornerEntries"), FIntPoint(7, 4), FIntPoint(7, 4), 1, 1, false);

	for (int32 Seed = 0; Seed < 16; ++Seed)
	{
		const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, Seed);
		TestTrue(FString::Printf(TEXT("Seed %d solves"), Seed), Result.bSucceeded);

		for (const FLayoutPlannedCell& PlannedCell : Result.PlannedCells)
		{
			if (PlannedCell.Intent != ELayoutCellIntent::Entry)
			{
				continue;
			}

			const bool bOnXEdge = PlannedCell.Cell.X == 0 || PlannedCell.Cell.X == Result.FootprintSize.X - 1;
			const bool bOnYEdge = PlannedCell.Cell.Y == 0 || PlannedCell.Cell.Y == Result.FootprintSize.Y - 1;
			TestFalse(
				FString::Printf(TEXT("Seed %d reserves normal entries away from footprint corners"), Seed),
				bOnXEdge && bOnYEdge);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverSupportsEntryCountModesTest,
	"PorismExtension.Layout.Solver.Profile.SupportsEntryCountModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverSupportsEntryCountModesTest::RunTest(const FString& Parameters)
{

	ULayoutProfileAsset* NoEntryProfile = CreateProfileWithUniversalContentSet(GetTransientPackage(), TEXT("LayoutProfile_NoEntries"), FIntPoint(3, 3), FIntPoint(3, 3), 1, 0, false);
	NoEntryProfile->EntryCountMode = ELayoutCountConstraintMode::None;
	const FLayoutSolveResult NoEntryResult = FLayoutProfileSolver::Solve(NoEntryProfile, 41);

	int32 NoEntryCount = 0;
	for (const FLayoutPlannedCell& PlannedCell : NoEntryResult.PlannedCells)
	{
		NoEntryCount += PlannedCell.Intent == ELayoutCellIntent::Entry ? 1 : 0;
	}

	TestTrue(TEXT("EntryCountMode=None still solves with no planned entry cells"), NoEntryResult.bSucceeded);
	TestEqual(TEXT("EntryCountMode=None reserves zero entry cells"), NoEntryCount, 0);

	ULayoutProfileAsset* RangeProfile = CreateProfileWithUniversalContentSet(GetTransientPackage(), TEXT("LayoutProfile_RangedEntries"), FIntPoint(3, 3), FIntPoint(3, 3), 1, 1, false);
	RangeProfile->EntryCountMode = ELayoutCountConstraintMode::Range;
	RangeProfile->MinEntryCount = 2;
	RangeProfile->MaxEntryCount = 2;
	const FLayoutSolveResult RangeResult = FLayoutProfileSolver::Solve(RangeProfile, 43);
	if (!RangeResult.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("EntryCountMode=Range solve failed: %s"), *RangeResult.FailureReason));
	}

	int32 RangeEntryCount = 0;
	for (const FLayoutPlannedCell& PlannedCell : RangeResult.PlannedCells)
	{
		RangeEntryCount += PlannedCell.Intent == ELayoutCellIntent::Entry ? 1 : 0;
	}

	TestTrue(TEXT("EntryCountMode=Range solves successfully"), RangeResult.bSucceeded);
	TestEqual(TEXT("EntryCountMode=Range reserves the deterministic count inside the configured range"), RangeEntryCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverPlansVerticalAccessCellsTest,
	"PorismExtension.Layout.Solver.Profile.PlansVerticalAccessCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverPlansVerticalAccessCellsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(GetTransientPackage(), TEXT("LayoutProfile_VerticalAccessCells"), FIntPoint(5, 5), FIntPoint(5, 5), 2, 1, false);
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 2;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 47);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Vertical-access planning solve failed: %s"), *Result.FailureReason));
		for (const FLayoutPlannedCell& PlannedCell : Result.PlannedCells)
		{
			AddInfo(FString::Printf(TEXT("Planned cell: %s intent=%d"), *PlannedCell.Cell.ToString(), static_cast<int32>(PlannedCell.Intent)));
		}
		for (const FLayoutRouteConstraintRecord& RouteConstraint : Result.RouteConstraints)
		{
			FString RequirementSummary;
			for (const FLayoutRouteFaceRequirement& Requirement : RouteConstraint.FaceRequirements)
			{
				if (!RequirementSummary.IsEmpty())
				{
					RequirementSummary += TEXT(",");
				}
				RequirementSummary += FString::Printf(
					TEXT("%s:%s"),
					*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Requirement.FaceDirection)),
					Requirement.TraversalChannel.IsValid() ? *Requirement.TraversalChannel.ToString() : TEXT("None"));
			}
			AddInfo(FString::Printf(TEXT("Route constraint: cell=%s intent=%d faces=[%s]"),
				*RouteConstraint.Cell.ToString(),
				static_cast<int32>(RouteConstraint.Intent),
				*RequirementSummary));
		}
	}

	int32 VerticalAccessIntentCount = 0;
	TSet<FIntPoint> VerticalAccessColumns;
	int32 PlannedTopColumnCellCount = 0;
	for (const FLayoutPlannedCell& PlannedCell : Result.PlannedCells)
	{
		if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
		{
			++VerticalAccessIntentCount;
			VerticalAccessColumns.Add(FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y));
		}
	}

	for (const FLayoutPlannedCell& PlannedCell : Result.PlannedCells)
	{
		if (PlannedCell.Cell.Z == 1 && VerticalAccessColumns.Contains(FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y)))
		{
			++PlannedTopColumnCellCount;
			TestNotEqual(TEXT("Topmost cells in vertical-access columns are planned as landing/floor cells instead of upward vertical-access cells"), PlannedCell.Intent, ELayoutCellIntent::VerticalAccess);
		}
	}

	TestTrue(TEXT("Profile with exact vertical access count solves successfully"), Result.bSucceeded);
	TestEqual(TEXT("Planner reserves the requested vertical-access transition cells below the top level"), VerticalAccessIntentCount, 2);
	TestEqual(TEXT("Planner reserves the exact requested vertical-access column count"), VerticalAccessColumns.Num(), 2);
	TestEqual(TEXT("Each vertical-access column keeps a planned top landing cell"), PlannedTopColumnCellCount, 2);
	TArray<FIntPoint> VerticalAccessColumnArray = VerticalAccessColumns.Array();
	for (int32 LeftIndex = 0; LeftIndex < VerticalAccessColumnArray.Num(); ++LeftIndex)
	{
		for (int32 RightIndex = LeftIndex + 1; RightIndex < VerticalAccessColumnArray.Num(); ++RightIndex)
		{
			const int32 ManhattanDistance = FMath::Abs(VerticalAccessColumnArray[LeftIndex].X - VerticalAccessColumnArray[RightIndex].X)
				+ FMath::Abs(VerticalAccessColumnArray[LeftIndex].Y - VerticalAccessColumnArray[RightIndex].Y);
			TestTrue(TEXT("Planner avoids directly adjacent vertical-access columns when the footprint has room"), ManhattanDistance > 1);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverPlansVerticalAccessPerTransitionTest,
	"PorismExtension.Layout.Solver.Profile.PlansVerticalAccessPerTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverPlansVerticalAccessPerTransitionTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateLayoutProfileSolverTestOuter(TEXT("LayoutProfile_VerticalAccessPerTransition"));
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(Outer, TEXT("LayoutProfile_VerticalAccessPerTransition"), FIntPoint(5, 5), FIntPoint(5, 5), 3, 1, false);
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 1;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 53);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Vertical-access-per-transition solve failed: %s"), *Result.FailureReason));
		for (const FLayoutPlannedCell& PlannedCell : Result.PlannedCells)
		{
			AddInfo(FString::Printf(TEXT("Planned cell: %s intent=%d"), *PlannedCell.Cell.ToString(), static_cast<int32>(PlannedCell.Intent)));
		}
		for (const FLayoutRouteConstraintRecord& RouteConstraint : Result.RouteConstraints)
		{
			FString RequirementSummary;
			for (const FLayoutRouteFaceRequirement& Requirement : RouteConstraint.FaceRequirements)
			{
				if (!RequirementSummary.IsEmpty())
				{
					RequirementSummary += TEXT(",");
				}
				RequirementSummary += FString::Printf(
					TEXT("%s:%s"),
					*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Requirement.FaceDirection)),
					Requirement.TraversalChannel.IsValid() ? *Requirement.TraversalChannel.ToString() : TEXT("None"));
			}
			AddInfo(FString::Printf(TEXT("Route constraint: cell=%s intent=%d faces=[%s]"),
				*RouteConstraint.Cell.ToString(),
				static_cast<int32>(RouteConstraint.Intent),
				*RequirementSummary));
		}
	}
	TestTrue(TEXT("Three-level profile still produces a planned-cell layout before any later solve failure"), !Result.PlannedCells.IsEmpty());

	TMap<int32, FIntPoint> VerticalAccessColumnByLevel;
	for (const FLayoutPlannedCell& PlannedCell : Result.PlannedCells)
	{
		if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
		{
			VerticalAccessColumnByLevel.Add(PlannedCell.Cell.Z, FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y));
		}
	}

	TestEqual(TEXT("Planner reserves one vertical-access transition on level 0"), VerticalAccessColumnByLevel.Contains(0) ? 1 : 0, 1);
	TestEqual(TEXT("Planner reserves one vertical-access transition on level 1"), VerticalAccessColumnByLevel.Contains(1) ? 1 : 0, 1);
	TestFalse(TEXT("Planner does not force the second transition stair to stack over the first transition stair when another candidate exists"),
		VerticalAccessColumnByLevel.Contains(0)
		&& VerticalAccessColumnByLevel.Contains(1)
		&& VerticalAccessColumnByLevel[0] == VerticalAccessColumnByLevel[1]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverFlatRegionRequestDoesNotDuplicateVerticalAccessTest,
	"PorismExtension.Layout.Solver.Profile.FlatRegionRequestDoesNotDuplicateVerticalAccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverFlatRegionRequestDoesNotDuplicateVerticalAccessTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithOneSidedVerticalAccessContentSet(
		GetTransientPackage(),
		TEXT("LayoutProfile_FlatVerticalAccessNoDuplication"),
		FIntPoint(5, 5),
		2);
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 1;
	Profile->bSupportsSteppedTerrainSolve = true;
	const FLayoutSolveResult PrecomputedResult = FLayoutProfileSolver::Solve(Profile, 59);
	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		Profile,
		59,
		TEXT("Test/FlatVerticalAccess"));
	Request.PrecomputedPlannedCells = PrecomputedResult.PlannedCells;
	Request.PlannedCells = PrecomputedResult.PlannedCells;
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::NonSteppedWorldPlacement;
	Request.SelectedModePlan.Scope = ELayoutContractRegionScope::Root;
	Request.SelectedModePlan.bUsesSteppedTerrainTopology = false;
	FString FinalizationFailureReason;
	const bool bFinalized = FLayoutProfileSolver::FinalizeSteppedTerrainPlan(
		Request.PrecomputedPlannedCells,
		Request,
		FinalizationFailureReason);
	TestTrue(TEXT("Selected non-stepped request finalizes authored VerticalAccess alternatives"), bFinalized);
	if (!bFinalized)
	{
		AddError(FinalizationFailureReason);
		return false;
	}
	TestEqual(TEXT("Selected non-stepped request carries one authored VerticalAccess host group"), Request.VerticalAccessHostGroups.Num(), 1);
	TestTrue(TEXT("Selected non-stepped request retains module-compatible host alternatives"),
		Request.VerticalAccessHostGroups.Num() == 1
		&& Request.VerticalAccessHostGroups[0].Options.Num() > 1);
	const FLayoutRegionSolveResult RegionResult = FLayoutProfileSolver::SolveRegion(Request);
	if (!PrecomputedResult.bSucceeded)
	{
		AddInfo(FString::Printf(
			TEXT("Flat precomputed vertical-access solve failed: %s"),
			*PrecomputedResult.FailureReason));
	}
	if (!RegionResult.SolveResult.bSucceeded)
	{
		AddInfo(FString::Printf(
			TEXT("Flat region vertical-access solve failed: %s"),
			*RegionResult.SolveResult.FailureReason));
	}
	TestTrue(TEXT("Flat precomputed layout solves"), PrecomputedResult.bSucceeded);
	TestEqual(
		TEXT("Flat precomputed layout carries one vertical access for its only transition"),
		PrecomputedResult.PlannedCells.FilterByPredicate([](const FLayoutPlannedCell& Cell)
		{
			return Cell.Intent == ELayoutCellIntent::VerticalAccess;
		}).Num(),
		1);
	TestTrue(TEXT("Flat region request solves"), RegionResult.SolveResult.bSucceeded);
	for (const int32 TransitionLevel : {0})
	{
		TestEqual(
			FString::Printf(TEXT("Flat region request reserves one vertical access on transition level %d"), TransitionLevel),
			RegionResult.SolveResult.PlannedCells.FilterByPredicate([TransitionLevel](const FLayoutPlannedCell& Cell)
			{
				return Cell.Cell.Z == TransitionLevel
					&& Cell.Intent == ELayoutCellIntent::VerticalAccess;
			}).Num(),
			1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverRejectsFlatRootVerticalAccessExactShortageTest,
	"PorismExtension.Layout.Solver.Profile.RejectsFlatRootVerticalAccessExactShortage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverRejectsFlatRootVerticalAccessExactShortageTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("LayoutProfile_FlatVerticalAccessExactShortage"),
		FIntPoint(5, 5),
		FIntPoint(5, 5),
		2,
		1,
		false);
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 2;

	const FLayoutSolveResult PlannedResult = FLayoutProfileSolver::Solve(Profile, 61);
	TestTrue(TEXT("Fixture starts with a valid exact flat vertical-access plan"), PlannedResult.bSucceeded);
	TArray<FLayoutPlannedCell> ShortPlan = PlannedResult.PlannedCells;
	FLayoutPlannedCell* const RemovedVerticalAccess = ShortPlan.FindByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Intent == ELayoutCellIntent::VerticalAccess && Cell.Cell.Z == 0;
	});
	TestNotNull(TEXT("Fixture contains a vertical-access cell to remove"), RemovedVerticalAccess);
	if (RemovedVerticalAccess == nullptr)
	{
		return false;
	}
	RemovedVerticalAccess->Intent = ELayoutCellIntent::Interior;

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells(
		Profile,
		61,
		FIntPoint(5, 5),
		ShortPlan);
	TestFalse(TEXT("Flat root rejects exact VerticalAccess shortage before CSP"), Result.bSucceeded);
	TestTrue(TEXT("Failure identifies the deficient flat transition"), Result.FailureReason.Contains(TEXT("Flat root VerticalAccess transition Z=0")));
	TestTrue(TEXT("Failure confirms rejection happens before CSP"), Result.FailureReason.Contains(TEXT("Rejecting before CSP")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverAuditsFlatRootVerticalAccessRangeAndPreservesNoneTest,
	"PorismExtension.Layout.Solver.Profile.AuditsFlatRootVerticalAccessRangeAndPreservesNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverAuditsFlatRootVerticalAccessRangeAndPreservesNoneTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* RangeProfile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("LayoutProfile_FlatVerticalAccessRangeAudit"),
		FIntPoint(5, 5),
		FIntPoint(5, 5),
		2,
		1,
		false);
	RangeProfile->VerticalAccessCountMode = ELayoutCountConstraintMode::Range;
	RangeProfile->MinVerticalAccessCount = 1;
	RangeProfile->MaxVerticalAccessCount = 2;
	const FLayoutSolveResult RangePlan = FLayoutProfileSolver::Solve(RangeProfile, 67);
	TestTrue(TEXT("Range fixture starts with a valid flat VerticalAccess plan"), RangePlan.bSucceeded);
	TArray<FLayoutPlannedCell> ShortRangePlan = RangePlan.PlannedCells;
	for (FLayoutPlannedCell& Cell : ShortRangePlan)
	{
		if (Cell.Cell.Z == 0 && Cell.Intent == ELayoutCellIntent::VerticalAccess)
		{
			Cell.Intent = ELayoutCellIntent::Interior;
		}
	}
	const FLayoutSolveResult ShortRangeResult = FLayoutProfileSolver::SolveWithPlannedCells(
		RangeProfile,
		67,
		FIntPoint(5, 5),
		ShortRangePlan);
	TestFalse(TEXT("Flat root rejects a Range plan below its authored minimum"), ShortRangeResult.bSucceeded);
	TestTrue(TEXT("Range failure identifies the flat VerticalAccess transition"), ShortRangeResult.FailureReason.Contains(TEXT("Flat root VerticalAccess transition Z=0")));

	ULayoutProfileAsset* NoneProfile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("LayoutProfile_FlatVerticalAccessNoneAudit"),
		FIntPoint(5, 5),
		FIntPoint(5, 5),
		2,
		1,
		false);
	NoneProfile->VerticalAccessCountMode = ELayoutCountConstraintMode::None;
	const FLayoutSolveResult NoneResult = FLayoutProfileSolver::Solve(NoneProfile, 71);
	TestTrue(TEXT("None keeps flat VerticalAccess unrestricted and unreserved"), NoneResult.bSucceeded);
	TestEqual(TEXT("None reserves no authored VerticalAccess cells"), NoneResult.PlannedCells.FilterByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Intent == ELayoutCellIntent::VerticalAccess;
	}).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverSeedsVerticalAccessColumnVarietyTest,
	"PorismExtension.Layout.Solver.Profile.SeedsVerticalAccessColumnVariety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverSeedsVerticalAccessColumnVarietyTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(GetTransientPackage(), TEXT("LayoutProfile_VerticalAccessSeededVariety"), FIntPoint(5, 5), FIntPoint(5, 5), 2, 1, false);
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 1;
	Profile->MinVerticalAccessCount = 1;
	Profile->MaxVerticalAccessCount = 1;
	const FLayoutSolverExecutionSettings ExecutionSettings = MakeSolverExecutionSettings(250000, 10.0f);

	TSet<FIntPoint> SelectedColumns;
	bool bSelectedNonCenterColumn = false;
	int32 SucceededSeedCount = 0;
	constexpr int32 ValidationSeedCount = 64;
	for (int32 Seed = 0; Seed < ValidationSeedCount; ++Seed)
	{
		const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, Seed, ExecutionSettings);
		if (!Result.bSucceeded)
		{
			continue;
		}

		++SucceededSeedCount;

		for (const FLayoutPlannedCell& PlannedCell : Result.PlannedCells)
		{
			if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
			{
				const FIntPoint Column(PlannedCell.Cell.X, PlannedCell.Cell.Y);
				SelectedColumns.Add(Column);
				bSelectedNonCenterColumn |= Column != FIntPoint(2, 2);
				break;
			}
		}
	}

	TestTrue(TEXT("Deterministic seed scan finds more than one successful vertical-access solve"), SucceededSeedCount > 1);
	TestTrue(TEXT("Successful vertical-access solves use more than one viable column"), SelectedColumns.Num() > 1);
	TestTrue(TEXT("Successful vertical-access solves are not always pulled to the center column"), bSelectedNonCenterColumn);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverPrefersConstraintAwareVerticalAccessCellsTest,
	"PorismExtension.Layout.Solver.Profile.PrefersConstraintAwareVerticalAccessCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverPrefersConstraintAwareVerticalAccessCellsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(GetTransientPackage(), TEXT("LayoutProfile_ConstraintAwareVerticalAccess"), FIntPoint(5, 5), FIntPoint(5, 5), 2, 0, true);
	Profile->EntryCountMode = ELayoutCountConstraintMode::None;
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 1;
	Profile->bRestrictVerticalAccessModulesToVerticalAccessCells = true;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 211);

	FIntVector VerticalAccessCell = FIntVector::ZeroValue;
	bool bFoundVerticalAccessCell = false;
	for (const FLayoutPlannedCell& PlannedCell : Result.PlannedCells)
	{
		if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
		{
			VerticalAccessCell = PlannedCell.Cell;
			bFoundVerticalAccessCell = true;
			break;
		}
	}

	TestTrue(TEXT("Planner reserves a vertical-access cell"), bFoundVerticalAccessCell);
	TestTrue(TEXT("Planner reserves the vertical-access cell on the lower transition level"), !bFoundVerticalAccessCell || VerticalAccessCell.Z == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverKeepsInteriorVerticalAccessColumnsOnUpperBoundaryOnlyLevelsTest,
	"PorismExtension.Layout.Solver.Profile.KeepsInteriorVerticalAccessColumnsOnUpperBoundaryOnlyLevels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverKeepsInteriorVerticalAccessColumnsOnUpperBoundaryOnlyLevelsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(GetTransientPackage(), TEXT("LayoutProfile_VerticalAccessUpperBoundaryOnly"), FIntPoint(5, 5), FIntPoint(5, 5), 2, 1, true);
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 1;
	const FLayoutSolverExecutionSettings ExecutionSettings = MakeSolverExecutionSettings(50000, 2.0f, 24, ELayoutSolverTraceMode::OnFailure, 64);

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 47, ExecutionSettings);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Upper boundary-only vertical access solve failed: %s"), *Result.FailureReason));
		for (const FLayoutValidationMessage& Message : Result.Messages)
		{
			AddInfo(Message.Message);
		}
	}
	TestTrue(TEXT("Profile with upper boundary-only levels and vertical access solves successfully"), Result.bSucceeded);

	bool bFoundInteriorUpperAccessColumnLanding = false;
	TSet<FIntPoint> VerticalAccessColumns;
	for (const FLayoutPlannedCell& PlannedCell : Result.PlannedCells)
	{
		if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
		{
			VerticalAccessColumns.Add(FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y));
		}
	}

	for (const FLayoutPlannedCell& PlannedCell : Result.PlannedCells)
	{
		const bool bIsBoundary = PlannedCell.Cell.X == 0
			|| PlannedCell.Cell.Y == 0
			|| PlannedCell.Cell.X == 4
			|| PlannedCell.Cell.Y == 4;
		if (PlannedCell.Intent != ELayoutCellIntent::VerticalAccess
			&& PlannedCell.Cell.Z > 0
			&& !bIsBoundary)
		{
			const FIntPoint Column(PlannedCell.Cell.X, PlannedCell.Cell.Y);
			if (VerticalAccessColumns.Contains(Column))
			{
				bFoundInteriorUpperAccessColumnLanding = true;
				break;
			}
		}
	}

	TestTrue(TEXT("Interior vertical-access columns keep non-vertical-access landing cells on upper levels even when upper levels use boundary-only fill"), bFoundInteriorUpperAccessColumnLanding);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FLayoutProfileSolverRequiresTraversalPrimaryContinuationAboveStairsTest,
		"PorismExtension.Layout.Solver.Profile.RequiresTraversalPrimaryContinuationAboveStairs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverRequiresTraversalPrimaryContinuationAboveStairsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_WalkableStairContinuation"), FIntPoint(3, 3), FIntPoint(3, 3), 2, 1, true);
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 1;
	Profile->bRestrictVerticalAccessModulesToVerticalAccessCells = true;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 241);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Walkable stair continuation solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Profile with a walkable-filled stair top solves successfully"), Result.bSucceeded);

	FIntVector StairCell = FIntVector::ZeroValue;
	bool bFoundStair = false;
	for (const FLayoutPlacedModule& Placement : Result.Placements)
	{
		if (Placement.Module != nullptr && Placement.Module->HasEffectiveRole(ELayoutModuleRole::VerticalAccess))
		{
			StairCell = Placement.Cell;
			bFoundStair = true;
			break;
		}
	}

	TestTrue(TEXT("The solve placed a vertical-access stair module"), bFoundStair);
	if (!bFoundStair)
	{
		return false;
	}

	const FIntVector UpperContinuationCell = StairCell + FIntVector(0, 0, 1);
	const FLayoutPlacedModule* UpperPlacement = Result.Placements.FindByPredicate([&UpperContinuationCell](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell == UpperContinuationCell;
	});

	TestNotNull(TEXT("A stair with RequiresWalkableFilledNeighbor on PosZ has a filled upper continuation cell"), UpperPlacement);
	TestTrue(
		TEXT("The upper continuation placement exposes the active upper walkable area"),
		UpperPlacement != nullptr
		&& UpperPlacement->Module != nullptr
		&& UpperPlacement->Module->GetEffectiveTraversalChannels().HasTagExact(LayoutGameplayTags::TraversalPrimary));
	TestFalse(
		TEXT("The upper continuation is a landing/cap, not another upward stair on the top level"),
		UpperPlacement != nullptr
		&& UpperPlacement->Module != nullptr
		&& UpperPlacement->Module->HasEffectiveRole(ELayoutModuleRole::VerticalAccess));
	return true;
}





IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverAllowsRequiredTopSupportFromUpperLevelTest,
	"PorismExtension.Layout.Solver.Profile.AllowsRequiredTopSupportFromUpperLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverAllowsRequiredTopSupportFromUpperLevelTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_RequiredTopSupport"), FIntPoint(3, 3), FIntPoint(3, 3), 2, 0, false);
	Profile->EntryCountMode = ELayoutCountConstraintMode::None;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 257);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Required top support solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Lower modules with PosZ RequiresFilledNeighbor can be satisfied by generated upper-level cap cells"), Result.bSucceeded);
	TestTrue(TEXT("The lower level uses the support module that requires a filled upper neighbor"), Result.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell.Z == 0
			&& Placement.Module != nullptr
			&& GetNameSafe(Placement.Module) == FString(TEXT("LayoutModule_VerticalSupportLower"));
	}));
	TestTrue(TEXT("The upper level uses cap modules that satisfy the lower support faces"), Result.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell.Z == 1
			&& Placement.Module != nullptr
			&& GetNameSafe(Placement.Module) == FString(TEXT("LayoutModule_VerticalSupportCap"));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverRejectsIncompatibleRequiredTopSupportCapTest,
	"PorismExtension.Layout.Solver.Profile.RejectsIncompatibleRequiredTopSupportCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverRejectsIncompatibleRequiredTopSupportCapTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_IncompatibleTopSupport"), FIntPoint(3, 3), FIntPoint(3, 3), 2, 0, false);
	Profile->EntryCountMode = ELayoutCountConstraintMode::None;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 259);

	TestFalse(TEXT("Merged vertical-region audit rejects an upper cap whose NegZ tags do not satisfy the lower PosZ RequiresFilledNeighbor face"), Result.bSucceeded);
	TestFalse(TEXT("The incompatible required top support solve reports a concrete failure reason"), Result.FailureReason.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverDoesNotUseExternalPlannedLowerCellForStairHoleTest,
	"PorismExtension.Layout.Solver.Profile.DoesNotUseExternalPlannedLowerCellForStairHole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverDoesNotUseExternalPlannedLowerCellForStairHoleTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(8, 8, 8);
	UChunkStructureTemplate* FloorTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_StairHoleExternalFloor"), CellSize);
	UChunkStructureTemplate* StairHoleTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_StairHoleExternalHole"), CellSize);
	UChunkStructureTemplate* StairTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_StairHoleExternalStair"), CellSize);
	const FGameplayTagContainer WalkwayTags = MakeTags({LayoutGameplayTags::FaceOpen});
	const FGameplayTagContainer UpperWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});

	const TArray<ELayoutCellIntent> AllCommonIntents = {
		ELayoutCellIntent::Boundary,
		ELayoutCellIntent::Core,
		ELayoutCellIntent::Interior
	};

	ULayoutModuleAsset* Floor = CreateModule(
		Outer,
		TEXT("LayoutModule_StairHoleExternalFloor"),
		FloorTemplate,
		AllCommonIntents,
		BuildFilledCubeFaces(
			WalkwayTags,
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			WalkwayTags,
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));

	ULayoutModuleAsset* StairHole = CreateModule(
		Outer,
		TEXT("LayoutModule_StairHoleExternalHole"),
		StairHoleTemplate,
		AllCommonIntents,
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, UpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegX, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, UpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, UpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegY, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, UpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosZ, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, WalkwayTags, WalkwayTags, ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor, UpperWalkable)
		},
		UpperWalkable);

	ULayoutModuleAsset* Stair = CreateModule(
		Outer,
		TEXT("LayoutModule_StairHoleExternalStair"),
		StairTemplate,
		{ELayoutCellIntent::VerticalAccess},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, UpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegX, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, UpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, UpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegY, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, UpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosZ, WalkwayTags, WalkwayTags, ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor, UpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegZ, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		UpperWalkable,
		{});

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_StairHoleExternal"), FIntPoint(3, 3), FIntPoint(3, 3), 2, 0, false);
	Profile->EntryCountMode = ELayoutCountConstraintMode::None;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 261);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Stair-hole external planned cell solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Solve succeeds by using the normal floor instead of invalid upper stair holes"), Result.bSucceeded);
	TestFalse(TEXT("A stair-hole module with NegZ RequiresWalkableFilledNeighbor is not satisfied by a merely planned lower cell"), Result.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell.Z > 0
			&& Placement.Module != nullptr
			&& GetNameSafe(Placement.Module) == FString(TEXT("LayoutModule_StairHoleExternalHole"));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverDoesNotPlaceTopOnlyModuleBelowPlannedUpperLevelTest,
	"PorismExtension.Layout.Solver.Profile.DoesNotPlaceTopOnlyModuleBelowPlannedUpperLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverDoesNotPlaceTopOnlyModuleBelowPlannedUpperLevelTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(8, 8, 8);
	UChunkStructureTemplate* WallTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_TopOnlyWall"), CellSize);
	UChunkStructureTemplate* BattlementTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_TopOnlyBattlement"), CellSize);
	const FGameplayTagContainer ExteriorTags = MakeTags({LayoutGameplayTags::FaceSolid});
	const TArray<ELayoutCellIntent> AllCommonIntents = {
		ELayoutCellIntent::Boundary,
		ELayoutCellIntent::Core,
		ELayoutCellIntent::Interior
	};

	ULayoutModuleAsset* Wall = CreateModule(
		Outer,
		TEXT("LayoutModule_TopOnlyWall"),
		WallTemplate,
		AllCommonIntents,
		BuildFilledCubeFaces(
			ExteriorTags,
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			ExteriorTags,
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));

	ULayoutModuleAsset* Battlement = CreateModule(
		Outer,
		TEXT("LayoutModule_TopOnlyBattlement"),
		BattlementTemplate,
		AllCommonIntents,
		BuildFilledCubeFaces(
			ExteriorTags,
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			ExteriorTags,
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_TopOnlyBattlement"), FIntPoint(3, 3), FIntPoint(3, 3), 2, 0, false);
	Profile->EntryCountMode = ELayoutCountConstraintMode::None;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 263);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Top-only battlement solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Solve succeeds with ordinary walls below and top-only modules where vertical empty space exists"), Result.bSucceeded);
	TestFalse(TEXT("A module with PosZ RequiresEmptyNeighbor is not placed below a planned upper-level cell"), Result.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell.Z == 0
			&& Placement.Module != nullptr
			&& GetNameSafe(Placement.Module) == FString(TEXT("LayoutModule_TopOnlyBattlement"));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverSupportsMixedUpperAndRoofCellsWithContinuationTest,
	"PorismExtension.Layout.Solver.Profile.SupportsMixedUpperAndRoofCellsWithContinuation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverSupportsMixedUpperAndRoofCellsWithContinuationTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(8, 8, 8);
	UChunkStructureTemplate* UpperStairTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_MixedUpperRoofStair"), CellSize);
	UChunkStructureTemplate* RoofCapTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_MixedUpperRoofCap"), CellSize);
	UChunkStructureTemplate* RoofDeckTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_MixedUpperRoofDeck"), CellSize);
	const FGameplayTagContainer WalkwayTags = MakeTags({LayoutGameplayTags::FaceOpen});
	const FGameplayTagContainer UpperWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});
	const FGameplayTagContainer RoofWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});
	FGameplayTagContainer UpperAndRoofWalkable = UpperWalkable;
	UpperAndRoofWalkable.AppendTags(RoofWalkable);

	FLayoutInternalAccessLink UpperToRoof;
	UpperToRoof.FromTraversalChannel = LayoutGameplayTags::TraversalPrimary;
	UpperToRoof.ToTraversalChannel = LayoutGameplayTags::TraversalPrimary;
	UpperToRoof.bBidirectional = true;

	ULayoutModuleAsset* UpperStair = CreateModule(
		Outer,
		TEXT("LayoutModule_MixedUpperRoofStair"),
		UpperStairTemplate,
		{ELayoutCellIntent::VerticalAccess},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, UpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegX, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, UpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, UpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegY, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, UpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosZ, WalkwayTags, WalkwayTags, ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor, RoofWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegZ, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		UpperAndRoofWalkable,
		{UpperToRoof});

	ULayoutModuleAsset* RoofCap = CreateModule(
		Outer,
		TEXT("LayoutModule_MixedUpperRoofCap"),
		RoofCapTemplate,
		{ELayoutCellIntent::Interior},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, RoofWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegX, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, RoofWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, RoofWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegY, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, RoofWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosZ, WalkwayTags, FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, WalkwayTags, WalkwayTags, ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor, RoofWalkable)
		},
		RoofWalkable);

	ULayoutModuleAsset* RoofDeck = CreateModule(
		Outer,
		TEXT("LayoutModule_MixedUpperRoofDeck"),
		RoofDeckTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			WalkwayTags,
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			WalkwayTags,
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor,
			RoofWalkable),
		RoofWalkable);

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_MixedUpperRoof"), FIntPoint(2, 1), FIntPoint(2, 1), 3, 0, false);

	TArray<FLayoutPlannedCell> PlannedCells;
	FLayoutPlannedCell& UpperStairCell = PlannedCells.AddDefaulted_GetRef();
	UpperStairCell.Cell = FIntVector(0, 0, 1);
	UpperStairCell.Intent = ELayoutCellIntent::VerticalAccess;
	FLayoutPlannedCell& SameLevelRoofCell = PlannedCells.AddDefaulted_GetRef();
	SameLevelRoofCell.Cell = FIntVector(1, 0, 1);
	SameLevelRoofCell.Intent = ELayoutCellIntent::Boundary;
	FLayoutPlannedCell& ContinuedRoofCell = PlannedCells.AddDefaulted_GetRef();
	ContinuedRoofCell.Cell = FIntVector(0, 0, 2);
	ContinuedRoofCell.Intent = ELayoutCellIntent::Interior;

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 263, FIntPoint(2, 1), PlannedCells);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Mixed upper/roof continuation solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Solver supports a level with both active upper continuation and separate roof cells"), Result.bSucceeded);
	TestTrue(TEXT("The upper area continues into a roof cap above it"), Result.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell == FIntVector(0, 0, 2)
			&& Placement.Module != nullptr
			&& GetNameSafe(Placement.Module) == FString(TEXT("LayoutModule_MixedUpperRoofCap"));
	}));
	TestTrue(TEXT("The same intermediate level can also contain a separate roof/deck cell"), Result.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell == FIntVector(1, 0, 1)
			&& Placement.Module != nullptr
			&& GetNameSafe(Placement.Module) == FString(TEXT("LayoutModule_MixedUpperRoofDeck"));
	}));
	return true;
}





IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverSolvesUpperLevelsAsVerticalContinuationRegionsTest,
	"PorismExtension.Layout.Solver.Profile.SolvesUpperLevelsAsVerticalContinuationRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverSolvesUpperLevelsAsVerticalContinuationRegionsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_VerticalRegionContinuation"), FIntPoint(3, 3), FIntPoint(3, 3), 2, 1, true);
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 1;
	Profile->bRestrictVerticalAccessModulesToVerticalAccessCells = true;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 113);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Vertical region continuation solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Vertical region continuation solve succeeds"), Result.bSucceeded);

	FIntVector VerticalAccessCell = FIntVector::ZeroValue;
	bool bFoundVerticalAccessCell = false;
	for (const FLayoutPlannedCell& PlannedCell : Result.PlannedCells)
	{
		if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
		{
			VerticalAccessCell = PlannedCell.Cell;
			bFoundVerticalAccessCell = true;
			break;
		}
	}

	TestTrue(TEXT("Planner emitted a lower-level vertical-access transition cell"), bFoundVerticalAccessCell);
	if (!bFoundVerticalAccessCell)
	{
		return false;
	}

	const FIntVector UpperContinuationCell(VerticalAccessCell.X, VerticalAccessCell.Y, VerticalAccessCell.Z + 1);
	const FLayoutPlacedModule* LowerPlacement = nullptr;
	const FLayoutPlacedModule* UpperPlacement = nullptr;
	for (const FLayoutPlacedModule& Placement : Result.Placements)
	{
		if (Placement.Cell == VerticalAccessCell)
		{
			LowerPlacement = &Placement;
		}
		else if (Placement.Cell == UpperContinuationCell)
		{
			UpperPlacement = &Placement;
		}
	}

	TestNotNull(TEXT("Lower vertical-access cell was filled"), LowerPlacement);
	TestNotNull(TEXT("Upper continuation cell was filled"), UpperPlacement);
	if (LowerPlacement == nullptr || UpperPlacement == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Lower vertical-access cell uses the stair module"), LowerPlacement->Module != nullptr && LowerPlacement->Module->HasEffectiveRole(ELayoutModuleRole::VerticalAccess));
	TestFalse(TEXT("Upper continuation cell is not forced to use an upward vertical-access module"), UpperPlacement->Module != nullptr && UpperPlacement->Module->HasEffectiveRole(ELayoutModuleRole::VerticalAccess));
	TestEqual(TEXT("Upper continuation cell consumes the stair with a landing module"), GetNameSafe(UpperPlacement->Module), FString(TEXT("LayoutModule_VerticalRegionLanding")));

	int32 LowerHorizontalRouteFaceRequirementCount = 0;
	for (const FLayoutRouteConstraintRecord& RouteConstraint : Result.RouteConstraints)
	{
		if (RouteConstraint.Cell != VerticalAccessCell)
		{
			continue;
		}

		for (const FLayoutRouteFaceRequirement& Requirement : RouteConstraint.FaceRequirements)
		{
			if (Requirement.FaceDirection == ELayoutFaceDirection::PosX
				|| Requirement.FaceDirection == ELayoutFaceDirection::NegX
				|| Requirement.FaceDirection == ELayoutFaceDirection::PosY
				|| Requirement.FaceDirection == ELayoutFaceDirection::NegY)
			{
				++LowerHorizontalRouteFaceRequirementCount;
			}
		}
	}

	TestTrue(
		TEXT("Lower vertical-access transition cell keeps at most one horizontal route-face requirement"),
		LowerHorizontalRouteFaceRequirementCount <= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverLimitsFutureVerticalBoundaryToAccessColumnsTest,
	"PorismExtension.Layout.Solver.Profile.LimitsFutureVerticalBoundaryToAccessColumns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverLimitsFutureVerticalBoundaryToAccessColumnsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_VerticalBoundaryScope"), FIntPoint(3, 3), FIntPoint(3, 3), 2, 1, true);
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 1;
	Profile->bRestrictVerticalAccessModulesToVerticalAccessCells = true;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 127);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Vertical boundary scope solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Ground-floor cells with strict empty tops do not see unrelated upper-level cells as future vertical boundaries"), Result.bSucceeded);

	bool bFoundGroundCell = false;
	for (const FLayoutPlacedModule& Placement : Result.Placements)
	{
		if (Placement.Cell.Z == 0
			&& Placement.Intent != ELayoutCellIntent::VerticalAccess
			&& Placement.Module != nullptr
			&& GetNameSafe(Placement.Module) == FString(TEXT("LayoutModule_VerticalBoundaryScopeGround")))
		{
			bFoundGroundCell = true;
			break;
		}
	}

	TestTrue(TEXT("At least one non-access ground cell used the strict-empty-top ground module"), bFoundGroundCell);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverUsesVerticalContinuationAsUpperReachabilityRootTest,
	"PorismExtension.Layout.Solver.Profile.UsesVerticalContinuationAsUpperReachabilityRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverUsesVerticalContinuationAsUpperReachabilityRootTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_VerticalRegionReachabilityRoot"), FIntPoint(3, 3), FIntPoint(3, 3), 2, 1, true);
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 1;
	Profile->bRestrictVerticalAccessModulesToVerticalAccessCells = true;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 113);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Vertical continuation reachability-root solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Upper floor uses the incoming vertical continuation as its reachability root"), Result.bSucceeded);

	bool bFoundUpperLanding = false;
	for (const FLayoutPlacedModule& Placement : Result.Placements)
	{
		if (Placement.Cell.Z == 1
			&& Placement.Module != nullptr
			&& GetNameSafe(Placement.Module) == FString(TEXT("LayoutModule_VerticalRegionReachableLanding")))
		{
			bFoundUpperLanding = true;
			break;
		}
	}

	TestTrue(TEXT("Upper continuation cell accepted a landing connected to the lower stair"), bFoundUpperLanding);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverBridgesVerticalTransitionToRequiredUpperWalkableAreaTest,
	"PorismExtension.Layout.Solver.Profile.BridgesVerticalTransitionToRequiredUpperWalkableArea",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverBridgesVerticalTransitionToRequiredUpperWalkableAreaTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_VerticalTransitionBridge"), FIntPoint(3, 3), FIntPoint(3, 3), 2, 1, true);
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 1;
	Profile->bRestrictVerticalAccessModulesToVerticalAccessCells = true;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 197);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Vertical transition bridge solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("A lower stair transition walkable area can seed the required upper-floor walkable area through a landing internal access link"), Result.bSucceeded);

	bool bFoundUpperBridgeLanding = false;
	for (const FLayoutPlacedModule& Placement : Result.Placements)
	{
		if (Placement.Cell.Z == 1
			&& Placement.Module != nullptr
			&& GetNameSafe(Placement.Module) == FString(TEXT("LayoutModule_VerticalBridgeLanding")))
		{
			bFoundUpperBridgeLanding = true;
			break;
		}
	}

	TestTrue(TEXT("Upper continuation cell consumed the lower stair transition with a bridge landing"), bFoundUpperBridgeLanding);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverUsesContextualReachabilityInsteadOfProfileIgnoreTagsTest,
	"PorismExtension.Layout.Solver.Profile.UsesContextualReachabilityInsteadOfProfileIgnoreTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverUsesContextualReachabilityInsteadOfProfileIgnoreTagsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_ContextualReachability"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 1, true);

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 5);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Contextual reachability solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Contextual reachability derives active walkability from entry roots without profile ignore tags"), Result.bSucceeded);
	for (const FLayoutValidationMessage& Message : Result.Messages)
	{
		TestFalse(TEXT("Inactive reusable walkable metadata does not emit unreachable warnings"), Message.Message.Contains(TEXT("unreachable")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverAllowsAnyNeighborForGroundAndUpperFloorsTest,
	"PorismExtension.Layout.Solver.Profile.AllowsAnyNeighborForGroundAndUpperFloors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverAllowsAnyNeighborForGroundAndUpperFloorsTest::RunTest(const FString& Parameters)
{

	ULayoutProfileAsset* GroundProfile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_AnyNeighborGround"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 1, true);
	const FLayoutSolveResult GroundResult = FLayoutProfileSolver::Solve(GroundProfile, 31);
	if (!GroundResult.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("AllowsAnyNeighbor ground-floor solve failed: %s"), *GroundResult.FailureReason));
	}

	TestTrue(TEXT("AllowsAnyNeighbor can place a ground-floor cell with empty space below and around it"), GroundResult.bSucceeded);
	TestEqual(TEXT("Ground-floor solve places one module"), GroundResult.Placements.Num(), 1);

	ULayoutProfileAsset* StackedProfile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_AnyNeighborStacked"), FIntPoint(1, 1), FIntPoint(1, 1), 2, 1, true);
	const FLayoutSolveResult StackedResult = FLayoutProfileSolver::Solve(StackedProfile, 31);
	if (!StackedResult.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("AllowsAnyNeighbor stacked-floor solve failed: %s"), *StackedResult.FailureReason));
	}

	TestTrue(TEXT("AllowsAnyNeighbor can place the same module above another filled module"), StackedResult.bSucceeded);
	TestEqual(TEXT("Stacked solve places both floor levels"), StackedResult.Placements.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverUsesStrictForwardCheckedEntryPairTest,
	"PorismExtension.Layout.Solver.Profile.UsesStrictForwardCheckedEntryPair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverUsesStrictForwardCheckedEntryPairTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(8, 8, 8);
	UChunkStructureTemplate* DoorTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_StrictDoor"), CellSize);
	UChunkStructureTemplate* OpenTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_StrictOpen"), CellSize);
	UChunkStructureTemplate* ClosedTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_StrictClosed"), CellSize);

	ULayoutModuleAsset* Door = CreateModule(
		Outer,
		TEXT("LayoutModule_StrictDoor"),
		DoorTemplate,
		{ELayoutCellIntent::Entry},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceEntry}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});

	ULayoutModuleAsset* OpenBoundary = CreateModule(
		Outer,
		TEXT("LayoutModule_StrictOpen"),
		OpenTemplate,
		{ELayoutCellIntent::Boundary},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});

	ULayoutModuleAsset* ClosedBoundary = CreateModule(
		Outer,
		TEXT("LayoutModule_StrictClosed"),
		ClosedTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceSolid}),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_StrictForwardEntry"), FIntPoint(2, 1), FIntPoint(2, 1), 1, 1, false);

	TArray<FLayoutPlannedCell> PlannedCells;
	FLayoutPlannedCell& EntryCell = PlannedCells.AddDefaulted_GetRef();
	EntryCell.Cell = FIntVector(0, 0, 0);
	EntryCell.Intent = ELayoutCellIntent::Entry;
	FLayoutPlannedCell& BoundaryCell = PlannedCells.AddDefaulted_GetRef();
	BoundaryCell.Cell = FIntVector(1, 0, 0);
	BoundaryCell.Intent = ELayoutCellIntent::Boundary;

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 83, FIntPoint(2, 1), PlannedCells);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Strict forward-checked solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("CSP solver finds the strict open-neighbor pairing"), Result.bSucceeded);

	bool bPlacedOpenBoundary = false;
	for (const FLayoutPlacedModule& Placement : Result.Placements)
	{
		bPlacedOpenBoundary |= Placement.Cell == FIntVector(1, 0, 0) && Placement.Module == OpenBoundary;
	}

	TestTrue(TEXT("Boundary cell uses the compatible open module, not the incompatible closed module"), bPlacedOpenBoundary);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverUsesExteriorEntryFaceAsReachabilityRootTest,
	"PorismExtension.Layout.Solver.Profile.UsesExteriorEntryFaceAsReachabilityRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverUsesExteriorEntryFaceAsReachabilityRootTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(8, 8, 8);
	const FGameplayTagContainer GroundWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});
	UChunkStructureTemplate* DoorTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_ExteriorRootDoor"), CellSize);
	UChunkStructureTemplate* FloorTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_ExteriorRootFloor"), CellSize);

	ULayoutModuleAsset* Door = CreateModule(
		Outer,
		TEXT("LayoutModule_ExteriorRootDoor"),
		DoorTemplate,
		{ELayoutCellIntent::Entry},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceEntry}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		GroundWalkable);

	ULayoutModuleAsset* Floor = CreateModule(
		Outer,
		TEXT("LayoutModule_ExteriorRootFloor"),
		FloorTemplate,
		{ELayoutCellIntent::Boundary},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		GroundWalkable);

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_ExteriorEntryRoot"), FIntPoint(2, 1), FIntPoint(2, 1), 1, 1, false);

	TArray<FLayoutPlannedCell> PlannedCells;
	FLayoutPlannedCell& EntryCell = PlannedCells.AddDefaulted_GetRef();
	EntryCell.Cell = FIntVector(0, 0, 0);
	EntryCell.Intent = ELayoutCellIntent::Entry;
	FLayoutPlannedCell& BoundaryCell = PlannedCells.AddDefaulted_GetRef();
	BoundaryCell.Cell = FIntVector(1, 0, 0);
	BoundaryCell.Intent = ELayoutCellIntent::Boundary;

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 101, FIntPoint(2, 1), PlannedCells);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Exterior entry root solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Explicit outside-facing entry face can root layout reachability"), Result.bSucceeded);
	TestFalse(TEXT("Explicit exterior entry root avoids fallback or unreachable warnings"), Result.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("unreachable from the authored entry root"))
			|| Message.Message.Contains(TEXT("no explicit outside-facing"));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverRequiresOnlyOneSharedWalkableFaceTagTest,
	"PorismExtension.Layout.Solver.Profile.RequiresOnlyOneSharedWalkableFaceTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverAcceptsBridgedExteriorEntryRootTest,
	"PorismExtension.Layout.Solver.Profile.AcceptsBridgedExteriorEntryRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverAcceptsBridgedExteriorEntryRootTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(8, 8, 8);
	const FGameplayTagContainer GroundWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});
	UChunkStructureTemplate* DoorTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_BridgedExteriorEntryRootDoor"), CellSize);
	UChunkStructureTemplate* FloorTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_BridgedExteriorEntryRootFloor"), CellSize);

	ULayoutModuleAsset* Door = CreateModule(
		Outer,
		TEXT("LayoutModule_BridgedExteriorEntryRootDoor"),
		DoorTemplate,
		{ELayoutCellIntent::Entry},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor, GroundWalkable),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		GroundWalkable);

	ULayoutModuleAsset* Floor = CreateModule(
		Outer,
		TEXT("LayoutModule_BridgedExteriorEntryRootFloor"),
		FloorTemplate,
		{ELayoutCellIntent::Boundary},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}), ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		GroundWalkable);

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_BridgedExteriorEntryRoot"), FIntPoint(2, 1), FIntPoint(2, 1), 1, 1, false);

	TArray<FLayoutPlannedCell> PlannedCells;
	FLayoutPlannedCell& EntryCell = PlannedCells.AddDefaulted_GetRef();
	EntryCell.Cell = FIntVector(0, 0, 0);
	EntryCell.Intent = ELayoutCellIntent::Entry;
	FLayoutPlannedCell& BoundaryCell = PlannedCells.AddDefaulted_GetRef();
	BoundaryCell.Cell = FIntVector(1, 0, 0);
	BoundaryCell.Intent = ELayoutCellIntent::Boundary;

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 102, FIntPoint(2, 1), PlannedCells);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Bridged exterior entry root solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Bridged exterior opening can root a standalone room entry solve"), Result.bSucceeded);
	TestFalse(TEXT("Bridged exterior entry root avoids authored-entry-root reachability warnings"), Result.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("unreachable from the authored entry root"));
	}));
	return true;
}

bool FLayoutProfileSolverRequiresOnlyOneSharedWalkableFaceTagTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(8, 8, 8);
	const FGameplayTagContainer GroundAndUpperWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});
	const FGameplayTagContainer UpperWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});
	UChunkStructureTemplate* LeftTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_WalkableRequiredLeft"), CellSize);
	UChunkStructureTemplate* RightTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_WalkableRequiredRight"), CellSize);

	ULayoutModuleAsset* Left = CreateModule(
		Outer,
		TEXT("LayoutModule_WalkableRequiredLeft"),
		LeftTemplate,
		{ELayoutCellIntent::Entry},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor, GroundAndUpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		GroundAndUpperWalkable);

	ULayoutModuleAsset* Right = CreateModule(
		Outer,
		TEXT("LayoutModule_WalkableRequiredRight"),
		RightTemplate,
		{ELayoutCellIntent::Boundary},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor, UpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		UpperWalkable);

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_OneSharedWalkable"), FIntPoint(2, 1), FIntPoint(2, 1), 1, 0, false);

	TArray<FLayoutPlannedCell> PlannedCells;
	FLayoutPlannedCell& LeftCell = PlannedCells.AddDefaulted_GetRef();
	LeftCell.Cell = FIntVector(0, 0, 0);
	LeftCell.Intent = ELayoutCellIntent::Entry;
	FLayoutPlannedCell& RightCell = PlannedCells.AddDefaulted_GetRef();
	RightCell.Cell = FIntVector(1, 0, 0);
	RightCell.Intent = ELayoutCellIntent::Boundary;

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 181, FIntPoint(2, 1), PlannedCells);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("One-shared-walkable solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Walkable-filled policies require one shared ConnectedTraversalChannels tag, not all tags"), Result.bSucceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverCountsOptionalWalkableFilledFacesTowardModuleMinimumTest,
	"PorismExtension.Layout.Solver.Profile.CountsOptionalWalkableFilledFacesTowardModuleMinimum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverCountsOptionalWalkableFilledFacesTowardModuleMinimumTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(8, 8, 8);
	const FGameplayTagContainer GroundWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});
	UChunkStructureTemplate* LeftTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_MinWalkableLeft"), CellSize);
	UChunkStructureTemplate* RightTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_MinWalkableRight"), CellSize);

	ULayoutModuleAsset* Left = CreateModule(
		Outer,
		TEXT("LayoutModule_MinWalkableLeft"),
		LeftTemplate,
		{ELayoutCellIntent::Entry},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		GroundWalkable);
	Left->MinWalkableFaces = 1;

	ULayoutModuleAsset* Right = CreateModule(
		Outer,
		TEXT("LayoutModule_MinWalkableRight"),
		RightTemplate,
		{ELayoutCellIntent::Boundary},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		GroundWalkable);

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_MinWalkable"), FIntPoint(2, 1), FIntPoint(2, 1), 1, 0, false);

	TArray<FLayoutPlannedCell> PlannedCells;
	FLayoutPlannedCell& LeftCell = PlannedCells.AddDefaulted_GetRef();
	LeftCell.Cell = FIntVector(0, 0, 0);
	LeftCell.Intent = ELayoutCellIntent::Entry;
	FLayoutPlannedCell& RightCell = PlannedCells.AddDefaulted_GetRef();
	RightCell.Cell = FIntVector(1, 0, 0);
	RightCell.Intent = ELayoutCellIntent::Boundary;

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 211, FIntPoint(2, 1), PlannedCells);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("MinWalkableFaces solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("RequiredFilledNeighbor with shared optional walkability counts toward MinWalkableFaces"), Result.bSucceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverAllowsEmptyOrWalkableFilledRequiresOneSharedWalkableTagTest,
	"PorismExtension.Layout.Solver.Profile.AllowsEmptyOrWalkableFilledRequiresOneSharedWalkableTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverAllowsEmptyOrWalkableFilledRequiresOneSharedWalkableTagTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(8, 8, 8);
	const FGameplayTagContainer GroundAndUpperWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});
	const FGameplayTagContainer UpperWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});
	UChunkStructureTemplate* LeftTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_WalkableOptionalLeft"), CellSize);
	UChunkStructureTemplate* RightTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_WalkableOptionalRight"), CellSize);

	ULayoutModuleAsset* Left = CreateModule(
		Outer,
		TEXT("LayoutModule_WalkableOptionalLeft"),
		LeftTemplate,
		{ELayoutCellIntent::Entry},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor, GroundAndUpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		GroundAndUpperWalkable);

	ULayoutModuleAsset* Right = CreateModule(
		Outer,
		TEXT("LayoutModule_WalkableOptionalRight"),
		RightTemplate,
		{ELayoutCellIntent::Boundary},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, UpperWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		UpperWalkable);

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_OptionalOneSharedWalkable"), FIntPoint(2, 1), FIntPoint(2, 1), 1, 0, false);

	TArray<FLayoutPlannedCell> PlannedCells;
	FLayoutPlannedCell& LeftCell = PlannedCells.AddDefaulted_GetRef();
	LeftCell.Cell = FIntVector(0, 0, 0);
	LeftCell.Intent = ELayoutCellIntent::Entry;
	FLayoutPlannedCell& RightCell = PlannedCells.AddDefaulted_GetRef();
	RightCell.Cell = FIntVector(1, 0, 0);
	RightCell.Intent = ELayoutCellIntent::Boundary;

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 187, FIntPoint(2, 1), PlannedCells);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Allows-empty-or-walkable-filled solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("AllowsEmptyOrWalkableFilledNeighbor requires one shared walkable tag when the neighbor is filled"), Result.bSucceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverGrowsRequiredWalkableAreaFromEntryRootTest,
	"PorismExtension.Layout.Solver.Profile.GrowsRequiredWalkableAreaFromEntryRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverGrowsRequiredWalkableAreaFromEntryRootTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(8, 8, 8);
	const FGameplayTagContainer GroundWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});
	UChunkStructureTemplate* DoorTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_FrontierDoor"), CellSize);
	UChunkStructureTemplate* ConnectedFloorTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_FrontierConnectedFloor"), CellSize);
	UChunkStructureTemplate* IsolatedFloorTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_FrontierIsolatedFloor"), CellSize);

	ULayoutModuleAsset* Door = CreateModule(
		Outer,
		TEXT("LayoutModule_FrontierDoor"),
		DoorTemplate,
		{ELayoutCellIntent::Entry},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceEntry}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		GroundWalkable);

	ULayoutModuleAsset* ConnectedFloor = CreateModule(
		Outer,
		TEXT("LayoutModule_FrontierConnectedFloor"),
		ConnectedFloorTemplate,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Core, ELayoutCellIntent::Interior},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		GroundWalkable);

	ULayoutModuleAsset* IsolatedFloor = CreateModule(
		Outer,
		TEXT("LayoutModule_FrontierIsolatedFloor"),
		IsolatedFloorTemplate,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Core, ELayoutCellIntent::Interior},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		GroundWalkable);

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_FrontierReachability"), FIntPoint(5, 5), FIntPoint(5, 5), 1, 1, false);

	TArray<FLayoutPlannedCell> PlannedCells;
	for (int32 Y = 0; Y < 5; ++Y)
	{
		for (int32 X = 0; X < 5; ++X)
		{
			FLayoutPlannedCell& Cell = PlannedCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			if (X == 0 && Y == 2)
			{
				Cell.Intent = ELayoutCellIntent::Entry;
			}
			else if (X == 2 && Y == 2)
			{
				Cell.Intent = ELayoutCellIntent::Core;
			}
			else if (X == 0 || X == 4 || Y == 0 || Y == 4)
			{
				Cell.Intent = ELayoutCellIntent::Boundary;
			}
			else
			{
				Cell.Intent = ELayoutCellIntent::Interior;
			}
		}
	}

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 173, FIntPoint(5, 5), PlannedCells);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Frontier reachability solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Solver grows the required walkable area from the entry root across a 5x5 footprint"), Result.bSucceeded);
	TestFalse(TEXT("Disconnected higher-weight walkable candidates are rejected during the solve"), Result.Placements.ContainsByPredicate([IsolatedFloor](const FLayoutPlacedModule& Placement)
	{
		return Placement.Module == IsolatedFloor;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverAllowsShellAndFloorInteriorWithRequiredReachabilityTest,
	"PorismExtension.Layout.Solver.Profile.AllowsShellAndFloorInteriorWithRequiredReachability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverAllowsShellAndFloorInteriorWithRequiredReachabilityTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(8, 8, 8);
	const FGameplayTagContainer GroundWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});
	const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});

	UChunkStructureTemplate* DoorTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_ShellFloorDoor"), CellSize);
	UChunkStructureTemplate* ShellTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_ShellFloorShell"), CellSize);
	UChunkStructureTemplate* FloorTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_ShellFloorInterior"), CellSize);

	ULayoutModuleAsset* Door = CreateModule(
		Outer,
		TEXT("LayoutModule_ShellFloorDoor"),
		DoorTemplate,
		{ELayoutCellIntent::Entry},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, OpenTags, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegX, OpenTags, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, OpenTags, OpenTags, ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceEntry}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		GroundWalkable);

	ULayoutModuleAsset* Shell = CreateModule(
		Outer,
		TEXT("LayoutModule_ShellFloorShell"),
		ShellTemplate,
		{ELayoutCellIntent::Boundary},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, OpenTags, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegX, OpenTags, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, OpenTags, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegY, OpenTags, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		GroundWalkable);

	ULayoutModuleAsset* Floor = CreateModule(
		Outer,
		TEXT("LayoutModule_ShellFloorInterior"),
		FloorTemplate,
		{ELayoutCellIntent::Core, ELayoutCellIntent::Interior},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, OpenTags, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegX, OpenTags, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosY, OpenTags, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegY, OpenTags, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		GroundWalkable);

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_ShellFloor"), FIntPoint(5, 5), FIntPoint(5, 5), 1, 1, false);

	TArray<FLayoutPlannedCell> PlannedCells;
	for (int32 Y = 0; Y < 5; ++Y)
	{
		for (int32 X = 0; X < 5; ++X)
		{
			FLayoutPlannedCell& Cell = PlannedCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			if (X == 2 && Y == 0)
			{
				Cell.Intent = ELayoutCellIntent::Entry;
			}
			else if (X == 2 && Y == 2)
			{
				Cell.Intent = ELayoutCellIntent::Core;
			}
			else if (X == 0 || X == 4 || Y == 0 || Y == 4)
			{
				Cell.Intent = ELayoutCellIntent::Boundary;
			}
			else
			{
				Cell.Intent = ELayoutCellIntent::Interior;
			}
		}
	}

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 223, FIntPoint(5, 5), PlannedCells);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Shell plus floor solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("A shell layout with plain reachable interior floor cells should solve without extra modules"), Result.bSucceeded);
	TestEqual(TEXT("Every planned shell and floor cell receives a placement"), Result.Placements.Num(), PlannedCells.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverRejectsOpenOnlyFaceAgainstExteriorTest,
	"PorismExtension.Layout.Solver.Profile.RejectsOpenOnlyFaceAgainstExterior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverRejectsOpenOnlyFaceAgainstExteriorTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(8, 8, 8);
	UChunkStructureTemplate* OpenOnlyTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_OpenOnlyWall"), CellSize);
	UChunkStructureTemplate* ExteriorTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_ExteriorWall"), CellSize);

	ULayoutModuleAsset* OpenOnlyWall = CreateModule(
		Outer,
		TEXT("LayoutModule_OpenOnlyWall"),
		OpenOnlyTemplate,
		{ELayoutCellIntent::Entry},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});

	ULayoutModuleAsset* ExteriorWall = CreateModule(
		Outer,
		TEXT("LayoutModule_ExteriorWall"),
		ExteriorTemplate,
		{ELayoutCellIntent::Boundary},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_OpenOnlyRejectsExterior"), FIntPoint(2, 1), FIntPoint(2, 1), 1, 1, false);

	TArray<FLayoutPlannedCell> PlannedCells;
	FLayoutPlannedCell& LeftCell = PlannedCells.AddDefaulted_GetRef();
	LeftCell.Cell = FIntVector(0, 0, 0);
	LeftCell.Intent = ELayoutCellIntent::Entry;
	FLayoutPlannedCell& RightCell = PlannedCells.AddDefaulted_GetRef();
	RightCell.Cell = FIntVector(1, 0, 0);
	RightCell.Intent = ELayoutCellIntent::Boundary;

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 227, FIntPoint(2, 1), PlannedCells);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Open-only versus exterior solve failed as expected: %s"), *Result.FailureReason));
	}

	TestFalse(TEXT("A face that only allows Open must not accept an Exterior face through global compatibility expansion"), Result.bSucceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverRotatesSquareModulesWithoutAuthoredYawRestrictionTest,
	"PorismExtension.Layout.Solver.Profile.RotatesSquareModulesWithoutAuthoredYawRestriction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverRotatesSquareModulesWithoutAuthoredYawRestrictionTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	const FIntVector CellSize(8, 8, 8);
	UChunkStructureTemplate* DoorTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_AuthoredOnlyDoor"), CellSize);
	UChunkStructureTemplate* OpenTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_AuthoredOnlyOpen"), CellSize);

	ULayoutModuleAsset* Door = CreateModule(
		Outer,
		TEXT("LayoutModule_AuthoredOnlyDoor"),
		DoorTemplate,
		{ELayoutCellIntent::Entry},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceEntry}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});

	ULayoutModuleAsset* OpenBoundary = CreateModule(
		Outer,
		TEXT("LayoutModule_AuthoredOnlyOpen"),
		OpenTemplate,
		{ELayoutCellIntent::Boundary},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("LayoutProfile_AuthoredOnlyYaw"), FIntPoint(2, 1), FIntPoint(2, 1), 1, 1, false);

	TArray<FLayoutPlannedCell> PlannedCells;
	FLayoutPlannedCell& EntryCell = PlannedCells.AddDefaulted_GetRef();
	EntryCell.Cell = FIntVector(0, 0, 0);
	EntryCell.Intent = ELayoutCellIntent::Entry;
	FLayoutPlannedCell& BoundaryCell = PlannedCells.AddDefaulted_GetRef();
	BoundaryCell.Cell = FIntVector(1, 0, 0);
	BoundaryCell.Intent = ELayoutCellIntent::Boundary;

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 97, FIntPoint(2, 1), PlannedCells);
	if (!Result.bSucceeded)
	{
		AddInfo(FString::Printf(TEXT("Square-cell yaw-unrestricted solve failed: %s"), *Result.FailureReason));
	}

	TestTrue(TEXT("Solver may rotate square-cell modules even when legacy authored yaw mode was restrictive"), Result.bSucceeded);

	bool bFoundRotatedPlacement = false;
	for (const FLayoutPlacedModule& Placement : Result.Placements)
	{
		if ((Placement.Module == Door || Placement.Module == OpenBoundary)
			&& Placement.YawRotationSteps != 0)
		{
			bFoundRotatedPlacement = true;
			break;
		}
	}

	TestTrue(TEXT("Solve records at least one non-authored yaw placement"), bFoundRotatedPlacement);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileSolverSteppedInsufficientQualifiedEntriesFailsTest,
	"PorismExtension.Layout.Solver.Stepped.InsufficientQualifiedEntriesFails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileSolverSteppedInsufficientQualifiedEntriesFailsTest::RunTest(const FString& Parameters)
{
	// Verified: qualified candidate count below EntryCount rejects with diagnostics.

	const FIntPoint FootprintSize(3, 3);
	FLayoutRegionSolveRequest Request;
	Request.FootprintSize = FootprintSize;
	Request.Seed = 77;
	Request.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	Request.ProfileSnapshot.bSupportsSteppedTerrainSolve = true;
	Request.ProfileSnapshot.EntryCountMode = ELayoutCountConstraintMode::Exact;
	Request.ProfileSnapshot.EntryCount = 3;  // request 3, supply only 2
	Request.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::None;
	Request.bHasQualifiedEntryCells = true;
	Request.QualifiedEntryCells = { FIntVector(0, 1, 0), FIntVector(2, 1, 0) };

	Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	Request.SteppedTerrainSupportMap.MaximumObservedSnappedLevelDelta = 1;
	for (int32 Y = 0; Y < FootprintSize.Y; ++Y)
	{
		for (int32 X = 0; X < FootprintSize.X; ++X)
		{
			FLayoutSteppedTerrainSupportSample& Sample = Request.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
			Sample.LocalCell = FIntVector(X, Y, 0);
			Sample.SupportSurfaceZ = 16;
			Sample.SnappedSupportFloorZ = 0;
			Sample.SnappedSupportCeilingZ = 16;
		}
	}

	for (int32 Y = 0; Y < FootprintSize.Y; ++Y)
	{
		for (int32 X = 0; X < FootprintSize.X; ++X)
		{
			FLayoutFrozenTerrainStageCellRecord& Record = Request.PrecomputedFrozenTerrainContract.StageMap.AddDefaulted_GetRef();
			Record.FootprintCellXY = FIntPoint(X, Y);
			Record.TerrainStageIndex = 0;
		}
	}

	TArray<FLayoutPlannedCell> PlannedCells;
	for (int32 Y = 0; Y < FootprintSize.Y; ++Y)
	{
		for (int32 X = 0; X < FootprintSize.X; ++X)
		{
			FLayoutPlannedCell& Cell = PlannedCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			const bool bOnPerimeter = (X == 0 || X == FootprintSize.X - 1 || Y == 0 || Y == FootprintSize.Y - 1);
			Cell.Intent = bOnPerimeter ? ELayoutCellIntent::Boundary : ELayoutCellIntent::Interior;
		}
	}

	FString EnrichmentFailureReason;
	TestFalse(TEXT("Insufficient qualified entry supply rejects enrichment"),
		LayoutProfileSolverInternal::EnrichPlannedCellsWithSteppedTerrainIntents(PlannedCells, Request, EnrichmentFailureReason));
	TestTrue(TEXT("Failure describes qualified Z=0 capacity"), EnrichmentFailureReason.Contains(TEXT("qualified Z=0")));

	// When qualified supply is insufficient, zero entries should be assigned.
	int32 EntryCount = 0;
	for (const FLayoutPlannedCell& Cell : PlannedCells)
	{
		if (Cell.Intent == ELayoutCellIntent::Entry) ++EntryCount;
	}
	TestEqual(TEXT("Insufficient qualified entry supply produces zero entry intents"), EntryCount, 0);
	return true;
}
