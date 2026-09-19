// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutStandaloneRegionRequestBuilder.h"

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutContentSetSolveSnapshotBuilder.h"
#include "Layout/Solver/LayoutModuleCatalogBuilder.h"
#include "Layout/Solver/LayoutRegionRequestSnapshotBuilder.h"
#include "Math/RandomStream.h"

namespace LayoutStandaloneRegionRequestBuilder
{
	FIntPoint SelectFootprintSize(const FLayoutProfileSolveSnapshot& ProfileSnapshot, const int32 Seed)
	{
		if (ProfileSnapshot.MinimumFootprintInCells.X <= 0
			|| ProfileSnapshot.MinimumFootprintInCells.Y <= 0
			|| ProfileSnapshot.MinimumFootprintInCells.X > ProfileSnapshot.MaximumFootprintInCells.X
			|| ProfileSnapshot.MinimumFootprintInCells.Y > ProfileSnapshot.MaximumFootprintInCells.Y)
		{
			return FIntPoint::ZeroValue;
		}

		FRandomStream Random(Seed);
		return FIntPoint(
			Random.RandRange(ProfileSnapshot.MinimumFootprintInCells.X, ProfileSnapshot.MaximumFootprintInCells.X),
			Random.RandRange(ProfileSnapshot.MinimumFootprintInCells.Y, ProfileSnapshot.MaximumFootprintInCells.Y));
	}

	namespace
	{
		FLayoutValidationAssertionRecord MakeSnapshotAssertionRecord(
			const FLayoutId AssertionId,
			const ELayoutValidationAssertionKind AssertionKind,
			const bool bPassed,
			const TArray<FLayoutId>& RelatedIds,
			const FString& FailureReason = FString())
		{
			FLayoutValidationAssertionRecord Assertion;
			Assertion.AssertionId = AssertionId;
			Assertion.AssertionKind = AssertionKind;
			Assertion.bPassed = bPassed;
			Assertion.RelatedIds = RelatedIds;
			Assertion.FailureReason = FailureReason;
			return Assertion;
		}

		void PopulateRequestBase(
			FLayoutRegionSolveRequest& Request,
			const int32 SnapshotSchemaVersion,
			const int32 Seed,
			const FString& RegionDebugPath,
			const FLayoutSolverExecutionSettings& ExecutionSettings,
			const FLayoutId RootPlacementPolicyId,
			const FLayoutId RootCandidateId,
			const int32 TemplatePlacementZOffsetBlocks,
			const FLayoutId RootSolveId,
			const ELayoutWorldBindingPlacementKind RootPlacementKind,
			const FLayoutWorldBindingPlacementPolicy& WorldBindingPlacementPolicy,
			const FLayoutSteppedTerrainSupportMap* SteppedTerrainSupportMap)
		{
			LayoutRegionRequestSnapshotBuilder::PopulateStandaloneRequestBase(
				Request,
				SnapshotSchemaVersion,
				Seed,
				RegionDebugPath,
				ExecutionSettings,
				RootPlacementPolicyId,
				RootCandidateId,
				RootSolveId,
				TemplatePlacementZOffsetBlocks,
				RootPlacementKind,
				WorldBindingPlacementPolicy);
			if (SteppedTerrainSupportMap != nullptr)
			{
				Request.SteppedTerrainSupportMap = *SteppedTerrainSupportMap;
			}
		}
	}

	FLayoutRegionSolveRequest BuildRequestFromContentSet(
		const int32 SnapshotSchemaVersion,
		const ULayoutRegionContentSetAsset* ContentSet,
		const ULayoutProfileAsset* Profile,
		const int32 Seed,
		const FString& RegionDebugPath,
		const FLayoutSolverExecutionSettings& ExecutionSettings,
		const FLayoutId RootPlacementPolicyId,
		const FLayoutId RootCandidateId,
		const int32 TemplatePlacementZOffsetBlocks,
		const FLayoutId RootSolveId,
		const ELayoutWorldBindingPlacementKind RootPlacementKind,
		const FLayoutWorldBindingPlacementPolicy& WorldBindingPlacementPolicy,
		const FLayoutSteppedTerrainSupportMap* SteppedTerrainSupportMap,
		const FIntVector* SharedCellSizeOverride)
	{
		FLayoutRegionSolveRequest Request;
		PopulateRequestBase(
			Request,
			SnapshotSchemaVersion,
			Seed,
			RegionDebugPath,
			ExecutionSettings,
			RootPlacementPolicyId,
			RootCandidateId,
			TemplatePlacementZOffsetBlocks,
			RootSolveId,
			RootPlacementKind,
			WorldBindingPlacementPolicy,
			SteppedTerrainSupportMap);
		if (Profile != nullptr && ContentSet != Profile->ContentSet.Get())
		{
			Request.ProfileSnapshot = FLayoutProfileSolver::BuildProfileSnapshot(Profile);
			Request.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
				TEXT("RegionRequest.ProfileContentSetBindingValid"),
				ELayoutValidationAssertionKind::RequestContractValid,
				false,
				{Request.ProfileSnapshot.SnapshotId, ContentSet != nullptr ? ContentSet->GetFName() : NAME_None},
				FString::Printf(
					TEXT("Region solve request content set '%s' must match profile '%s' owned content set '%s'."),
					*GetNameSafe(ContentSet),
					*Profile->GetName(),
					*GetNameSafe(Profile->ContentSet.Get()))));
			return Request;
		}
		Request.ContentSetSnapshot = FLayoutProfileSolver::BuildContentSetSnapshot(ContentSet);
		if (SharedCellSizeOverride != nullptr && *SharedCellSizeOverride != FIntVector::ZeroValue)
		{
			Request.ContentSetSnapshot.SharedCellSizeInBlocks = *SharedCellSizeOverride;
		}
		Request.ModuleCatalog = LayoutContentSetSolveSnapshotBuilder::BuildModuleCatalogFromContentSet(
			SnapshotSchemaVersion,
			ContentSet,
			SharedCellSizeOverride);
		Request.ProfileSnapshot = FLayoutProfileSolver::BuildProfileSnapshot(Profile);
		Request.FootprintSize = SelectFootprintSize(Request.ProfileSnapshot, Seed);
		LayoutRegionRequestSnapshotBuilder::FinalizeStandaloneRequestSnapshots(Request);
		return Request;
	}

	FLayoutRegionSolveRequest BuildRequestFromProfile(
		const int32 SnapshotSchemaVersion,
		const ULayoutProfileAsset* Profile,
		const int32 Seed,
		const FString& RegionDebugPath,
		const FLayoutSolverExecutionSettings& ExecutionSettings,
		const FLayoutId RootPlacementPolicyId,
		const FLayoutId RootCandidateId,
		const int32 TemplatePlacementZOffsetBlocks,
		const FLayoutId RootSolveId,
		const ELayoutWorldBindingPlacementKind RootPlacementKind,
		const FLayoutWorldBindingPlacementPolicy& WorldBindingPlacementPolicy,
		const FLayoutSteppedTerrainSupportMap* SteppedTerrainSupportMap,
		const FIntVector* SharedCellSizeOverride)
	{
		FLayoutRegionSolveRequest Request;
		PopulateRequestBase(
			Request,
			SnapshotSchemaVersion,
			Seed,
			RegionDebugPath,
			ExecutionSettings,
			RootPlacementPolicyId,
			RootCandidateId,
			TemplatePlacementZOffsetBlocks,
			RootSolveId,
			RootPlacementKind,
			WorldBindingPlacementPolicy,
			SteppedTerrainSupportMap);
		LayoutRegionRequestSnapshotBuilder::AppendStandaloneRequestBaseAssertions(Request);
		if (Profile == nullptr)
		{
			Request.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
				TEXT("RegionRequest.ProfilePresent"),
				ELayoutValidationAssertionKind::SnapshotSourcePresent,
				false,
				{},
				TEXT("Region solve request requires a layout profile.")));
			return Request;
		}

		Request.ProfileSnapshot = FLayoutProfileSolver::BuildProfileSnapshot(Profile);
		if (Profile->ContentSet == nullptr)
		{
			Request.ValidationAssertions.Append(Request.ProfileSnapshot.ValidationAssertions);
			Request.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
				TEXT("RegionRequest.ProfileContentSetPresent"),
				ELayoutValidationAssertionKind::SnapshotSourcePresent,
				false,
				{Request.ProfileSnapshot.SnapshotId},
				FString::Printf(
					TEXT("Layout profile '%s' does not reference a content set. Assign ContentSet on the profile."),
					*Profile->GetName())));
			return Request;
		}

		return BuildRequestFromContentSet(
			SnapshotSchemaVersion,
			Profile->ContentSet,
			Profile,
			Seed,
			RegionDebugPath,
			ExecutionSettings,
			RootPlacementPolicyId,
			RootCandidateId,
			TemplatePlacementZOffsetBlocks,
			RootSolveId,
			RootPlacementKind,
			WorldBindingPlacementPolicy,
			SteppedTerrainSupportMap,
			SharedCellSizeOverride);
	}
}
