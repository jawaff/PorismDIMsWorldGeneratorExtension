// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Contracts/LayoutContractManifestCache.h"

#include "Layout/Contracts/LayoutContractPipeline.h"

namespace
{
	constexpr uint64 LayoutManifestCacheFnvOffsetBasis = 14695981039346656037ull;
	constexpr uint64 LayoutManifestCacheFnvPrime = 1099511628211ull;

	void AppendManifestCacheStableString(uint64& InOutHash, const FString& Value)
	{
		for (const TCHAR Character : Value)
		{
			InOutHash ^= static_cast<uint64>(Character);
			InOutHash *= LayoutManifestCacheFnvPrime;
		}
		InOutHash ^= static_cast<uint64>('|');
		InOutHash *= LayoutManifestCacheFnvPrime;
	}

	void AppendManifestCacheStableInt(uint64& InOutHash, const int64 Value)
	{
		AppendManifestCacheStableString(InOutHash, LexToString(Value));
	}

	void AppendManifestCacheStableName(uint64& InOutHash, const FLayoutId Value)
	{
		AppendManifestCacheStableString(InOutHash, Value.ToString());
	}

	void AppendManifestCacheStablePath(uint64& InOutHash, const FSoftObjectPath& Value)
	{
		AppendManifestCacheStableString(InOutHash, Value.ToString());
	}

	FLayoutId MakeManifestCacheStableId(const FString& Prefix, const uint64 Hash)
	{
		return FLayoutId(*FString::Printf(TEXT("%s.%016llX"), *Prefix, static_cast<unsigned long long>(Hash)));
	}

	TArray<FLayoutId> MakeSortedUniqueNames(const TArray<FLayoutId>& Names)
	{
		TArray<FLayoutId> SortedNames = Names;
		SortedNames.Sort([](const FLayoutId Left, const FLayoutId Right)
		{
			return Left.LexicalLess(Right);
		});
		for (int32 Index = SortedNames.Num() - 1; Index > 0; --Index)
		{
			if (SortedNames[Index] == SortedNames[Index - 1])
			{
				SortedNames.RemoveAt(Index);
			}
		}
		return SortedNames;
	}

	void AppendManifestCacheStableNameArray(uint64& InOutHash, const TArray<FLayoutId>& Names)
	{
		for (const FLayoutId Name : MakeSortedUniqueNames(Names))
		{
			AppendManifestCacheStableName(InOutHash, Name);
		}
	}

	FLayoutId BuildPlacementPolicySignatureId(const FLayoutWorldBindingPlacementPolicy& PlacementPolicy)
	{
		uint64 Hash = LayoutManifestCacheFnvOffsetBasis;
		AppendManifestCacheStableString(Hash, TEXT("PlacementPolicy"));
		AppendManifestCacheStableInt(Hash, PlacementPolicy.TerrainSampleGridSpacing);
		AppendManifestCacheStableInt(Hash, PlacementPolicy.HeightIgnoreThreshold);
		AppendManifestCacheStableInt(Hash, PlacementPolicy.TerrainTransition.bAllowFoundationFill ? 1 : 0);
		AppendManifestCacheStableInt(Hash, PlacementPolicy.TerrainTransition.MaxFoundationDepth);
		AppendManifestCacheStableInt(Hash, PlacementPolicy.TerrainTransition.bAllowPerimeterRampTransition ? 1 : 0);
		return MakeManifestCacheStableId(TEXT("PlacementPolicy"), Hash);
	}

	FLayoutId BuildProfileCountContractId(const FLayoutProfileSolveSnapshot& ProfileSnapshot)
	{
		uint64 Hash = LayoutManifestCacheFnvOffsetBasis;
		AppendManifestCacheStableString(Hash, TEXT("ProfileCountContract"));
		AppendManifestCacheStableInt(Hash, static_cast<int64>(ProfileSnapshot.EntryCountMode));
		AppendManifestCacheStableInt(Hash, ProfileSnapshot.EntryCount);
		AppendManifestCacheStableInt(Hash, ProfileSnapshot.MinEntryCount);
		AppendManifestCacheStableInt(Hash, ProfileSnapshot.MaxEntryCount);
		AppendManifestCacheStableInt(Hash, static_cast<int64>(ProfileSnapshot.VerticalAccessCountMode));
		AppendManifestCacheStableInt(Hash, ProfileSnapshot.VerticalAccessCount);
		AppendManifestCacheStableInt(Hash, ProfileSnapshot.MinVerticalAccessCount);
		AppendManifestCacheStableInt(Hash, ProfileSnapshot.MaxVerticalAccessCount);
		return MakeManifestCacheStableId(TEXT("ProfileCounts"), Hash);
	}

	FIntVector ResolveManifestCacheSharedCellSizeInBlocks(const FLayoutFrozenRequestManifestArtifact& Artifact)
	{
		if (Artifact.ContentSetSnapshot.SharedCellSizeInBlocks != FIntVector::ZeroValue)
		{
			return Artifact.ContentSetSnapshot.SharedCellSizeInBlocks;
		}
		return Artifact.ModuleCatalog.SharedCellSizeInBlocks;
	}

	int32 ResolveManifestCacheExactVerticalAccessCount(const FLayoutProfileSolveSnapshot& ProfileSnapshot)
	{
		return ProfileSnapshot.VerticalAccessCountMode == ELayoutCountConstraintMode::Exact
			? ProfileSnapshot.VerticalAccessCount
			: INDEX_NONE;
	}

	void AppendManifestCacheUniqueAssertionSummary(
		TArray<FLayoutContractManifestValidationAssertion>& InOutSummaries,
		const FLayoutValidationAssertionRecord& AssertionRecord)
	{
		for (const FLayoutContractManifestValidationAssertion& ExistingSummary : InOutSummaries)
		{
			if (ExistingSummary.AssertionId == AssertionRecord.AssertionId)
			{
				return;
			}
		}

		FLayoutContractManifestValidationAssertion& Summary = InOutSummaries.AddDefaulted_GetRef();
		Summary.AssertionId = AssertionRecord.AssertionId;
		Summary.AssertionKind = AssertionRecord.AssertionKind;
		Summary.bPassed = AssertionRecord.bPassed;
		Summary.RelatedIds = AssertionRecord.RelatedIds;
	}

	TArray<FLayoutContractManifestValidationAssertion> BuildManifestCacheValidationAssertions(const FLayoutFrozenRequestManifestArtifact& Artifact)
	{
		TArray<FLayoutContractManifestValidationAssertion> Summaries;
		auto AppendAssertions = [&Summaries](const TArray<FLayoutValidationAssertionRecord>& AssertionRecords)
		{
			for (const FLayoutValidationAssertionRecord& AssertionRecord : AssertionRecords)
			{
				AppendManifestCacheUniqueAssertionSummary(Summaries, AssertionRecord);
			}
		};

		AppendAssertions(Artifact.ValidationAssertions);
		AppendAssertions(Artifact.ProfileSnapshot.ValidationAssertions);
		AppendAssertions(Artifact.ContentSetSnapshot.ValidationAssertions);
		AppendAssertions(Artifact.ModuleCatalog.ValidationAssertions);
		for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : Artifact.ContentSetSnapshot.Entries)
		{
			if (EntrySnapshot.CompiledChildRequestTemplate.IsValid())
			{
				AppendAssertions(EntrySnapshot.CompiledChildRequestTemplate->ValidationAssertions);
			}
		}
		for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : Artifact.ModuleCatalog.Modules)
		{
			AppendAssertions(ModuleSnapshot.ValidationAssertions);
		}
		return Summaries;
	}

	TArray<FLayoutContractManifestContentEntrySummary> BuildManifestCacheContentEntrySummaries(const FLayoutFrozenRequestManifestArtifact& Artifact)
	{
		TArray<FLayoutContractManifestContentEntrySummary> Summaries;
		Summaries.Reserve(Artifact.ContentSetSnapshot.Entries.Num());
		for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : Artifact.ContentSetSnapshot.Entries)
		{
			FLayoutContractManifestContentEntrySummary& Summary = Summaries.AddDefaulted_GetRef();
			Summary.EntryId = EntrySnapshot.EntryId;
			Summary.ContentKind = EntrySnapshot.ContentKind;
			Summary.Weight = EntrySnapshot.Weight;
			Summary.ProvidedZoneFeatures = EntrySnapshot.ProvidedZoneFeatures;
			Summary.ClosureProviderIntents = EntrySnapshot.ClosureProviderIntents;
			Summary.SeamProviderIntents = EntrySnapshot.SeamProviderIntents;
			Summary.ModuleSnapshotIndex = EntrySnapshot.ModuleSnapshotIndex;
			Summary.ModulePlacementZone = EntrySnapshot.ModulePlacementZone;
			Summary.ModuleLevelPlacementPolicy = EntrySnapshot.ModuleLevelPlacementPolicy;
			Summary.ModuleSpecificLevel = EntrySnapshot.ModuleSpecificLevel;
			Summary.bModuleOptional = EntrySnapshot.bModuleOptional;
			Summary.ChildProfileSnapshotId = EntrySnapshot.ChildProfileSnapshotId;
			Summary.ChildProfilePath = EntrySnapshot.ChildProfilePath;
			Summary.ChildContentSetSnapshotId = EntrySnapshot.ChildContentSetSnapshotId;
			Summary.ChildPlacementZone = EntrySnapshot.ChildPlacementZone;
			Summary.ChildLevelPlacementPolicy = EntrySnapshot.ChildLevelPlacementPolicy;
			Summary.ChildSpecificLevel = EntrySnapshot.ChildSpecificLevel;
			Summary.bChildOptional = EntrySnapshot.bChildOptional;
			Summary.bChildContributesHostVerticalAccess = EntrySnapshot.bChildContributesHostVerticalAccess;
		}
		return Summaries;
	}

	TArray<FLayoutContractManifestModuleSummary> BuildManifestCacheModuleSummaries(const FLayoutFrozenRequestManifestArtifact& Artifact)
	{
		TArray<FLayoutContractManifestModuleSummary> Summaries;
		Summaries.Reserve(Artifact.ModuleCatalog.Modules.Num());
		for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : Artifact.ModuleCatalog.Modules)
		{
			FLayoutContractManifestModuleSummary& Summary = Summaries.AddDefaulted_GetRef();
			Summary.SnapshotId = ModuleSnapshot.SnapshotId;
			Summary.DebugName = ModuleSnapshot.DebugName;
			Summary.TemplatePath = ModuleSnapshot.Template.ToSoftObjectPath();
			Summary.BoundsCells = ModuleSnapshot.BoundsCells;
			Summary.OccupiedLocalCells = ModuleSnapshot.OccupiedLocalCells;
			Summary.Roles = ModuleSnapshot.Roles;
			Summary.TraversalChannels = ModuleSnapshot.TraversalChannels;
			Summary.SourceContentEntryId = ModuleSnapshot.SourceContentEntryId;
			Summary.PlacementZone = ModuleSnapshot.PlacementZone;
			Summary.LevelPlacementPolicy = ModuleSnapshot.LevelPlacementPolicy;
			Summary.SpecificLevel = ModuleSnapshot.SpecificLevel;
			Summary.bOptional = ModuleSnapshot.bOptional;
			Summary.bCompositeSnapshot = ModuleSnapshot.SourceCompositeModule != nullptr;
		}
		return Summaries;
	}

	TArray<FLayoutId> BuildManifestCacheClosureRequirementIds(const FLayoutProfileSolveSnapshot& ProfileSnapshot)
	{
		TArray<FLayoutId> RequirementIds;
		RequirementIds.Reserve(ProfileSnapshot.ClosureRequirements.Num());
		for (const FLayoutClosureRequirement& Requirement : ProfileSnapshot.ClosureRequirements)
		{
			RequirementIds.Add(Requirement.ClosureId);
		}
		return RequirementIds;
	}

	TArray<FLayoutId> BuildManifestCacheZoneFeatureRequirementIds(const FLayoutProfileSolveSnapshot& ProfileSnapshot)
	{
		TArray<FLayoutId> RequirementIds;
		RequirementIds.Reserve(ProfileSnapshot.ZoneFeatureRequirements.Num());
		for (const FLayoutZoneFeatureRequirement& Requirement : ProfileSnapshot.ZoneFeatureRequirements)
		{
			RequirementIds.Add(Requirement.RequirementId);
		}
		return RequirementIds;
	}

	FLayoutContractManifest BuildManifestCacheManifestFromArtifact(const FLayoutFrozenRequestManifestArtifact& Artifact)
	{
		FLayoutContractManifest Manifest;
		Manifest.ManifestId = Artifact.ManifestId;
		Manifest.ProfileSnapshotId = Artifact.ProfileSnapshot.SnapshotId;
		Manifest.ProfileSourcePath = !Artifact.ProfilePath.IsNull()
			? Artifact.ProfilePath
			: Artifact.ProfileSnapshot.SourceProfilePath;
		Manifest.ContentSetSnapshotId = Artifact.ContentSetSnapshot.SnapshotId;
		Manifest.ModuleCatalogId = Artifact.ModuleCatalog.SnapshotId;
		Manifest.WorldBindingId = Artifact.WorldBindingId;
		Manifest.SharedCellSizeInBlocks = ResolveManifestCacheSharedCellSizeInBlocks(Artifact);
		Manifest.MinimumFootprintInCells = Artifact.ProfileSnapshot.MinimumFootprintInCells;
		Manifest.MaximumFootprintInCells = Artifact.ProfileSnapshot.MaximumFootprintInCells;
		Manifest.LevelCount = Artifact.ProfileSnapshot.LevelCount;
		Manifest.bSupportsSteppedTerrainSolve = Artifact.ProfileSnapshot.bSupportsSteppedTerrainSolve;
		Manifest.bEnableTerrainSeams = Artifact.ProfileSnapshot.bEnableTerrainSeams;
		Manifest.RequiredVerticalAccessCount = ResolveManifestCacheExactVerticalAccessCount(Artifact.ProfileSnapshot);
		Manifest.ClosureRequirementIds = BuildManifestCacheClosureRequirementIds(Artifact.ProfileSnapshot);
		Manifest.ZoneFeatureRequirementIds = BuildManifestCacheZoneFeatureRequirementIds(Artifact.ProfileSnapshot);
		Manifest.ValidationAssertions = BuildManifestCacheValidationAssertions(Artifact);
		Manifest.ContentEntries = BuildManifestCacheContentEntrySummaries(Artifact);
		Manifest.Modules = BuildManifestCacheModuleSummaries(Artifact);
		return Manifest;
	}
}

FLayoutContractManifestCacheKey FLayoutContractManifestCache::BuildKeyFromSolveRequest(const FLayoutRegionSolveRequest& SolveRequest)
{
	FLayoutContractManifestCacheKey Key;
	Key.EffectiveSnapshotId = SolveRequest.EffectiveSnapshotId;
	Key.ProfileSnapshotId = SolveRequest.ProfileSnapshot.SnapshotId;
	Key.ProfileCountContractId = BuildProfileCountContractId(SolveRequest.ProfileSnapshot);
	Key.ProfilePath = !SolveRequest.ProfilePath.IsNull()
		? SolveRequest.ProfilePath
		: SolveRequest.ProfileSnapshot.SourceProfilePath;
	Key.ContentSetSnapshotId = SolveRequest.ContentSetSnapshot.SnapshotId;
	Key.ModuleCatalogId = SolveRequest.ModuleCatalog.SnapshotId;
	Key.WorldBindingId = SolveRequest.WorldBindingId;
	Key.RootSolveId = SolveRequest.RootSolveId;
	Key.RootCandidateId = SolveRequest.RootCandidateId;
	Key.RootPlacementPolicyId = SolveRequest.RootPlacementPolicyId;
	Key.PlacementPolicySignatureId = BuildPlacementPolicySignatureId(SolveRequest.WorldBindingPlacementPolicy);
	Key.RootPlacementKind = SolveRequest.RootPlacementKind;
	Key.ContinuationFamilyId = SolveRequest.RootContinuationSelection.FamilyId;
	Key.ContinuationPlacementKind = SolveRequest.RootContinuationSelection.PlacementKind;
	Key.ContinuationResolvedEntryLevel = SolveRequest.RootContinuationSelection.ResolvedEntryLevel;
	Key.SourceContentEntryId = SolveRequest.SourceContentEntryId;
	Key.DelegatedZoneFeatureRequirementIds = MakeSortedUniqueNames(SolveRequest.DelegatedZoneFeatureRequirementIds);
	for (const FLayoutZoneFeatureProviderCommitment& Commitment :
		SolveRequest.PrecommittedZoneFeatureProviderCommitments)
	{
		Key.PrecommittedZoneFeatureProviderCommitmentIds.AddUnique(
			Commitment.ProviderCommitmentId);
	}
	Key.PrecommittedZoneFeatureProviderCommitmentIds.Sort(
		[](const FLayoutId Left, const FLayoutId Right)
		{
			return Left.LexicalLess(Right);
		});
	Key.DelegatedClosureRequirementIds = MakeSortedUniqueNames(SolveRequest.DelegatedClosureRequirementIds);
	Key.SnapshotSchemaVersion = SolveRequest.SnapshotSchemaVersion;
	Key.bEnableTerrainSeams = SolveRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve
		&& SolveRequest.ProfileSnapshot.bEnableTerrainSeams;
	Key.KeyId = BuildKeyId(Key);
	return Key;
}

FLayoutId FLayoutContractManifestCache::BuildKeyId(const FLayoutContractManifestCacheKey& Key)
{
	uint64 Hash = LayoutManifestCacheFnvOffsetBasis;
	AppendManifestCacheStableString(Hash, TEXT("ContractManifestCacheKey"));
	AppendManifestCacheStableName(Hash, Key.EffectiveSnapshotId);
	AppendManifestCacheStableName(Hash, Key.ProfileSnapshotId);
	AppendManifestCacheStableName(Hash, Key.ProfileCountContractId);
	AppendManifestCacheStablePath(Hash, Key.ProfilePath);
	AppendManifestCacheStableName(Hash, Key.ContentSetSnapshotId);
	AppendManifestCacheStableName(Hash, Key.ModuleCatalogId);
	AppendManifestCacheStableName(Hash, Key.WorldBindingId);
	AppendManifestCacheStableName(Hash, Key.RootSolveId);
	AppendManifestCacheStableName(Hash, Key.RootCandidateId);
	AppendManifestCacheStableName(Hash, Key.RootPlacementPolicyId);
	AppendManifestCacheStableName(Hash, Key.PlacementPolicySignatureId);
	AppendManifestCacheStableInt(Hash, static_cast<int64>(Key.RootPlacementKind));
	AppendManifestCacheStableName(Hash, Key.ContinuationFamilyId);
	AppendManifestCacheStableInt(Hash, static_cast<int64>(Key.ContinuationPlacementKind));
	AppendManifestCacheStableInt(Hash, Key.ContinuationResolvedEntryLevel);
	AppendManifestCacheStableName(Hash, Key.SourceContentEntryId);
	AppendManifestCacheStableNameArray(Hash, Key.DelegatedZoneFeatureRequirementIds);
	AppendManifestCacheStableNameArray(Hash, Key.PrecommittedZoneFeatureProviderCommitmentIds);
	AppendManifestCacheStableNameArray(Hash, Key.DelegatedClosureRequirementIds);
	AppendManifestCacheStableInt(Hash, Key.SnapshotSchemaVersion);
	AppendManifestCacheStableInt(Hash, Key.bEnableTerrainSeams ? 1 : 0);
	return MakeManifestCacheStableId(TEXT("ContractManifestCache"), Hash);
}

FLayoutFrozenRequestManifestArtifact FLayoutContractManifestCache::BuildFrozenRequestManifestArtifact(const FLayoutRegionSolveRequest& SolveRequest)
{
	FLayoutFrozenRequestManifestArtifact Artifact;
	Artifact.Key = BuildKeyFromSolveRequest(SolveRequest);
	Artifact.ManifestId = !SolveRequest.EffectiveSnapshotId.IsNone()
		? SolveRequest.EffectiveSnapshotId
		: SolveRequest.ProfileSnapshot.SnapshotId;
	Artifact.WorldBindingId = SolveRequest.WorldBindingId;
	Artifact.ProfilePath = !SolveRequest.ProfilePath.IsNull()
		? SolveRequest.ProfilePath
		: SolveRequest.ProfileSnapshot.SourceProfilePath;
	Artifact.ProfileSnapshot = SolveRequest.ProfileSnapshot;
	Artifact.ContentSetSnapshot = SolveRequest.ContentSetSnapshot;
	Artifact.ModuleCatalog = SolveRequest.ModuleCatalog;
	Artifact.ValidationAssertions = SolveRequest.ValidationAssertions;
	return Artifact;
}

bool FLayoutContractManifestCache::ValidateFrozenRequestManifestArtifact(
	const FLayoutFrozenRequestManifestArtifact& Artifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (Artifact.Key.KeyId.IsNone())
	{
		OutFailureReason = TEXT("Frozen request manifest artifact is missing cache key id.");
		return false;
	}
	if (Artifact.ManifestId.IsNone())
	{
		OutFailureReason = TEXT("Frozen request manifest artifact is missing manifest id.");
		return false;
	}
	if (Artifact.ProfileSnapshot.SnapshotId.IsNone())
	{
		OutFailureReason = TEXT("Frozen request manifest artifact is missing profile snapshot id.");
		return false;
	}
	if (Artifact.ContentSetSnapshot.SnapshotId.IsNone()
		&& Artifact.ModuleCatalog.SnapshotId.IsNone())
	{
		OutFailureReason = TEXT("Frozen request manifest artifact is missing content/module snapshot id.");
		return false;
	}
	if (Artifact.ProfileSnapshot.SourceProfile != nullptr)
	{
		OutFailureReason = TEXT("Frozen request manifest artifact still carries a live profile source.");
		return false;
	}
	if (Artifact.ContentSetSnapshot.SourceContentSet != nullptr)
	{
		OutFailureReason = TEXT("Frozen request manifest artifact still carries a live content-set source.");
		return false;
	}
	for (int32 ModuleIndex = 0; ModuleIndex < Artifact.ModuleCatalog.Modules.Num(); ++ModuleIndex)
	{
		const FLayoutModuleSolveSnapshot& ModuleSnapshot = Artifact.ModuleCatalog.Modules[ModuleIndex];
		if (ModuleSnapshot.SourceModule != nullptr || ModuleSnapshot.SourceCompositeModule != nullptr)
		{
			OutFailureReason = FString::Printf(
				TEXT("Frozen request manifest artifact module snapshot %d still carries live module/composite sources."),
				ModuleIndex);
			return false;
		}
	}
	return true;
}

bool FLayoutContractManifestCache::FindOrAddManifest(
	const FLayoutFrozenRequestManifestArtifact& Artifact,
	FLayoutContractManifestCacheEntry& OutEntry,
	bool& bOutCacheHit)
{
	FString ValidationFailureReason;
	if (!ValidateFrozenRequestManifestArtifact(Artifact, ValidationFailureReason))
	{
		OutEntry = FLayoutContractManifestCacheEntry();
		bOutCacheHit = false;
		return false;
	}

	if (const FLayoutContractManifestCacheEntry* ExistingEntry = EntriesByKeyId.Find(Artifact.Key.KeyId))
	{
		OutEntry = *ExistingEntry;
		bOutCacheHit = true;
		return true;
	}

	FLayoutContractManifestCacheEntry NewEntry;
	NewEntry.Key = Artifact.Key;
	NewEntry.Manifest = BuildManifestCacheManifestFromArtifact(Artifact);
	EntriesByKeyId.Add(Artifact.Key.KeyId, NewEntry);
	OutEntry = NewEntry;
	bOutCacheHit = false;
	return true;
}

bool FLayoutContractManifestCache::FindOrAddManifest(
	const FLayoutRegionSolveRequest& SolveRequest,
	FLayoutContractManifestCacheEntry& OutEntry,
	bool& bOutCacheHit)
{
	const FLayoutFrozenRequestManifestArtifact Artifact = BuildFrozenRequestManifestArtifact(SolveRequest);
	return FindOrAddManifest(Artifact, OutEntry, bOutCacheHit);
}

void FLayoutContractManifestCache::Reset()
{
	EntriesByKeyId.Reset();
}

int32 FLayoutContractManifestCache::Num() const
{
	return EntriesByKeyId.Num();
}
