// Copyright 2026 Spotted Loaf Studio

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutBackgroundAdmissibilityPreflight.h"

#include "Misc/AutomationTest.h"

namespace
{
	FLayoutBackgroundAdmissibilityPreflightInput MakeUndergroundInput()
	{
		FLayoutBackgroundAdmissibilityPreflightInput Input;
		Input.RequestManifest.bHasSelectedModePlan = true;
		Input.RequestManifest.SelectedModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::UndergroundPocketPlacement;
		Input.RequestManifest.SelectedModePlan.PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
		Input.RequestManifest.bHasRootPlacementSubmission = true;
		Input.RequestManifest.bHasFrozenTerrainBiomeAdapterInput = true;
		FLayoutFrozenTerrainBiomeAdapterInput& Terrain = Input.RequestManifest.FrozenTerrainBiomeAdapterInput;
		Terrain.bHasFiniteSearchBounds = true;
		Terrain.bHasSampledColumnEvidence = true;
		Terrain.bHasRelativeEnvironmentClassification = true;
		Terrain.bIsClassifiedUnderground = false;
		FLayoutTerrainSurfaceSample& Surface = Terrain.SurfaceSamples.AddDefaulted_GetRef();
		Surface.bIsValid = true;
		return Input;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBackgroundAdmissibilityPreflightUndergroundBaseEvidenceTest,
	"PorismExtension.Layout.Async.AdmissibilityPreflight.UndergroundBaseEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBackgroundAdmissibilityPreflightUndergroundBaseEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutBackgroundAdmissibilityPreflightInput Input = MakeUndergroundInput();
	Input.RequestManifest.ProfileSnapshot.bUndergroundPlacement = true;
	const FLayoutBackgroundAdmissibilityPreflightResult SurfaceResult =
		FLayoutBackgroundAdmissibilityPreflight::Run(Input);
	TestFalse(TEXT("Underground profile rejects a surface root"), SurfaceResult.bAdmissible);
	TestTrue(TEXT("Underground rejection is actionable"), SurfaceResult.FailureReason.Contains(TEXT("only supports underground placement")));

	Input.RequestManifest.FrozenTerrainBiomeAdapterInput.SurfaceSamples.Reset();
	Input.RequestManifest.FrozenTerrainBiomeAdapterInput.bHasSampledColumnEvidence = false;
	Input.RequestManifest.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos = FIntVector(10, 20, 45);
	Input.RequestManifest.FrozenTerrainBiomeAdapterInput.bHasPocketVoidIntervalEvidence = true;
	Input.RequestManifest.FrozenTerrainBiomeAdapterInput.bIsClassifiedUnderground = true;
	FLayoutFrozenTerrainVoidIntervalSample& CavityInterval =
		Input.RequestManifest.FrozenTerrainBiomeAdapterInput.PocketVoidIntervals.AddDefaulted_GetRef();
	CavityInterval.BlockXY = FIntPoint(10, 20);
	CavityInterval.MinZ = 40;
	CavityInterval.MaxZ = 55;
	CavityInterval.bHasVoidEvidence = true;
	TestTrue(
		TEXT("Underground accepts finite frozen cavity evidence without an invented surface"),
		FLayoutBackgroundAdmissibilityPreflight::Run(Input).bAdmissible);

	Input.RequestManifest.ProfileSnapshot.bUndergroundPlacement = false;
	const FLayoutBackgroundAdmissibilityPreflightResult CavityResult =
		FLayoutBackgroundAdmissibilityPreflight::Run(Input);
	TestFalse(TEXT("Surface profile rejects a cavity root"), CavityResult.bAdmissible);
	TestTrue(TEXT("Surface rejection is actionable"), CavityResult.FailureReason.Contains(TEXT("only supports surface placement")));

	Input.RequestManifest.ProfileSnapshot.bUndergroundPlacement = true;
	TestTrue(
		TEXT("Underground accepts finite frozen cavity evidence without an invented surface"),
		FLayoutBackgroundAdmissibilityPreflight::Run(Input).bAdmissible);

	Input.RequestManifest.FrozenTerrainBiomeAdapterInput.PocketVoidIntervals.Reset();
	Input.RequestManifest.FrozenTerrainBiomeAdapterInput.bHasPocketVoidIntervalEvidence = false;
	const FLayoutBackgroundAdmissibilityPreflightResult MissingBaseResult = FLayoutBackgroundAdmissibilityPreflight::Run(Input);
	TestFalse(TEXT("Underground rejects missing frozen surface and cavity evidence"), MissingBaseResult.bAdmissible);
	TestTrue(TEXT("Missing cavity rejection remains actionable"), MissingBaseResult.FailureReason.Contains(TEXT("requires finite frozen surface or cavity-interval evidence")));
	return true;
}
