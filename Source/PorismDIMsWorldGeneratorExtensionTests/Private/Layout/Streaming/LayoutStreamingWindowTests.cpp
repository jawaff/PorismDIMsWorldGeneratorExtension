// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Streaming/LayoutStreamingWindow.h"
#include "Layout/Contracts/LayoutContractTypes.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Misc/AutomationTest.h"

using namespace PorismLayoutTestUtilities;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutStreamingWindowIncludesFillSupportTest,
	"PorismExtension.Layout.Streaming.IncludesFillSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutStreamingWindowIncludesFillSupportTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainContract Contract;
	auto& Fill = Contract.TerrainWrites.AddDefaulted_GetRef();
	Fill.BlockWorldPos = FIntVector(16, 0, 0);
	Fill.bResolveMaterialFromTerrain = true;
	Fill.MaterialSourceBlockWorldPos = FIntVector(16, 0, -1);
	auto& Clear = Contract.TerrainWrites.AddDefaulted_GetRef();
	Clear.BlockWorldPos = FIntVector(32, 0, 0);
	Clear.MaterialSourceBlockWorldPos = FIntVector(999); // Ignored for explicit writes.
	TSet<FIntVector> Required = {FIntVector::ZeroValue};
	FLayoutStreamingWindow::AddFrozenTerrainChunkOrigins(Contract, FIntVector(16), Required);
	TestEqual(TEXT("Coverage retains template chunk and adds write/source chunks"), Required.Num(), 4);
	TestTrue(TEXT("Support below chunk boundary participates in readiness"), Required.Contains(FIntVector(16, 0, -16)));
	TSet<FIntVector> Observed = {FIntVector::ZeroValue, FIntVector(16, 0, 0), FIntVector(32, 0, 0)};
	TestFalse(TEXT("Template and write chunks alone cannot release fill"), FLayoutStreamingWindow::AreRequiredChunkOriginsObserved(Required, Observed));
	Observed.Add(FIntVector(16, 0, -16));
	TestTrue(TEXT("Fresh support chunk completes existing readiness gate"), FLayoutStreamingWindow::AreRequiredChunkOriginsObserved(Required, Observed));
	Contract.TerrainWrites[0].MaterialSourceSearchDepthBlocks = 32;
	FLayoutStreamingWindow::AddFrozenTerrainChunkOrigins(Contract, FIntVector(16), Required);
	TestEqual(TEXT("Bounded search includes every crossed chunk"), Required.Num(), 6);
	Observed.Add(FIntVector(16, 0, -48));
	TestFalse(TEXT("Search endpoints alone cannot release an unloaded middle chunk"), FLayoutStreamingWindow::AreRequiredChunkOriginsObserved(Required, Observed));
	Observed.Add(FIntVector(16, 0, -32));
	TestTrue(TEXT("Complete material search range releases readiness"), FLayoutStreamingWindow::AreRequiredChunkOriginsObserved(Required, Observed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutStreamingWindowHandlesNegativeChunkOriginsTest,
	"PorismExtension.Layout.Streaming.HandlesNegativeChunkOrigins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutStreamingWindowSnapsSiteCenterToGlobalCellLatticeTest,
	"PorismExtension.Layout.Streaming.SnapsSiteCenterToGlobalCellLattice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutStreamingWindowComputesFootprintMinFromLatticeAlignedSiteCenterTest,
	"PorismExtension.Layout.Streaming.ComputesFootprintMinFromLatticeAlignedSiteCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutStreamingWindowCollectsSiteChunkOriginsTest,
	"PorismExtension.Layout.Streaming.CollectsSiteChunkOrigins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutStreamingWindowObservedChunkCoverageTest,
	"PorismExtension.Layout.Streaming.ObservedChunkCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutStreamingWindowCollectsCompositeSiteChunkOriginsWithoutBundleBoundsTest,
	"PorismExtension.Layout.Streaming.CollectsCompositeSiteChunkOriginsWithoutBundleBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutStreamingWindowHandlesNegativeChunkOriginsTest::RunTest(const FString& Parameters)
{
	const FIntVector ChunkOrigin = FLayoutStreamingWindow::BlockWorldPosToChunkOrigin(FIntVector(-1, -17, 0), FIntVector(16, 16, 16));
	TestEqual(TEXT("Negative block positions map to the owning chunk origin using floor division"), ChunkOrigin, FIntVector(-16, -32, 0));
	return true;
}

bool FLayoutStreamingWindowSnapsSiteCenterToGlobalCellLatticeTest::RunTest(const FString& Parameters)
{
	const FIntVector SnappedSiteCenter = FLayoutStreamingWindow::SnapSiteCenterBlockWorldPosToCellLattice(
		FIntVector(11, -9, 5),
		FIntVector(8, 8, 8));
	TestEqual(
		TEXT("World-facing site centers snap to the nearest global shared-cell lattice point anchored at block-world origin"),
		SnappedSiteCenter,
		FIntVector(8, -8, 8));
	return true;
}

bool FLayoutStreamingWindowComputesFootprintMinFromLatticeAlignedSiteCenterTest::RunTest(const FString& Parameters)
{
	const FIntVector FootprintMin = FLayoutStreamingWindow::ComputeFootprintMinBlockWorldPos(
		FLayoutStreamingWindow::SnapSiteCenterBlockWorldPosToCellLattice(
			FIntVector(11, -9, 5),
			FIntVector(8, 8, 8)),
		FIntPoint(1, 1),
		FIntVector(8, 8, 8));
	TestEqual(
		TEXT("Footprint min derivation stays consistent once the site center has been snapped onto the global cell lattice"),
		FootprintMin,
		FIntVector(4, -12, 8));
	return true;
}

bool FLayoutStreamingWindowCollectsSiteChunkOriginsTest::RunTest(const FString& Parameters)
{
	FResolvedLayoutSiteRecord SiteRecord;
	SiteRecord.SiteCenterBlockWorldPos = FIntVector(24, 8, 0);
	SiteRecord.SolveResult.FootprintSize = FIntPoint(2, 1);
	{
		FLayoutPlacedModule& FirstPlacement = SiteRecord.SolveResult.Placements.AddDefaulted_GetRef();
		FirstPlacement.Cell = FIntVector(0, 0, 0);
		FirstPlacement.Intent = ELayoutCellIntent::Entry;
		FirstPlacement.Module = reinterpret_cast<ULayoutModuleAsset*>(1);

		FLayoutPlacedModule& SecondPlacement = SiteRecord.SolveResult.Placements.AddDefaulted_GetRef();
		SecondPlacement.Cell = FIntVector(1, 0, 0);
		SecondPlacement.Intent = ELayoutCellIntent::Boundary;
		SecondPlacement.Module = reinterpret_cast<ULayoutModuleAsset*>(1);
	}

	const TSet<FIntVector> ChunkOrigins = FLayoutStreamingWindow::CollectRequiredChunkOriginsForSite(
		SiteRecord,
		FIntVector(16, 16, 16),
		FIntVector(16, 16, 16));
	TestEqual(TEXT("A two-cell site that straddles a chunk boundary touches two chunk origins"), ChunkOrigins.Num(), 2);
	TestTrue(TEXT("The first touched chunk origin is collected"), ChunkOrigins.Contains(FIntVector(0, 0, 0)));
	TestTrue(TEXT("The second touched chunk origin is collected"), ChunkOrigins.Contains(FIntVector(16, 0, 0)));
	return true;
}

bool FLayoutStreamingWindowObservedChunkCoverageTest::RunTest(const FString& Parameters)
{
	const TSet<FIntVector> RequiredChunkOrigins = {FIntVector(0, 0, 0), FIntVector(16, 0, 0)};
	const TSet<FIntVector> PartialObservedOrigins = {FIntVector(0, 0, 0)};
	const TSet<FIntVector> FullObservedOrigins = {FIntVector(0, 0, 0), FIntVector(16, 0, 0)};

	TestFalse(TEXT("Partial observed chunk coverage keeps realization deferred"), FLayoutStreamingWindow::AreRequiredChunkOriginsObserved(RequiredChunkOrigins, PartialObservedOrigins));
	TestTrue(TEXT("Full observed chunk coverage allows realization"), FLayoutStreamingWindow::AreRequiredChunkOriginsObserved(RequiredChunkOrigins, FullObservedOrigins));

	TArray<FLayoutLoadedChunkLayer> Layers;
	Layers.SetNum(2);
	Layers[0].ChunkSizeInBlocks = FIntVector(32);
	Layers[1].ChunkSizeInBlocks = FIntVector(16);
	Layers[0].Chunks.Add(FIntVector(-32, 0, 0), FLayoutLoadedChunkState{true});
	Layers[1].Chunks.Add(FIntVector(0, 0, 0), FLayoutLoadedChunkState{true});
	const FIntVector Min(-1, 0, 0), Max(15, 15, 15);
	TestTrue(TEXT("Mixed coarse/fine neighbors cover the complete footprint"),
		FLayoutStreamingWindow::IsBlockBoxCovered(Min, Max, Layers, true));
	TestFalse(TEXT("A missing block at the outer edge keeps realization deferred"),
		FLayoutStreamingWindow::IsBlockBoxCovered(Min, FIntVector(16, 15, 15), Layers));
	Layers[1].Chunks.Add(FIntVector(0, 0, 0), FLayoutLoadedChunkState{});
	TestTrue(TEXT("Updated-only data is loaded"), FLayoutStreamingWindow::IsBlockBoxCovered(Min, Max, Layers));
	TestFalse(TEXT("Updated-only data does not confer Created authority"),
		FLayoutStreamingWindow::IsBlockBoxCovered(Min, Max, Layers, true));
	Layers[0].Chunks.Add(FIntVector::ZeroValue, FLayoutLoadedChunkState{true});
	TestEqual(TEXT("Material lookup prefers the observed finer layer"),
		FLayoutStreamingWindow::FindLoadedLayerAtPosition(FIntVector(1), Layers), 1);
	Layers[1].Chunks.Reset();
	TestEqual(TEXT("Fine unload exposes existing coarse coverage"),
		FLayoutStreamingWindow::FindLoadedLayerAtPosition(FIntVector(1), Layers), 0);
	TestTrue(TEXT("Coarse coverage remains eligible without a new Created event"),
		FLayoutStreamingWindow::IsBlockBoxCovered(Min, Max, Layers, true));
	Layers[0].Chunks.Reset();
	TestFalse(TEXT("Full unload removes coverage"), FLayoutStreamingWindow::IsBlockBoxCovered(Min, Max, Layers));
	return true;
}

bool FLayoutStreamingWindowCollectsCompositeSiteChunkOriginsWithoutBundleBoundsTest::RunTest(const FString& Parameters)
{
	ULayoutCompositeModuleAsset* const Composite = NewObject<ULayoutCompositeModuleAsset>(GetTransientPackage(), TEXT("LayoutComposite_StreamingSitePair"));
	FLayoutCompositeModuleCell& FirstCell = Composite->Cells.AddDefaulted_GetRef();
	FirstCell.LocalCell = FIntVector(0, 0, 0);
	FLayoutCompositeModuleCell& SecondCell = Composite->Cells.AddDefaulted_GetRef();
	SecondCell.LocalCell = FIntVector(1, 0, 0);

	FResolvedLayoutSiteRecord SiteRecord;
	SiteRecord.SiteCenterBlockWorldPos = FIntVector(24, 8, 0);
	SiteRecord.SolveResult.FootprintSize = FIntPoint(2, 1);

	FLayoutPlacedModule& Placement = SiteRecord.SolveResult.Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector(0, 0, 0);
	Placement.Intent = ELayoutCellIntent::Entry;
	Placement.CompositeModule = Composite;
	Placement.OccupiedLocalCells = Composite->GetOccupiedLocalCells();

	const TSet<FIntVector> ChunkOrigins = FLayoutStreamingWindow::CollectRequiredChunkOriginsForSite(
		SiteRecord,
		FIntVector(16, 16, 16),
		FIntVector(16, 16, 16));
	TestEqual(TEXT("Composite site chunk collection still covers both occupied cells without BundleBoundsCells"), ChunkOrigins.Num(), 2);
	TestTrue(TEXT("Composite site chunk collection includes the first occupied chunk"), ChunkOrigins.Contains(FIntVector(0, 0, 0)));
	TestTrue(TEXT("Composite site chunk collection includes the second occupied chunk"), ChunkOrigins.Contains(FIntVector(16, 0, 0)));
	return true;
}
