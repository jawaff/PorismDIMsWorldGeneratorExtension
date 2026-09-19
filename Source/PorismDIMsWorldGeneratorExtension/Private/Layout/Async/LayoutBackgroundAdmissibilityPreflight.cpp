// Copyright 2026 Spotted Loaf Studio

#include "Layout/Async/LayoutBackgroundAdmissibilityPreflight.h"

namespace
{
	FLayoutBackgroundAdmissibilityPreflightResult Reject(const TCHAR* Reason)
	{
		FLayoutBackgroundAdmissibilityPreflightResult Result;
		Result.bAdmissible = false;
		Result.FailureReason = Reason;
		return Result;
	}

	FLayoutBackgroundAdmissibilityPreflightResult Accept()
	{
		FLayoutBackgroundAdmissibilityPreflightResult Result;
		Result.bAdmissible = true;
		return Result;
	}

	bool IsTerrainMode(const ELayoutContractEnvironmentMode Mode)
	{
		switch (Mode)
		{
		case ELayoutContractEnvironmentMode::NonSteppedWorldPlacement:
		case ELayoutContractEnvironmentMode::SteppedSurfacePlacement:
		case ELayoutContractEnvironmentMode::BridgeContinuation:
		case ELayoutContractEnvironmentMode::TunnelContinuation:
		case ELayoutContractEnvironmentMode::UndergroundPocketPlacement:
			return true;
		default:
			return false;
		}
	}

	bool HasBaseTerrainEnvelope(const FLayoutFrozenTerrainBiomeAdapterInput& Artifact)
	{
		return Artifact.bHasFiniteSearchBounds
			&& Artifact.bHasSampledColumnEvidence
			&& !Artifact.SurfaceSamples.IsEmpty();
	}

	/** Accepts pointer-free cavity intervals as Underground terrain evidence without inventing a surface sample. */
	bool HasUndergroundTerrainEnvelope(const FLayoutFrozenTerrainBiomeAdapterInput& Artifact)
	{
		return Artifact.bHasFiniteSearchBounds
			&& Artifact.bHasPocketVoidIntervalEvidence
			&& !Artifact.PocketVoidIntervals.IsEmpty();
	}
}

FLayoutBackgroundAdmissibilityPreflightResult FLayoutBackgroundAdmissibilityPreflight::Run(
	const FLayoutBackgroundAdmissibilityPreflightInput& Input)
{
	const FLayoutWorkerSolveRequestManifest& Manifest = Input.RequestManifest;
	if (!Manifest.bHasSelectedModePlan)
	{
		return Reject(TEXT("Cheap admissibility preflight requires a frozen selected mode plan."));
	}

	if (Input.Kind == ELayoutBackgroundAdmissibilityPreflightKind::Root
		&& !Manifest.bHasRootPlacementSubmission)
	{
		return Reject(TEXT("Root cheap admissibility preflight requires a frozen root placement submission."));
	}

	if (Manifest.SelectedModePlan.PlacementKind == ELayoutWorldBindingPlacementKind::OrdinaryRoot)
	{
		if (!Manifest.bHasFrozenTerrainBiomeAdapterInput
			|| !Manifest.FrozenTerrainBiomeAdapterInput.bHasRelativeEnvironmentClassification)
		{
			return Reject(TEXT("Ordinary-root preflight requires frozen selected-site environment classification."));
		}
		if (Manifest.ProfileSnapshot.bUndergroundPlacement
			!= Manifest.FrozenTerrainBiomeAdapterInput.bIsClassifiedUnderground)
		{
			return Reject(Manifest.ProfileSnapshot.bUndergroundPlacement
				? TEXT("Layout profile only supports underground placement.")
				: TEXT("Layout profile only supports surface placement."));
		}
	}

	if (IsTerrainMode(Manifest.SelectedModePlan.EnvironmentMode))
	{
		if (!Manifest.bHasFrozenTerrainBiomeAdapterInput)
		{
			return Reject(TEXT("Terrain cheap admissibility preflight requires a frozen terrain/biome artifact."));
		}

		const FLayoutFrozenTerrainBiomeAdapterInput& Artifact = Manifest.FrozenTerrainBiomeAdapterInput;
		const bool bUndergroundMode =
			Manifest.SelectedModePlan.EnvironmentMode == ELayoutContractEnvironmentMode::UndergroundPocketPlacement;
		if (!HasBaseTerrainEnvelope(Artifact)
			&& (!bUndergroundMode || !HasUndergroundTerrainEnvelope(Artifact)))
		{
			return Reject(bUndergroundMode
				? TEXT("Underground cheap admissibility preflight requires finite frozen surface or cavity-interval evidence.")
				: TEXT("Terrain cheap admissibility preflight requires finite in-bounds sampled terrain evidence."));
		}

		if (Manifest.SelectedModePlan.EnvironmentMode == ELayoutContractEnvironmentMode::BridgeContinuation
			&& (!Artifact.bHasTerrainPathEvidence || Artifact.TerrainPathSamples.IsEmpty()))
		{
			return Reject(TEXT("Bridge cheap admissibility preflight requires frozen terrain-path evidence."));
		}

	}

	return Accept();
}
