// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
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
#include "UObject/UnrealType.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;

	/** Adds one typed sparse-placement rule and returns its concrete payload. */
	template <typename RuleType>
	RuleType& AddSparseRule(ULayoutProfileAsset& Profile)
	{
		FInstancedStruct& Rule = Profile.SparsePlacementRules.AddDefaulted_GetRef();
		Rule.InitializeAs<RuleType>();
		return *Rule.GetMutablePtr<RuleType>();
	}

	/** Adds one generic GroundOnly preserve-only rule to a profile. */
	void AddGroundInteriorTerrainResidualRule(ULayoutProfileAsset& Profile)
	{
		FLayoutSparsePreserveTerrainRule& Rule = AddSparseRule<FLayoutSparsePreserveTerrainRule>(Profile);
		Rule.RuleId = TEXT("GroundInteriorTerrain");
		Rule.PlacementZone = ELayoutPlacementZone::Interior;
		Rule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	}

	/** Creates one one-cell sparse module whose faces tolerate settled empty terrain. */
	ULayoutModuleAsset* CreateSparseModule(UObject* Outer, const TCHAR* Name)
	{
		UChunkStructureTemplate* Template = CreateTemplate(
			Outer,
			*FString::Printf(TEXT("%s_Template"), Name),
			FIntVector(8, 8, 8));
		const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
		return CreateModule(
			Outer,
			Name,
			Template,
			{ELayoutCellIntent::Interior, ELayoutCellIntent::Core},
			BuildFilledCubeFaces(
				OpenTags,
				OpenTags,
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				OpenTags,
				OpenTags,
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	}

	/** Creates sparse content with one explicit lateral owning-region boundary requirement. */
	ULayoutRegionContentSetAsset* CreateFaceBoundarySparseContentSet(
		UObject* Outer,
		const TCHAR* Name,
		const ELayoutFaceBoundaryRequirement BoundaryRequirement)
	{
		ULayoutModuleAsset* Module = CreateSparseModule(Outer, *FString::Printf(TEXT("%s_Module"), Name));
		for (const ELayoutFaceDirection Direction : {
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY})
		{
			Module->FaceRules.FindRule(Direction)->BoundaryRequirement = BoundaryRequirement;
		}
		FLayoutRegionContentEntry Entry;
		Entry.EntryId = FName(Name);
		Entry.ContentKind = ELayoutRegionContentKind::Module;
		Entry.ModuleSettings.Module = Module;
		Entry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Any;
		return CreateRegionContentSet(Outer, *FString::Printf(TEXT("%s_Content"), Name), {Entry});
	}

	/** Builds weighted sparse content without coupling selection weight to legal yaw count. */
	ULayoutRegionContentSetAsset* CreateWeightedSparseContentSet(UObject* Outer)
	{
		FLayoutRegionContentEntry Light;
		Light.EntryId = TEXT("LightScatter");
		Light.ContentKind = ELayoutRegionContentKind::Module;
		Light.Weight = 1;
		Light.ModuleSettings.Module = CreateSparseModule(Outer, TEXT("TerrainResidualLightScatter"));
		Light.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;

		FLayoutRegionContentEntry Heavy;
		Heavy.EntryId = TEXT("HeavyScatter");
		Heavy.ContentKind = ELayoutRegionContentKind::Module;
		Heavy.Weight = 3;
		Heavy.ModuleSettings.Module = CreateSparseModule(Outer, TEXT("TerrainResidualHeavyScatter"));
		Heavy.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;

		return CreateRegionContentSet(Outer, TEXT("TerrainResidualSparseContent"), {Light, Heavy});
	}

	/** Imports one existing generic recursive-layout acceptance fixture. */
	bool LoadRecursiveTerrainResidualFixture(FAutomationTestBase& Test, FLayoutProfileJsonFixtureAssets& OutAssets)
	{
		const FString FixturePath = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir()
			/ TEXT("Plugins/PorismDIMsWorldGeneratorExtension/Source/PorismDIMsWorldGeneratorExtensionTests/Private/Layout/Fixtures/Data/DA_Profile_Castle_DA_ContentSet_Castle_MultiChildForcedSeamShare.json"));
		FString Json;
		if (!Test.TestTrue(TEXT("Recursive acceptance fixture loads"), FFileHelper::LoadFileToString(Json, *FixturePath)))
		{
			return false;
		}
		TArray<FString> Issues;
		const bool bImported = FLayoutProfileJsonFixture::ImportFromString(
			Json,
			CreatePackage(*FString::Printf(TEXT("/Temp/TerrainResidualRecursive_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))),
			OutAssets,
			Issues);
		for (const FString& Issue : Issues)
		{
			Test.AddError(Issue);
		}
		return Test.TestTrue(TEXT("Recursive acceptance fixture imports"), bImported)
			&& Test.TestEqual(TEXT("Recursive acceptance fixture imports cleanly"), Issues.Num(), 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainResidualCanonicalTopologyTest,
	"PorismExtension.Layout.Solver.TerrainResidualSparse.CanonicalTopology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainResidualFillAvailableDeterminismTest,
	"PorismExtension.Layout.Solver.TerrainResidualSparse.FillAvailableDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainResidualAtomicSparseBundleTest,
	"PorismExtension.Layout.Solver.TerrainResidualSparse.AtomicBundleConsumption",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainResidualRequiredChildClaimTest,
	"PorismExtension.Layout.Solver.TerrainResidualSparse.RequiredChildClaimsResidualTerrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainResidualRequiredAnchorRouteTest,
	"PorismExtension.Layout.Solver.TerrainResidualSparse.RequiredAnchorRouteClaimsResidualTerrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainResidualFaceBoundaryMatrixTest,
	"PorismExtension.Layout.Solver.TerrainResidualSparse.FaceBoundaryMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainResidualShiftedGroundMetadataTest,
	"PorismExtension.Layout.Solver.TerrainResidualSparse.ShiftedGroundMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainResidualJsonRoundTripTest,
	"PorismExtension.Layout.Solver.TerrainResidualSparse.JsonRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainResidualAuthoredSparseRuleOrderTest,
	"PorismExtension.Layout.Solver.TerrainResidualSparse.AuthoredRuleOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTypedSparseRulePayloadTest,
	"PorismExtension.Layout.Solver.TerrainResidualSparse.TypedRulePayloads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Proves a representative large footprint keeps its physical shell while every unclaimed Interior cell becomes terrain residual. */
bool FLayoutTerrainResidualCanonicalTopologyTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("TerrainResidualCanonicalTopology"),
		FIntPoint(22, 25),
		FIntPoint(22, 25),
		1,
		0,
		false);
	AddGroundInteriorTerrainResidualRule(*Profile);
	FLayoutSparseFillAvailablePlacementRule& SparseRule = AddSparseRule<FLayoutSparseFillAvailablePlacementRule>(*Profile);
	SparseRule.RuleId = TEXT("RepresentativeBoundedScatter");
	SparseRule.CandidateSource = ELayoutSparseCandidateSource::ExistingResiduals;
	SparseRule.ContentSet = CreateWeightedSparseContentSet(GetTransientPackage());
	SparseRule.PlacementZone = ELayoutPlacementZone::Interior;
	SparseRule.MinSpacingCells = 1000;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 22025);
	TestTrue(TEXT("Representative terrain-residual profile solves"), Result.bSucceeded);
	if (!Result.bSucceeded)
	{
		AddError(Result.FailureReason);
		return false;
	}

	int32 EdgeCount = 0;
	int32 CornerCount = 0;
	int32 InteriorCount = 0;
	for (const FLayoutPlannedCell& Cell : Result.PlannedCells)
	{
		EdgeCount += Cell.PlacementZone == ELayoutPlacementZone::Edge ? 1 : 0;
		CornerCount += Cell.PlacementZone == ELayoutPlacementZone::Corner ? 1 : 0;
		InteriorCount += Cell.PlacementZone == ELayoutPlacementZone::Interior ? 1 : 0;
	}
	TestEqual(TEXT("Physical non-corner edge topology remains canonical"), EdgeCount, 86);
	TestEqual(TEXT("Physical corner topology remains canonical"), CornerCount, 4);
	TestEqual(TEXT("Interior topology remains canonical"), InteriorCount, 460);
	TestEqual(TEXT("Physical perimeter plus one maximally spaced sparse module are placed"), Result.Placements.Num(), 91);
	TestEqual(TEXT("Every unclaimed Interior cell remains terrain residual"), Result.ResidualUnoccupiedCells.Num(), 459);
	TestEqual(TEXT("Representative sparse pass sees every canonical Interior residual once"), Result.SparsePlacementStats.EligibleCellCount, 460);
	TestTrue(TEXT("Representative FillAvailable legal checks stay bounded by eligible roots"), Result.SparsePlacementStats.LegalCandidateCheckCount > 0 && Result.SparsePlacementStats.LegalCandidateCheckCount <= 460);
	TestEqual(TEXT("Large spacing accepts one representative sparse placement"), Result.SparsePlacementStats.AcceptedPlacementCount, 1);
	TestEqual(TEXT("Sparse plus untouched residual authority preserves full Interior set"), Result.SparsePlacementCommitments.Num() + Result.ResidualUnoccupiedCells.Num(), 460);

	for (const FLayoutResidualCellRecord& Residual : Result.ResidualUnoccupiedCells)
	{
		TestEqual(TEXT("Residual keeps terrain-backed source"), Residual.Source, ELayoutResidualCellSource::TerrainBackedRule);
		TestEqual(TEXT("Residual keeps source rule"), Residual.SourceSparsePlacementRuleId, FName(TEXT("GroundInteriorTerrain")));
		TestEqual(TEXT("Residual keeps authored ground level"), Residual.ModuleLevelIndex, 0);
		TestEqual(TEXT("Residual keeps canonical Interior zone"), Residual.PlacementZone, ELayoutPlacementZone::Interior);
	}
	return true;
}

/** Proves FillAvailable is deterministic, spaced, weighted by entry rather than yaw, and maximal for one-cell content. */
bool FLayoutTerrainResidualFillAvailableDeterminismTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("TerrainResidualFillAvailable"),
		FIntPoint(7, 7),
		FIntPoint(7, 7),
		1,
		0,
		false);
	FLayoutSparseFillAvailablePlacementRule& SparseRule = AddSparseRule<FLayoutSparseFillAvailablePlacementRule>(*Profile);
	SparseRule.RuleId = TEXT("WeightedFillAvailable");
	SparseRule.CandidateSource = ELayoutSparseCandidateSource::PreserveSupportedTerrain;
	SparseRule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	SparseRule.ContentSet = CreateWeightedSparseContentSet(GetTransientPackage());
	SparseRule.PlacementZone = ELayoutPlacementZone::Interior;
	SparseRule.MinSpacingCells = 2;

	const FLayoutSolveResult First = FLayoutProfileSolver::Solve(Profile, 707);
	const FLayoutSolveResult Replay = FLayoutProfileSolver::Solve(Profile, 707);
	TestTrue(TEXT("FillAvailable solve succeeds"), First.bSucceeded);
	TestTrue(TEXT("Same-seed FillAvailable replay succeeds"), Replay.bSucceeded);
	if (!First.bSucceeded || !Replay.bSucceeded)
	{
		AddError(!First.bSucceeded ? First.FailureReason : Replay.FailureReason);
		return false;
	}

	TestEqual(TEXT("Same seed repeats sparse placement count"), First.SparsePlacementCommitments.Num(), Replay.SparsePlacementCommitments.Num());
	for (int32 Index = 0; Index < First.SparsePlacementCommitments.Num() && Index < Replay.SparsePlacementCommitments.Num(); ++Index)
	{
		const FLayoutSparsePlacementCommitment& Left = First.SparsePlacementCommitments[Index];
		const FLayoutSparsePlacementCommitment& Right = Replay.SparsePlacementCommitments[Index];
		TestEqual(TEXT("Same seed repeats sparse cell"), Left.Cell, Right.Cell);
		TestEqual(TEXT("Same seed repeats weighted entry"), Left.SourceContentEntryId, Right.SourceContentEntryId);
		TestEqual(TEXT("Same seed repeats sparse yaw"), Left.YawRotationSteps, Right.YawRotationSteps);
		TestEqual(TEXT("One-cell sparse commitment records atomic occupancy"), Left.OccupiedCells, TArray<FIntVector>({Left.Cell}));
	}

	for (int32 LeftIndex = 0; LeftIndex < First.SparsePlacementCommitments.Num(); ++LeftIndex)
	{
		for (int32 RightIndex = LeftIndex + 1; RightIndex < First.SparsePlacementCommitments.Num(); ++RightIndex)
		{
			const FIntVector& Left = First.SparsePlacementCommitments[LeftIndex].Cell;
			const FIntVector& Right = First.SparsePlacementCommitments[RightIndex].Cell;
			const int32 Distance = FMath::Abs(Left.X - Right.X) + FMath::Abs(Left.Y - Right.Y) + FMath::Abs(Left.Z - Right.Z);
			TestTrue(TEXT("FillAvailable respects Manhattan spacing"), Distance >= SparseRule.MinSpacingCells);
		}
	}

	TestEqual(TEXT("One authored sparse rule is evaluated"), First.SparsePlacementStats.RuleCount, 1);
	TestEqual(TEXT("Every Interior residual enters bounded sparse eligibility once"), First.SparsePlacementStats.EligibleCellCount, 25);
	TestTrue(TEXT("Legal checks stay bounded by eligible roots"), First.SparsePlacementStats.LegalCandidateCheckCount <= 25);
	TestEqual(TEXT("Accepted counter matches commitments"), First.SparsePlacementStats.AcceptedPlacementCount, First.SparsePlacementCommitments.Num());
	TestEqual(
		TEXT("Sparse placements plus untouched residuals preserve full 5x5 Interior terrain authority"),
		First.SparsePlacementCommitments.Num() + First.ResidualUnoccupiedCells.Num(),
		25);
	for (const FLayoutResidualCellRecord& Residual : First.ResidualUnoccupiedCells)
	{
		const bool bBlockedBySpacing = First.SparsePlacementCommitments.ContainsByPredicate([&Residual, &SparseRule](const FLayoutSparsePlacementCommitment& Commitment)
		{
			const int32 Distance = FMath::Abs(Commitment.Cell.X - Residual.Cell.X)
				+ FMath::Abs(Commitment.Cell.Y - Residual.Cell.Y)
				+ FMath::Abs(Commitment.Cell.Z - Residual.Cell.Z);
			return Distance < SparseRule.MinSpacingCells;
		});
		TestTrue(TEXT("Every untouched residual is blocked by spacing in maximal FillAvailable output"), bBlockedBySpacing);
	}
	return true;
}

/** Proves a sparse composite claims and removes every occupied residual cell as one atomic commitment. */
bool FLayoutTerrainResidualAtomicSparseBundleTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		Outer,
		TEXT("TerrainResidualAtomicBundle"),
		FIntPoint(6, 6),
		FIntPoint(6, 6),
		1,
		0,
		false);
	AddGroundInteriorTerrainResidualRule(*Profile);

	ULayoutModuleAsset* Leaf = CreateSparseModule(Outer, TEXT("TerrainResidualAtomicLeaf"));
	FLayoutCompositeModuleCell FirstCell;
	FirstCell.Module = Leaf;
	FirstCell.LocalCell = FIntVector(0, 0, 0);
	FLayoutCompositeModuleCell SecondCell;
	SecondCell.Module = Leaf;
	SecondCell.LocalCell = FIntVector(1, 0, 0);
	ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("TerrainResidualAtomicComposite"));
	Composite->Cells = {FirstCell, SecondCell};

	FLayoutRegionContentEntry CompositeEntry;
	CompositeEntry.EntryId = TEXT("AtomicPair");
	CompositeEntry.ContentKind = ELayoutRegionContentKind::Module;
	CompositeEntry.ModuleSettings.CompositeModule = Composite;
	CompositeEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;
	ULayoutRegionContentSetAsset* SparseContent = CreateRegionContentSet(Outer, TEXT("TerrainResidualAtomicSparseContent"), {CompositeEntry});

	FLayoutSparseFillAvailablePlacementRule& SparseRule = AddSparseRule<FLayoutSparseFillAvailablePlacementRule>(*Profile);
	SparseRule.RuleId = TEXT("AtomicPairs");
	SparseRule.CandidateSource = ELayoutSparseCandidateSource::ExistingResiduals;
	SparseRule.ContentSet = SparseContent;
	SparseRule.PlacementZone = ELayoutPlacementZone::Interior;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 606);
	TestTrue(TEXT("Atomic sparse bundle solve succeeds"), Result.bSucceeded);
	if (!Result.bSucceeded)
	{
		AddError(Result.FailureReason);
		return false;
	}

	TSet<FIntVector> ClaimedCells;
	for (const FLayoutSparsePlacementCommitment& Commitment : Result.SparsePlacementCommitments)
	{
		TestEqual(TEXT("Each composite commitment owns both occupied cells"), Commitment.OccupiedCells.Num(), 2);
		for (const FIntVector& Cell : Commitment.OccupiedCells)
		{
			TestFalse(TEXT("Sparse bundles never overlap claimed residual cells"), ClaimedCells.Contains(Cell));
			ClaimedCells.Add(Cell);
			TestFalse(TEXT("Claimed bundle cell is removed from residual output"), Result.ResidualUnoccupiedCells.ContainsByPredicate([&Cell](const FLayoutResidualCellRecord& Residual)
			{
				return Residual.Cell == Cell;
			}));
		}
	}
	TestEqual(TEXT("Atomic bundle occupancy plus untouched residuals preserves full 4x4 Interior set"), ClaimedCells.Num() + Result.ResidualUnoccupiedCells.Num(), 16);
	TestTrue(TEXT("Occupied-cell work covers every accepted composite cell without exceeding two cells per legal root"),
		Result.SparsePlacementStats.OccupiedCellCheckCount >= Result.SparsePlacementStats.AcceptedPlacementCount * 2
		&& Result.SparsePlacementStats.OccupiedCellCheckCount <= Result.SparsePlacementStats.LegalCandidateCheckCount * 2);
	return true;
}

/** Proves required child placement claims generic Interior terrain support before final residual publication. */
bool FLayoutTerrainResidualRequiredChildClaimTest::RunTest(const FString& Parameters)
{
	FLayoutProfileJsonFixtureAssets Assets;
	if (!LoadRecursiveTerrainResidualFixture(*this, Assets)
		|| !TestNotNull(TEXT("Recursive fixture profile exists"), Assets.Profile.Get())
		|| !TestNotNull(TEXT("Recursive fixture content set exists"), Assets.ContentSet.Get()))
	{
		return false;
	}

	ULayoutProfileAsset* Profile = Assets.Profile;
	Profile->MinimumFootprintInCells = FIntPoint(8, 8);
	Profile->MaximumFootprintInCells = FIntPoint(8, 8);
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::None;
	Profile->VerticalAccessCount = 0;
	Profile->MinVerticalAccessCount = 0;
	Profile->MaxVerticalAccessCount = 0;
	Profile->bRequireAllTraversalChannelsReachable = false;
	Profile->SparsePlacementRules.Reset();
	AddGroundInteriorTerrainResidualRule(*Profile);

	if (!TestEqual(TEXT("Fixture exposes one generic child requirement"), Profile->ZoneFeatureRequirements.Num(), 1))
	{
		return false;
	}
	FLayoutZoneFeatureRequirement& Requirement = Profile->ZoneFeatureRequirements[0];
	Requirement.RequirementId = TEXT("RequiredInteriorChild");
	Requirement.Zone = ELayoutPlacementZone::Interior;
	Requirement.MinCount = 1;
	Requirement.MaxCount = 1;

	FLayoutRegionContentEntry* ChildEntry = Assets.ContentSet->Entries.FindByPredicate([](const FLayoutRegionContentEntry& Entry)
	{
		return Entry.ContentKind == ELayoutRegionContentKind::ChildRegion;
	});
	if (!TestNotNull(TEXT("Fixture exposes generic child provider"), ChildEntry))
	{
		return false;
	}
	ChildEntry->ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Any;
	ChildEntry->ChildRegionSettings.bOptional = false;
	// Fixture walls originally require filled courtyard neighbors. This acceptance
	// case authors the intended optional-empty interior face policy explicitly.
	for (FLayoutRegionContentEntry& Entry : Assets.ContentSet->Entries)
	{
		if (Entry.ContentKind != ELayoutRegionContentKind::Module
			|| Entry.ModuleSettings.Module == nullptr)
		{
			continue;
		}
		for (const ELayoutFaceDirection Direction : {
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY})
		{
			if (FLayoutFaceRule* FaceRule = Entry.ModuleSettings.Module->FaceRules.FindRule(Direction))
			{
				if (FaceRule->OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor)
				{
					FaceRule->OccupancyPolicy = ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor;
				}
				else if (FaceRule->OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor)
				{
					FaceRule->OccupancyPolicy = ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor;
				}
			}
		}
	}

	FLayoutRegionSolveRequest Request;
	FString BuildFailure;
	if (!TestTrue(TEXT("Recursive terrain-residual request builds"), FLayoutProfileJsonFixture::BuildImportedRootRequest(
		Assets,
		808,
		TEXT("TerrainResidualRequiredChild"),
		Request,
		FLayoutRootSolveBudgetSettings(),
		NAME_None,
		NAME_None,
		0,
		&BuildFailure)))
	{
		AddError(BuildFailure);
		return false;
	}
	Request.ExecutionSettings.MaxSolveDurationSeconds = 10.0f;
	const FLayoutRegionSolveScheduleResult Schedule = FLayoutProfileSolver::SolveRegionTree(Request);
	TestTrue(TEXT("Required child claims terrain-residual candidate volume"), Schedule.bSucceeded);
	if (!Schedule.bSucceeded)
	{
		AddError(Schedule.FailureReason);
		return false;
	}

	const FLayoutRegionSolveResult* ParentResult = Schedule.RegionResults.FindByPredicate([&Request](const FLayoutRegionSolveResult& Result)
	{
		return Result.RegionDebugPath == Request.RegionDebugPath;
	});
	TestNotNull(TEXT("Schedule retains parent result"), ParentResult);
	int32 ChildCount = 0;
	for (const FLayoutRegionSolveResult& RegionResult : Schedule.RegionResults)
	{
		ChildCount += RegionResult.RegionDebugPath.StartsWith(Request.RegionDebugPath + TEXT("/")) ? 1 : 0;
	}
	TestEqual(TEXT("Exactly one required generic child commits"), ChildCount, 1);
	if (ParentResult != nullptr)
	{
		TestTrue(TEXT("Unclaimed parent Interior cells remain terrain-backed residuals"), ParentResult->SolveResult.ResidualUnoccupiedCells.ContainsByPredicate([](const FLayoutResidualCellRecord& Residual)
		{
			return Residual.Source == ELayoutResidualCellSource::TerrainBackedRule;
		}));

		for (const FLayoutRegionSolveResult& RegionResult : Schedule.RegionResults)
		{
			if (!RegionResult.RegionDebugPath.StartsWith(Request.RegionDebugPath + TEXT("/")))
			{
				continue;
			}
			for (const FLayoutPlannedCell& ChildCell : RegionResult.SolveResult.PlannedCells)
			{
				const FIntVector ParentCell = ChildCell.Cell + RegionResult.RegionCellOffset;
				TestFalse(TEXT("Required child cells are removed from parent residual output"), ParentResult->SolveResult.ResidualUnoccupiedCells.ContainsByPredicate([&ParentCell](const FLayoutResidualCellRecord& Residual)
				{
					return Residual.Cell == ParentCell;
				}));
			}
		}
	}
	return true;
}

/** Proves a translated required contact anchor reuses local A* and promotes only its selected residual path. */
bool FLayoutTerrainResidualRequiredAnchorRouteTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("TerrainResidualRequiredAnchorRoute"),
		FIntPoint(7, 7),
		FIntPoint(7, 7),
		1,
		1,
		false);
	AddGroundInteriorTerrainResidualRule(*Profile);

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		Profile,
		7337,
		TEXT("TerrainResidualRequiredAnchorRoute"));
	FLayoutCommittedTraversalAnchor& ContactAnchor = Request.CommittedTraversalAnchors.AddDefaulted_GetRef();
	ContactAnchor.Cell = FIntVector(3, 3, 0);
	ContactAnchor.TraversalChannel = LayoutGameplayTags::TraversalPrimary;

	const FLayoutRegionSolveResult RegionResult = FLayoutProfileSolver::SolveRegion(Request);
	TestTrue(TEXT("Required contact anchor routes through claimable terrain residuals"), RegionResult.SolveResult.bSucceeded);
	if (!RegionResult.SolveResult.bSucceeded)
	{
		AddError(RegionResult.SolveResult.FailureReason);
		return false;
	}

	const TArray<FLayoutCellReservationRecord> RouteReservations = RegionResult.SolveResult.CompiledReservations.FilterByPredicate([](const FLayoutCellReservationRecord& Reservation)
	{
		return Reservation.ReservationKind == ELayoutCellReservationKind::RequiredRoute
			|| Reservation.ReservationKind == ELayoutCellReservationKind::RouteJunction;
	});
	TestTrue(TEXT("Existing local A* promotes at least one selected route cell"), !RouteReservations.IsEmpty());
	TestEqual(TEXT("Connector remains planner intent rather than module role"),
		StaticEnum<ELayoutModuleRole>()->GetValueByNameString(TEXT("Connector")),
		static_cast<int64>(INDEX_NONE));
	TestTrue(TEXT("Required translated contact anchor belongs to promoted route"), RouteReservations.ContainsByPredicate([](const FLayoutCellReservationRecord& Reservation)
	{
		return Reservation.Cell == FIntVector(3, 3, 0);
	}));
	for (const FLayoutCellReservationRecord& Reservation : RouteReservations)
	{
		TestFalse(TEXT("Promoted structural route cells cannot remain terrain residuals"), RegionResult.SolveResult.ResidualUnoccupiedCells.ContainsByPredicate([&Reservation](const FLayoutResidualCellRecord& Residual)
		{
			return Residual.Cell == Reservation.Cell;
		}));
		if (const FLayoutPlacedModule* Placement = RegionResult.SolveResult.Placements.FindByPredicate([&Reservation](const FLayoutPlacedModule& Candidate)
		{
			return Candidate.Cell == Reservation.Cell;
		}); Placement != nullptr && Placement->Module != nullptr)
		{
			TestTrue(TEXT("Route carrier uses an existing coarse module role"),
				Placement->Module->Roles.Contains(ELayoutModuleRole::Entry)
				|| Placement->Module->Roles.Contains(ELayoutModuleRole::Interior));
		}
	}
	Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;
	const auto Optional = FLayoutProfileSolver::SolveRegion(Request);
	TestTrue(TEXT("Explicit anchor remains satisfiable with optional global traversal"), Optional.SolveResult.bSucceeded);
	TestTrue(TEXT("Optional traversal still publishes the committed anchor route"),
		Optional.SolveResult.CompiledReservations.ContainsByPredicate([](const FLayoutCellReservationRecord& Reservation)
		{
			return Reservation.Cell == FIntVector(3, 3, 0)
				&& (Reservation.ReservationKind == ELayoutCellReservationKind::RequiredRoute
					|| Reservation.ReservationKind == ELayoutCellReservationKind::RouteJunction);
		}));
	return true;
}

/** Proves BoundaryRequirement remains independent from empty/filled occupancy for true exterior and terrain residuals. */
bool FLayoutTerrainResidualFaceBoundaryMatrixTest::RunTest(const FString& Parameters)
{
	auto SolveCase = [](const TCHAR* Name, const ELayoutPlacementZone ResidualZone, const ELayoutPlacementZone SparseZone, const ELayoutFaceBoundaryRequirement BoundaryRequirement)
	{
		ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
			GetTransientPackage(),
			Name,
			FIntPoint(5, 5),
			FIntPoint(5, 5),
			1,
			0,
			false);
		FLayoutSparsePreserveTerrainRule& ResidualRule = AddSparseRule<FLayoutSparsePreserveTerrainRule>(*Profile);
		ResidualRule.RuleId = FName(*FString::Printf(TEXT("%s_Residual"), Name));
		ResidualRule.PlacementZone = ResidualZone;
		ResidualRule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
		FLayoutSparseExactPlacementRule& SparseRule = AddSparseRule<FLayoutSparseExactPlacementRule>(*Profile);
		SparseRule.RuleId = FName(*FString::Printf(TEXT("%s_Sparse"), Name));
		SparseRule.CandidateSource = ELayoutSparseCandidateSource::ExistingResiduals;
		SparseRule.ContentSet = CreateFaceBoundarySparseContentSet(GetTransientPackage(), Name, BoundaryRequirement);
		SparseRule.Count = 1;
		SparseRule.PlacementZone = SparseZone;
		return FLayoutProfileSolver::Solve(Profile, 5151);
	};

	const FLayoutSolveResult InteriorFacingResidual = SolveCase(
		TEXT("TerrainResidualMustInterior"),
		ELayoutPlacementZone::Interior,
		ELayoutPlacementZone::Core,
		ELayoutFaceBoundaryRequirement::MustFaceInterior);
	TestTrue(TEXT("MustFaceInterior case solves"), InteriorFacingResidual.bSucceeded);
	TestEqual(TEXT("MustFaceInterior admits intentional empty terrain inside owning topology"), InteriorFacingResidual.SparsePlacementCommitments.Num(), 1);
	if (InteriorFacingResidual.SparsePlacementCommitments.IsEmpty() && !InteriorFacingResidual.Messages.IsEmpty())
	{
		AddError(InteriorFacingResidual.Messages.Last().Message);
	}

	const FLayoutSolveResult InteriorFacingExterior = SolveCase(
		TEXT("TerrainResidualInteriorRejectsExterior"),
		ELayoutPlacementZone::Any,
		ELayoutPlacementZone::Corner,
		ELayoutFaceBoundaryRequirement::MustFaceInterior);
	TestTrue(TEXT("True-exterior rejection remains best effort"), InteriorFacingExterior.bSucceeded);
	TestEqual(TEXT("MustFaceInterior rejects true owning-region exterior"), InteriorFacingExterior.SparsePlacementCommitments.Num(), 0);

	const FLayoutSolveResult ExteriorFacingResidual = SolveCase(
		TEXT("TerrainResidualExteriorRejectsInterior"),
		ELayoutPlacementZone::Interior,
		ELayoutPlacementZone::Core,
		ELayoutFaceBoundaryRequirement::MustFaceExterior);
	TestTrue(TEXT("Interior-residual rejection remains best effort"), ExteriorFacingResidual.bSucceeded);
	TestEqual(TEXT("MustFaceExterior rejects intentional interior terrain residual"), ExteriorFacingResidual.SparsePlacementCommitments.Num(), 0);
	return true;
}

/** Proves sparse admission uses preserved authored level and stage metadata instead of physical Z or scattered topology. */
bool FLayoutTerrainResidualShiftedGroundMetadataTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	ULayoutModuleAsset* GroundModule = CreateSparseModule(Outer, TEXT("TerrainResidualShiftedGroundModule"));
	FLayoutRegionContentEntry GroundEntry;
	GroundEntry.EntryId = TEXT("ShiftedGroundScatter");
	GroundEntry.ContentKind = ELayoutRegionContentKind::Module;
	GroundEntry.ModuleSettings.Module = GroundModule;
	GroundEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;
	GroundEntry.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	ULayoutRegionContentSetAsset* SparseContent = CreateRegionContentSet(Outer, TEXT("TerrainResidualShiftedGroundContent"), {GroundEntry});

	ULayoutProfileAsset* Profile = CreateProfile(Outer, TEXT("TerrainResidualShiftedGroundProfile"), FIntPoint(3, 3), FIntPoint(3, 3), 1, 0, false);
	FLayoutSparseExactPlacementRule& SparseRule = AddSparseRule<FLayoutSparseExactPlacementRule>(*Profile);
	SparseRule.RuleId = TEXT("ShiftedGroundRule");
	SparseRule.CandidateSource = ELayoutSparseCandidateSource::ExistingResiduals;
	SparseRule.ContentSet = SparseContent;
	SparseRule.Count = 1;
	SparseRule.PlacementZone = ELayoutPlacementZone::Interior;

	FLayoutSolveResult Result;
	Result.bSucceeded = true;
	Result.Seed = 320;
	Result.FootprintSize = FIntPoint(3, 3);
	FLayoutResidualCellRecord& Residual = Result.ResidualUnoccupiedCells.AddDefaulted_GetRef();
	Residual.Cell = FIntVector(1, 1, 2);
	Residual.Intent = ELayoutCellIntent::Interior;
	Residual.Source = ELayoutResidualCellSource::TerrainBackedRule;
	Residual.SourceRegionDebugPath = TEXT("ShiftedGround");
	Residual.SourceSparsePlacementRuleId = TEXT("GroundInteriorTerrain");
	Residual.ModuleLevelIndex = 0;
	Residual.PlacementZone = ELayoutPlacementZone::Interior;
	Residual.TerrainStageIndex = 3;

	FString FailureReason;
	const bool bApplied = LayoutProfileSolverInternal::ApplySparsePlacementRulesToSolveResult(
		FLayoutProfileSolver::BuildProfileSnapshot(Profile),
		TEXT("ShiftedGround"),
		320,
		Result,
		FailureReason);
	TestTrue(TEXT("Shifted authored-ground sparse pass succeeds"), bApplied);
	if (!bApplied)
	{
		AddError(FailureReason);
		return false;
	}
	if (!TestEqual(TEXT("GroundOnly sparse module admits shifted physical Z through ModuleLevelIndex"), Result.SparsePlacementCommitments.Num(), 1))
	{
		for (const FLayoutValidationMessage& Message : Result.Messages)
		{
			AddError(Message.Message);
		}
		return false;
	}
	TestEqual(TEXT("Shifted residual is consumed"), Result.ResidualUnoccupiedCells.Num(), 0);
	TestEqual(TEXT("Sparse placement keeps shifted physical cell"), Result.SparsePlacementCommitments[0].Cell, FIntVector(1, 1, 2));
	return true;
}

/** Proves schema-v4 typed sparse snapshots transport preserved terrain without old split authoring. */
bool FLayoutTerrainResidualJsonRoundTripTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePackage(*FString::Printf(TEXT("/Temp/TerrainResidualJson_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		Outer,
		TEXT("TerrainResidualJsonProfile"),
		FIntPoint(5, 5),
		FIntPoint(5, 5),
		1,
		0,
		false);
	FLayoutSparseFillAvailablePlacementRule& SparseRule = AddSparseRule<FLayoutSparseFillAvailablePlacementRule>(*Profile);
	SparseRule.RuleId = TEXT("JsonFillAvailable");
	SparseRule.CandidateSource = ELayoutSparseCandidateSource::PreserveSupportedTerrain;
	SparseRule.ContentSet = CreateWeightedSparseContentSet(Outer);
	SparseRule.PlacementZone = ELayoutPlacementZone::Interior;
	SparseRule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	SparseRule.MinSpacingCells = 3;

	FString Json;
	FString ExportError;
	TestTrue(TEXT("Terrain-residual profile exports"), FLayoutProfileJsonFixture::ExportToString(Profile, Profile->ContentSet, Json, ExportError));
	if (Json.IsEmpty())
	{
		AddError(ExportError);
		return false;
	}
	TestFalse(TEXT("Removed terrain-residual array stays absent"), Json.Contains(TEXT("\"terrain_residual_rules\"")));
	TestTrue(TEXT("Export includes typed FillAvailable sparse rule"), Json.Contains(TEXT("\"rule_type\": \"fill_available\"")));
	TestTrue(TEXT("Export includes preserve-supported-terrain source"), Json.Contains(TEXT("\"candidate_source\": \"PreserveSupportedTerrain\"")));
	TestTrue(TEXT("Export includes schema-v4 profile/sparse snapshots"), Json.Contains(TEXT("\"snapshot_schema_version\": 4")));
	TestFalse(TEXT("Removed sparse residual-only switch stays absent"), Json.Contains(TEXT("use_residual_unoccupied_cells_only")));
	TestFalse(TEXT("Removed sparse reservation exclusions stay absent"), Json.Contains(TEXT("excluded_reservation_kinds")));
	TestFalse(TEXT("Removed sparse priority stays absent"), Json.Contains(TEXT("\"priority\"")));
	TestFalse(TEXT("Removed sparse effect stays absent"), Json.Contains(TEXT("\"effect\"")));

	FString OldSchemaJson = Json.Replace(TEXT("\"schema_version\": 4"), TEXT("\"schema_version\": 3"));
	FLayoutProfileJsonFixtureAssets RejectedOldSchema;
	TArray<FString> OldSchemaIssues;
	TestFalse(TEXT("Old sparse authoring schema is rejected without migration"), FLayoutProfileJsonFixture::ImportFromString(
		OldSchemaJson,
		CreatePackage(*FString::Printf(TEXT("/Temp/TerrainResidualOldSchema_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))),
		RejectedOldSchema,
		OldSchemaIssues));
	TestTrue(TEXT("Old schema rejection is explicit"), OldSchemaIssues.ContainsByPredicate([](const FString& Issue)
	{
		return Issue.Contains(TEXT("Unsupported layout fixture schema v3"));
	}));

	FLayoutProfileJsonFixtureAssets Imported;
	TArray<FString> Issues;
	const bool bImported = FLayoutProfileJsonFixture::ImportFromString(
		Json,
		CreatePackage(*FString::Printf(TEXT("/Temp/TerrainResidualJsonReplay_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))),
		Imported,
		Issues);
	for (const FString& Issue : Issues)
	{
		AddError(Issue);
	}
	TestTrue(TEXT("Terrain-residual profile reimports"), bImported);
	TestEqual(TEXT("Terrain-residual profile reimports cleanly"), Issues.Num(), 0);
	if (!bImported || Imported.Profile == nullptr)
	{
		return false;
	}
	TestEqual(TEXT("Typed sparse authoring round-trips"), Imported.Profile->SparsePlacementRules.Num(), 1);
	if (!Imported.Profile->SparsePlacementRules.IsEmpty())
	{
		const FLayoutSparseFillAvailablePlacementRule* ImportedRule =
			Imported.Profile->SparsePlacementRules[0].GetPtr<FLayoutSparseFillAvailablePlacementRule>();
		TestNotNull(TEXT("FillAvailable concrete type round-trips"), ImportedRule);
		if (ImportedRule != nullptr)
		{
			TestEqual(TEXT("Sparse spacing round-trips"), ImportedRule->MinSpacingCells, 3);
			TestEqual(TEXT("Preserve source round-trips"), ImportedRule->CandidateSource, ELayoutSparseCandidateSource::PreserveSupportedTerrain);
		}
	}
	TestEqual(TEXT("Profile snapshot uses typed sparse schema version"), Imported.ProfileSnapshot.SnapshotSchemaVersion, 4);
	return true;
}

/** Proves authored sparse array order is the sole competition priority. */
bool FLayoutTerrainResidualAuthoredSparseRuleOrderTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("TerrainResidualAuthoredRuleOrder"),
		FIntPoint(5, 5),
		FIntPoint(5, 5),
		1,
		0,
		false);
	AddGroundInteriorTerrainResidualRule(*Profile);

	auto AddRule = [Profile](const FName RuleId, ULayoutRegionContentSetAsset* ContentSet)
	{
		FLayoutSparseExactPlacementRule& Rule = AddSparseRule<FLayoutSparseExactPlacementRule>(*Profile);
		Rule.RuleId = RuleId;
		Rule.CandidateSource = ELayoutSparseCandidateSource::ExistingResiduals;
		Rule.ContentSet = ContentSet;
		Rule.Count = 1;
		Rule.PlacementZone = ELayoutPlacementZone::Core;
	};
	AddRule(TEXT("FirstAuthored"), CreateFaceBoundarySparseContentSet(GetTransientPackage(), TEXT("TerrainResidualFirstRule"), ELayoutFaceBoundaryRequirement::MustFaceInterior));
	AddRule(TEXT("SecondAuthored"), CreateFaceBoundarySparseContentSet(GetTransientPackage(), TEXT("TerrainResidualSecondRule"), ELayoutFaceBoundaryRequirement::MustFaceInterior));

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 9191);
	TestTrue(TEXT("Competing authored sparse rules remain best effort"), Result.bSucceeded);
	TestEqual(TEXT("Only one rule consumes the single Core residual"), Result.SparsePlacementCommitments.Num(), 1);
	if (!Result.SparsePlacementCommitments.IsEmpty())
	{
		TestEqual(TEXT("First authored sparse rule wins competition"), Result.SparsePlacementCommitments[0].RuleId, FName(TEXT("FirstAuthored")));
	}
	TestEqual(TEXT("Both authored rules are evaluated in array order"), Result.SparsePlacementStats.RuleCount, 2);
	return true;
}

/** Proves concrete reflected rule variants expose and flatten only their relevant payload. */
bool FLayoutTypedSparseRulePayloadTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Shared content-rule base stays hidden from typed rule picker"),
		FLayoutSparseContentRuleBase::StaticStruct()->HasMetaData(TEXT("Hidden")));
	TestNull(TEXT("Preserve-only rule has no content set"),
		FLayoutSparsePreserveTerrainRule::StaticStruct()->FindPropertyByName(
			GET_MEMBER_NAME_CHECKED(FLayoutSparseContentRuleBase, ContentSet)));
	TestNotNull(TEXT("Exact rule exposes Count"),
		FLayoutSparseExactPlacementRule::StaticStruct()->FindPropertyByName(
			GET_MEMBER_NAME_CHECKED(FLayoutSparseExactPlacementRule, Count)));
	TestNull(TEXT("Exact rule does not expose range minimum"),
		FLayoutSparseExactPlacementRule::StaticStruct()->FindPropertyByName(
			GET_MEMBER_NAME_CHECKED(FLayoutSparseRangePlacementRule, MinCount)));
	TestNotNull(TEXT("Range rule exposes minimum"),
		FLayoutSparseRangePlacementRule::StaticStruct()->FindPropertyByName(
			GET_MEMBER_NAME_CHECKED(FLayoutSparseRangePlacementRule, MinCount)));
	TestNotNull(TEXT("Range rule exposes maximum"),
		FLayoutSparseRangePlacementRule::StaticStruct()->FindPropertyByName(
			GET_MEMBER_NAME_CHECKED(FLayoutSparseRangePlacementRule, MaxCount)));
	TestNull(TEXT("FillAvailable rule exposes no exact count"),
		FLayoutSparseFillAvailablePlacementRule::StaticStruct()->FindPropertyByName(
			GET_MEMBER_NAME_CHECKED(FLayoutSparseExactPlacementRule, Count)));

	UObject* Outer = CreatePackage(TEXT("/Temp/TypedSparseRulePayloads"));
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		Outer,
		TEXT("TypedSparseRulePayloads"),
		FIntPoint(5, 5),
		FIntPoint(5, 5),
		1,
		0,
		false);
	FLayoutSparsePreserveTerrainRule& Preserve = AddSparseRule<FLayoutSparsePreserveTerrainRule>(*Profile);
	Preserve.RuleId = TEXT("Preserve");
	Preserve.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	FLayoutSparseRangePlacementRule& Range = AddSparseRule<FLayoutSparseRangePlacementRule>(*Profile);
	Range.RuleId = TEXT("Range");
	Range.CandidateSource = ELayoutSparseCandidateSource::ExistingResiduals;
	Range.ContentSet = CreateWeightedSparseContentSet(Outer);
	Range.MinCount = 2;
	Range.MaxCount = 4;
	Range.MinSpacingCells = 3;

	const FLayoutProfileSolveSnapshot Snapshot = FLayoutProfileSolver::BuildProfileSnapshot(Profile);
	TestEqual(TEXT("Typed profile snapshot uses schema v4"), Snapshot.SnapshotSchemaVersion, 4);
	TestEqual(TEXT("Both typed rules flatten"), Snapshot.SparsePlacementRules.Num(), 2);
	if (Snapshot.SparsePlacementRules.Num() == 2)
	{
		TestEqual(TEXT("Preserve variant flattens without count mode"), Snapshot.SparsePlacementRules[0].RuleKind, ELayoutSparsePlacementRuleKind::PreserveTerrain);
		TestEqual(TEXT("Range variant flattens"), Snapshot.SparsePlacementRules[1].RuleKind, ELayoutSparsePlacementRuleKind::Range);
		TestEqual(TEXT("Range source flattens"), Snapshot.SparsePlacementRules[1].CandidateSource, ELayoutSparseCandidateSource::ExistingResiduals);
		TestEqual(TEXT("Range minimum flattens"), Snapshot.SparsePlacementRules[1].MinCount, 2);
		TestEqual(TEXT("Range maximum flattens"), Snapshot.SparsePlacementRules[1].MaxCount, 4);
		TestEqual(TEXT("Range spacing flattens"), Snapshot.SparsePlacementRules[1].MinSpacingCells, 3);
	}

	const FLayoutSolveResult First = FLayoutProfileSolver::Solve(Profile, 4545);
	const FLayoutSolveResult Replay = FLayoutProfileSolver::Solve(Profile, 4545);
	TestTrue(TEXT("Typed Range rule solves"), First.bSucceeded);
	TestTrue(TEXT("Typed Range same-seed replay solves"), Replay.bSucceeded);
	TestTrue(
		*FString::Printf(
			TEXT("Best-effort typed Range places available content within its maximum (commitments=%d residuals=%d)"),
			First.SparsePlacementCommitments.Num(),
			First.ResidualUnoccupiedCells.Num()),
		First.SparsePlacementCommitments.Num() > 0 && First.SparsePlacementCommitments.Num() <= 4);
	// Range requests a best-effort target, not a hard minimum. In this 3x3 area,
	// a center-first placement exhausts Manhattan-spacing-3 capacity after one placement.
	if (First.SparsePlacementCommitments.Num() < Range.MinCount)
	{
		for (const auto& Residual : First.ResidualUnoccupiedCells)
		{
			TestTrue(TEXT("Below-target placement leaves no spacing-eligible residual"),
				First.SparsePlacementCommitments.ContainsByPredicate([&](const auto& Placement)
				{
					const FIntVector Delta = Placement.Cell - Residual.Cell;
					return FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) + FMath::Abs(Delta.Z) < Range.MinSpacingCells;
				}));
		}
	}
	TestEqual(TEXT("Typed Range same seed repeats count"), First.SparsePlacementCommitments.Num(), Replay.SparsePlacementCommitments.Num());
	return true;
}
