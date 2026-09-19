// Copyright 2026 Spotted Loaf Studio

#include "Layout/Solver/LayoutSparsePlacementRuleSnapshotBuilder.h"

#include "Layout/Assets/LayoutProfileAsset.h"

namespace LayoutSparsePlacementRuleSnapshotBuilder
{
	void PopulateSparsePlacementRuleSnapshotBase(
		FLayoutSparsePlacementRuleSolveSnapshot& Snapshot,
		const FLayoutSparsePlacementRuleBase& Rule,
		const ELayoutSparsePlacementRuleKind RuleKind,
		const ELayoutSparseCandidateSource CandidateSource,
		const int32 Count,
		const int32 MinCount,
		const int32 MaxCount,
		const int32 MinSpacingCells,
		const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot,
		const FLayoutModuleCatalog& ModuleCatalog)
	{
		Snapshot.RuleId = Rule.RuleId;
		Snapshot.RuleKind = RuleKind;
		Snapshot.CandidateSource = CandidateSource;
		Snapshot.ContentSetSnapshot = ContentSetSnapshot;
		Snapshot.ModuleCatalog = ModuleCatalog;
		Snapshot.Count = Count;
		Snapshot.MinCount = MinCount;
		Snapshot.MaxCount = MaxCount;
		Snapshot.PlacementZone = Rule.PlacementZone;
		Snapshot.LevelPlacementPolicy = Rule.LevelPlacementPolicy;
		Snapshot.SpecificLevel = Rule.SpecificLevel;
		Snapshot.MinSpacingCells = MinSpacingCells;
		Snapshot.Validation.Messages.Append(Snapshot.ContentSetSnapshot.Validation.Messages);
		Snapshot.Validation.Messages.Append(Snapshot.ModuleCatalog.Validation.Messages);
		Snapshot.ProofRecords.Append(Snapshot.ContentSetSnapshot.ProofRecords);
		Snapshot.ProofRecords.Append(Snapshot.ModuleCatalog.ProofRecords);
		Snapshot.ValidationAssertions.Append(Snapshot.ContentSetSnapshot.ValidationAssertions);
		Snapshot.ValidationAssertions.Append(Snapshot.ModuleCatalog.ValidationAssertions);
	}
}
