// Copyright 2026 Spotted Loaf Studio
#pragma once

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Types/LayoutGameplayTags.h"

namespace SparseStructuralTestFixture
{
	/** One-cell child with an exact, empty-compatible egress. Caller solves and retains the request. */
	inline FLayoutRegionSolveRequest BuildChildRequest(const FIntVector& Offset)
	{
		using namespace PorismLayoutTestUtilities;
		ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(GetTransientPackage(),
			TEXT("SparseProofChild"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 1, false);
		Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::None;
		FLayoutRegionContentEntry ChildOnlyEntry;
		ChildOnlyEntry.EntryId = TEXT("ChildOnlyInteriorEntry");
		ChildOnlyEntry.ContentKind = ELayoutRegionContentKind::Module;
		ChildOnlyEntry.ModuleSettings.Module = DuplicateObject<ULayoutModuleAsset>(
			Profile->ContentSet->Entries[0].ModuleSettings.Module, GetTransientPackage(), TEXT("ChildOnlyInterior"));
		Profile->ContentSet->Entries.Add(ChildOnlyEntry);
		Profile->ContentSet->Entries[0].ModuleSettings.Module->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 173, TEXT("SparseProofParent/Child"));
		Request.RegionCellOffset = Offset;
		FLayoutPlannedCell Cell;
		Cell.Cell = FIntVector::ZeroValue;
		Cell.Intent = ELayoutCellIntent::Entry;
		Cell.PlacementZone = ELayoutPlacementZone::Perimeter;
		Cell.EntryOrigin = ELayoutEntryOrigin::AuthoredBoundary;
		Request.PlannedCells = {Cell};
		Request.PrecomputedPlannedCells = Request.PlannedCells;
		FLayoutCellCandidateDomainRestriction Exact;
		Exact.Cell = Cell.Cell;
		Exact.RestrictionId = TEXT("Child.Exact");
		for (const FLayoutModuleSolveSnapshot& Snapshot : Request.ModuleCatalog.Modules)
		{
			if (Snapshot.DebugName.ToString().Contains(TEXT("GenericModule"))) Exact.AllowedCandidates.Add({Snapshot.SnapshotId, 0});
		}
		Request.CandidateDomainRestrictions.Add(Exact);
		FLayoutCommittedEndpointAnchor Endpoint;
		Endpoint.CommitmentId = TEXT("Child.Egress");
		Endpoint.FaceDirection = ELayoutFaceDirection::NegX;
		Endpoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
		Endpoint.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
		Endpoint.TraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
		Request.CommittedEndpointAnchors.Add(Endpoint);
		return Request;
	}
}
