// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Debug/ChunkWorldGeneratedBlockProbeLibrary.h"

#include "ChunkWorldStructs/ChunkWorldEnums.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldGeneratedBlockProbeScansRequestedColumnsTest,
	"PorismExtension.ChunkWorld.GeneratedBlockProbe.ScansRequestedColumns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldGeneratedBlockProbeScansRequestedColumnsTest::RunTest(const FString& Parameters)
{
	PorismLayoutWorldTestUtilities::FLayoutWorldTestHarness Harness = PorismLayoutWorldTestUtilities::CreateChunkWorldHarness(nullptr);
	TestNotNull(TEXT("Transient chunk world harness spawns a runtime world"), Harness.World);
	if (Harness.World == nullptr)
	{
		return false;
	}

	Harness.World->SetBlockValueByBlockWorldPos(FIntVector(2, 3, 0), SinfullMaterial, false);
	Harness.World->SetBlockValueByBlockWorldPos(FIntVector(2, 3, 2), SinfullMaterial, false);
	Harness.World->SetBlockValueByBlockWorldPos(FIntVector(2, 3, 5), SinfullMaterial, false);
	Harness.World->SetBlockValueByBlockWorldPos(FIntVector(3, 3, 1), SinfullMaterial, false);

	FGeneratedBlockColumnProbeRequest Request;
	Request.CenterBlockWorldPos = FIntVector(2, 3, 0);
	Request.MinZBlock = -1;
	Request.MaxZBlock = 6;
	Request.RelativeColumnOffsets = {
		FIntPoint(0, 0),
		FIntPoint(1, 0),
		FIntPoint(5, 0)
	};
	Request.EmptyMaterialIndex = DefaultMaterial;

	FGeneratedBlockProbeReport Report;
	TestTrue(
		TEXT("Generated block probe succeeds for a valid chunk world"),
		UChunkWorldGeneratedBlockProbeLibrary::ProbeGeneratedBlockColumns(Harness.World, Request, Report));

	TestTrue(TEXT("Probe detects at least one solid generated block"), Report.bFoundAnySolid);
	TestEqual(TEXT("Probe scans each explicit column offset"), Report.SampledColumnCount, 3);
	TestEqual(TEXT("Probe reports only columns with non-empty materials as solid"), Report.SolidColumnCount, 2);
	TestEqual(TEXT("Probe reports the lowest highest surface among solid columns"), Report.MinHighestSolidZ, 1);
	TestEqual(TEXT("Probe reports the highest surface among solid columns"), Report.MaxHighestSolidZ, 5);
	TestEqual(TEXT("Probe keeps per-column results"), Report.Columns.Num(), 3);

	const FGeneratedBlockColumnProbeResult& CenterColumn = Report.Columns[0];
	TestTrue(TEXT("Center column is solid"), CenterColumn.bFoundSolid);
	TestEqual(TEXT("Center column keeps the lowest solid Z"), CenterColumn.LowestSolidZ, 0);
	TestEqual(TEXT("Center column keeps the highest solid Z"), CenterColumn.HighestSolidZ, 5);
	TestEqual(TEXT("Center column counts all non-empty cells"), CenterColumn.SolidBlockCount, 3);
	TestEqual(TEXT("Center column reports the highest solid material"), CenterColumn.HighestSolidMaterialIndex, SinfullMaterial);

	const FGeneratedBlockColumnProbeResult& NeighborColumn = Report.Columns[1];
	TestTrue(TEXT("Neighbor column is solid"), NeighborColumn.bFoundSolid);
	TestEqual(TEXT("Neighbor column keeps its highest solid Z"), NeighborColumn.HighestSolidZ, 1);

	const FGeneratedBlockColumnProbeResult& EmptyColumn = Report.Columns[2];
	TestFalse(TEXT("Empty column stays marked empty"), EmptyColumn.bFoundSolid);

	return true;
}
