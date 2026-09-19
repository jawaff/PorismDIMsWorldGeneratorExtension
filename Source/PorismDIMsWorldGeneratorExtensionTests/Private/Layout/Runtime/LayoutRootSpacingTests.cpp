// Copyright 2026 Spotted Loaf Studio

#include "Misc/AutomationTest.h"
#include "Layout/Planning/LayoutRootSpacing.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutRootSpacingRuntimeTest,
	"PorismExtension.Layout.Runtime.RootSpacing.FootprintGapAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRootSpacingRuntimeTest::RunTest(const FString& Parameters)
{
	const FIntVector Cell(5, 10, 5);
	FLayoutRootSpacingReservation A, B;
	TestTrue(TEXT("Build authored footprint"), FLayoutRootSpacingReservation::TryBuild(TEXT("Binding"), FIntVector::ZeroValue, FIntPoint(2, 2), Cell, A));
	TestTrue(TEXT("Build a differently sized root"), FLayoutRootSpacingReservation::TryBuild(TEXT("Binding"), FIntVector(40, 0, 0), FIntPoint(4, 2), Cell, B));
	TestTrue(TEXT("Exactly five normal X cells of free space pass"), A.IsSeparatedFrom(B, 5, Cell));
	B.Min.X -= 1;
	TestFalse(TEXT("One block below minimum rejects regardless of root type"), A.IsSeparatedFrom(B, 5, Cell));
	B = A;
	TestFalse(TEXT("Zero gap still rejects overlap"), A.IsSeparatedFrom(B, 0, Cell));
	B.Min.X = A.Max.X + 1;
	B.Max.X = B.Min.X + 9;
	TestTrue(TEXT("Zero gap allows touching bounds without shared blocks"), A.IsSeparatedFrom(B, 0, Cell));
	B.BindingId = TEXT("OtherBinding");
	TestTrue(TEXT("Cross-binding spacing remains out of scope"), A.IsSeparatedFrom(B, 100, Cell));
	TestFalse(TEXT("Overflowing authored envelope rejects"), FLayoutRootSpacingReservation::TryBuild(TEXT("Binding"), FIntVector(MAX_int32, 0, 0), FIntPoint(2, 2), Cell, B));
	TestTrue(TEXT("Nearby exclusion survives outside center coverage"), A.IntersectsCoverage({FIntPoint(20, 0)}, {FIntPoint(20, 0)}, 25, 50));
	TestFalse(TEXT("Exclusion outside complete influence expires"), A.IntersectsCoverage({FIntPoint(30, 0)}, {FIntPoint(30, 0)}, 25, 50));

	TestTrue(TEXT("Candidate half-extents exclude centers outside the root itself"),
		A.ExcludesCenterRegion(TEXT("Binding"), FIntPoint(5, 0), FIntPoint(8, 2), FIntPoint(2, 2), Cell, 0));
	TestFalse(TEXT("A partially blocked region remains searchable"),
		A.ExcludesCenterRegion(TEXT("Binding"), FIntPoint(5, 0), FIntPoint(10, 2), FIntPoint(2, 2), Cell, 0));
	TestFalse(TEXT("Another candidate size can fit within the same region"),
		A.ExcludesCenterRegion(TEXT("Binding"), FIntPoint(5, 0), FIntPoint(8, 2), FIntPoint(1, 1), Cell, 0));
	TestFalse(TEXT("Another binding is never excluded by this gap proof"),
		A.ExcludesCenterRegion(TEXT("OtherBinding"), FIntPoint::ZeroValue, FIntPoint::ZeroValue, FIntPoint(2, 2), Cell, 100));
	TestFalse(TEXT("Overflow is inconclusive, not proof of exclusion"),
		A.ExcludesCenterRegion(TEXT("Binding"), FIntPoint(MAX_int32, 0), FIntPoint(MAX_int32, 0), FIntPoint(2, 2), Cell, 0));

	UChunkWorldLayoutRuntimeComponent* Runtime = NewObject<UChunkWorldLayoutRuntimeComponent>();
	ULayoutWorldBindingAsset* Binding = NewObject<ULayoutWorldBindingAsset>();
	Binding->BindingId = TEXT("Binding");
	Binding->BaseCellDimensionsBlocks = Cell;
	Binding->MinimumRootGapCells = 5;
	Runtime->LayoutWorldBindings.Add(Binding);
	Runtime->RootSpacingReservations.Add(TEXT("Castle"), A);
	TestFalse(TEXT("Queued reservation blocks another root candidate"), Runtime->CanPlanOrdinaryRootSiteForBinding(Binding, A));
	TestTrue(TEXT("Same reservation can validate itself"), Runtime->CanPlanOrdinaryRootSiteForBinding(Binding, A, TEXT("Castle")));
	Runtime->FinishPlanningAreaAttempt(TEXT("Castle"), false, false);
	TestFalse(TEXT("Success retains exclusion beyond worker lifetime"), Runtime->CanPlanOrdinaryRootSiteForBinding(Binding, A));
	Runtime->FinishPlanningAreaAttempt(TEXT("Castle"), false, true);
	TestTrue(TEXT("Cancellation releases exclusion"), Runtime->CanPlanOrdinaryRootSiteForBinding(Binding, A));
	Runtime->RootSpacingReservations.Add(TEXT("Castle"), A);
	Runtime->PruneRootSpacingReservations({}, {});
	TestTrue(TEXT("No coverage releases temporary spacing history"), Runtime->RootSpacingReservations.IsEmpty());
	Runtime->RootSpacingReservations.Add(TEXT("Castle"), A);
	Runtime->ResetResolvedLayoutSiteRecords(true);
	TestTrue(TEXT("Explicit full reset releases exclusions"), Runtime->RootSpacingReservations.IsEmpty());

	FResolvedLayoutSiteRecord CachedRoot;
	auto Publication = CachedRoot.GetRootPublicationMetadata();
	Publication.RootSolveId = TEXT("AutomaticSolve");
	CachedRoot.SetRootPublicationMetadata(Publication);
	CachedRoot.bHasBeenCommittedToChunkWorld = true;
	CachedRoot.bWritePlanReady = true;
	CachedRoot.SolveResult.FootprintSize = FIntPoint(2, 2);
	CachedRoot.SolveResult.SharedCellSizeInBlocks = Cell;
	CachedRoot.SolveResult.Placements.SetNum(3);
	CachedRoot.CachedTemplatePlacementCount = 3;
	Runtime->ResolvedSiteRecords.Add(TEXT("AutoSite"), CachedRoot);
	Runtime->PlanningRecordKeysBySiteRecordKey.Add(TEXT("AutoSite"), TEXT("AutoRoot"));
	Runtime->ResolvedRootFrozenTerrainContracts.Add(Publication.RootSolveId, FLayoutFrozenTerrainContract());
	Runtime->ResolvedRootRealizationWritePlans.Add(Publication.RootSolveId, nullptr);
	Publication.RootSolveId = TEXT("ExplicitSolve");
	CachedRoot.SetRootPublicationMetadata(Publication);
	Runtime->ResolvedSiteRecords.Add(TEXT("ExplicitSite"), CachedRoot);
	Runtime->ResolvedRootFrozenTerrainContracts.Add(Publication.RootSolveId, FLayoutFrozenTerrainContract());
	Runtime->ResolvedRootRealizationWritePlans.Add(Publication.RootSolveId, nullptr);
	Runtime->RootSpacingReservations.Add(TEXT("ExplicitSite"), A);
	Runtime->RootSpacingReservations.Add(TEXT("AutoRoot"), A);
	Runtime->PruneRootSpacingReservations({FIntPoint::ZeroValue}, {FIntPoint::ZeroValue});
	const auto& CompactRoot = Runtime->ResolvedSiteRecords.FindChecked(TEXT("AutoSite"));
	TestTrue(TEXT("Placed automatic root releases placements inside retained influence"), CompactRoot.SolveResult.Placements.IsEmpty());
	TestFalse(TEXT("Compact root no longer advertises replay authority"), CompactRoot.bWritePlanReady);
	TestTrue(TEXT("Compact root preserves committed ownership"), CompactRoot.bHasBeenCommittedToChunkWorld);
	TestEqual(TEXT("Compact root preserves footprint metrics"), CompactRoot.SolveResult.FootprintSize, FIntPoint(2, 2));
	TestEqual(TEXT("Compact root preserves placement diagnostics"), CompactRoot.CachedTemplatePlacementCount, 3);
	TestFalse(TEXT("Placed automatic root releases frozen payload inside influence"), Runtime->ResolvedRootFrozenTerrainContracts.Contains(TEXT("AutomaticSolve")));
	TestFalse(TEXT("Placed automatic root releases write plan inside influence"), Runtime->ResolvedRootRealizationWritePlans.Contains(TEXT("AutomaticSolve")));
	TestTrue(TEXT("Compact root retains spacing exclusion"), Runtime->RootSpacingReservations.Contains(TEXT("AutoRoot")));
	TestEqual(TEXT("Explicit committed preview keeps placements"), Runtime->ResolvedSiteRecords.FindChecked(TEXT("ExplicitSite")).SolveResult.Placements.Num(), 3);
	Runtime->StampedChunkOrigins.FindOrAdd(FIntVector(1, 0, 0)).StampedArtifactIdsByRootSolveId.Add(TEXT("AutomaticSolve"), TEXT("Artifact"));
	Runtime->StampedChunkOrigins.FindOrAdd(FIntVector(2, 0, 0)).StampedArtifactIdsByRootSolveId.Add(TEXT("ExplicitSolve"), TEXT("Artifact"));
	Runtime->StampedChunkOrigins.FindOrAdd(FIntVector(3, 0, 0)).bLoadedFromSave = true;
	Runtime->ObservedCoverageTileSize = FIntVector(1);
	Runtime->ObservedChunkLayers.SetNum(1);
	Runtime->ObservedChunkLayers[0].ChunkSizeInBlocks = FIntVector(1);
	Runtime->ObservedChunkLayers[0].Chunks.Add(FIntVector(3, 0, 0), FLayoutLoadedChunkState{});
	Runtime->PruneRootSpacingReservations({}, {});
	TestFalse(TEXT("Unloaded expired root releases stamp history"), Runtime->StampedChunkOrigins.Contains(FIntVector(1, 0, 0)));
	TestTrue(TEXT("Explicit ownership retains unloaded stamp protection"), Runtime->StampedChunkOrigins.Contains(FIntVector(2, 0, 0)));
	TestTrue(TEXT("Loaded restored chunk retains protection outside planning coverage"), Runtime->StampedChunkOrigins.Contains(FIntVector(3, 0, 0)));
	Runtime->ObservedChunkLayers[0].Chunks.Remove(FIntVector(3, 0, 0));
	Runtime->PruneRootSpacingReservations({}, {});
	TestFalse(TEXT("Unloaded unowned restore history retires outside coverage"), Runtime->StampedChunkOrigins.Contains(FIntVector(3, 0, 0)));
	TestFalse(TEXT("Automatic heavy root expires without influence"), Runtime->ResolvedSiteRecords.Contains(TEXT("AutoSite")));
	TestFalse(TEXT("Frozen payload copy expires with root"), Runtime->ResolvedRootFrozenTerrainContracts.Contains(TEXT("AutomaticSolve")));
	TestFalse(TEXT("Write-plan copy expires with root"), Runtime->ResolvedRootRealizationWritePlans.Contains(TEXT("AutomaticSolve")));
	TestTrue(TEXT("Explicit preview keeps Apply/Clear lifetime"), Runtime->ResolvedSiteRecords.Contains(TEXT("ExplicitSite")));
	TestTrue(TEXT("Explicit frozen payload survives automatic eviction"), Runtime->ResolvedRootFrozenTerrainContracts.Contains(TEXT("ExplicitSolve")));
	TestTrue(TEXT("Explicit spacing remains reserved"), Runtime->RootSpacingReservations.Contains(TEXT("ExplicitSite")));
	return true;
}
#endif
