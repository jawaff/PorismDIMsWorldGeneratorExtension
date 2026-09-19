// Copyright 2026 Spotted Loaf Studio

#include "Layout/Planning/LayoutActiveBiomeSampling.h"
#include "Layout/Async/LayoutBackgroundSolveCancellation.h"

#include "Engine/DataTable.h"

namespace
{
	constexpr const char* ActiveBiomeConstantPositiveFastNoise = "AAAAAIA/";

	FastNoise::SmartNode<> MakeConstantPositiveNode()
	{
		return FastNoise::NewFromEncodedNodeTree(ActiveBiomeConstantPositiveFastNoise);
	}

	FastNoise::SmartNode<> CompileConfiguredNode(
		UObject* const Creator,
		const FString& EncodedNode,
		const TSubclassOf<UFastNoiseEditor>& BlueprintNode,
		UFastNoiseEditor* const RuntimeNode,
		std::vector<FNodeLink>& EditorNodes,
		const bool bAllowPositiveFallback = true)
	{
		if (!EncodedNode.IsEmpty())
		{
			return FastNoise::NewFromEncodedNodeTree(TCHAR_TO_ANSI(*EncodedNode));
		}

		if (BlueprintNode != nullptr)
		{
			UFastNoiseEditor* const NoiseEditor = NewObject<UFastNoiseEditor>(
				Creator != nullptr ? Creator : GetTransientPackage(),
				BlueprintNode,
				NAME_None,
				RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
			if (NoiseEditor != nullptr)
			{
				NoiseEditor->Nodes = &EditorNodes;
				const FNodeLink NodeLink = NoiseEditor->GetNoiseRef(Creator);
				if (NodeLink.Node != nullptr)
				{
					return NodeLink.Node->BaseNode;
				}
			}
		}

		if (RuntimeNode != nullptr)
		{
			RuntimeNode->Nodes = &EditorNodes;
			const FNodeLink NodeLink = RuntimeNode->GetNoiseRef(Creator);
			if (NodeLink.Node != nullptr)
			{
				return NodeLink.Node->BaseNode;
			}
		}

		return bAllowPositiveFallback ? MakeConstantPositiveNode() : FastNoise::SmartNode<>();
	}

	bool HasBiomeInstructions(const FBiomeDualData& Row)
	{
		return !Row.GenU_Mat1.IsEmpty()
			|| !Row.GenU_Mat2.IsEmpty()
			|| !Row.GenU_Mesh1.IsEmpty()
			|| !Row.GenU_Mesh2.IsEmpty()
			|| !Row.GenA_Mat1.IsEmpty()
			|| !Row.GenA_Mat2.IsEmpty()
			|| !Row.GenA_Mesh1.IsEmpty()
			|| !Row.GenA_Mesh2.IsEmpty()
			|| !Row.GenB_Mat1.IsEmpty()
			|| !Row.GenB_Mat2.IsEmpty()
			|| !Row.GenB_Mesh1.IsEmpty()
			|| !Row.GenB_Mesh2.IsEmpty();
	}

	void AddCompiledRow(
		UObject* const Creator,
		const FBiomeDualData& SourceRow,
		const FName SourceRowName,
		const int32 SourceRowIndex,
		std::vector<FNodeLink>& EditorNodes,
		TArray<FLayoutActiveBiomeSampler::FCompiledRow>& OutRows)
	{
		if (SourceRow.BiomeHidden)
		{
			return;
		}

		FLayoutActiveBiomeSampler::FCompiledRow& CompiledRow = OutRows.AddDefaulted_GetRef();
		CompiledRow.RowRef.RowName = SourceRowName.IsNone() ? FName(*SourceRow.BiomeName) : SourceRowName;
		CompiledRow.RowRef.BiomeName = SourceRow.BiomeName;
		CompiledRow.RowRef.SourceRowIndex = SourceRowIndex;
		CompiledRow.DomainOver = SourceRow.DomainOver;
		CompiledRow.Overlap = SourceRow.Overlap;
		CompiledRow.bNoiseOnly = !HasBiomeInstructions(SourceRow);
		CompiledRow.DomainNode = CompileConfiguredNode(
			Creator,
			SourceRow.Domain,
			SourceRow.DomainBP,
			SourceRow.DomainRun,
			EditorNodes);
		CompiledRow.DualSwitchNode = CompileConfiguredNode(
			Creator,
			SourceRow.DualSwitch,
			SourceRow.DualSwitchBP,
			SourceRow.DualSwitchRun,
			EditorNodes);
		CompiledRow.GenANode = CompileConfiguredNode(
			Creator,
			SourceRow.GenA,
			SourceRow.GenABP,
			SourceRow.GenARun,
			EditorNodes);
		CompiledRow.GenBNode = CompileConfiguredNode(
			Creator,
			SourceRow.GenB,
			SourceRow.GenBBP,
			SourceRow.GenBRun,
			EditorNodes);
	}

	template <typename TMatchPredicate>
	bool FindTopOwnedSolidSurfaceInColumn(
		const FLayoutActiveBiomeSampler& Sampler,
		const FIntPoint BlockXY,
		const int32 SearchStartZBlockWorld,
		const int32 SearchDepthBlocks,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		TMatchPredicate&& MatchesSolid,
		FLayoutActiveBiomeSurfaceSample& OutSurface)
	{
		OutSurface = FLayoutActiveBiomeSurfaceSample();
		const int32 SafeDepth = FMath::Max(1, SearchDepthBlocks);
		auto BuildBlockWorldPosition =
			[BlockXY](const int32 ZBlockWorld)
		{
			return FIntVector(BlockXY.X, BlockXY.Y, ZBlockWorld);
		};
		auto TrySampleAtZ =
			[&Sampler, &CoordinateSettings, &BuildBlockWorldPosition](
				const int32 ZBlockWorld,
				FLayoutActiveBiomeSample& OutSample)
		{
			if (LayoutSolveCancellation::IsCurrentThreadCancellationRequested()) return false;
			return Sampler.SampleAtBlockPosition(
				BuildBlockWorldPosition(ZBlockWorld),
				CoordinateSettings,
				OutSample);
		};

		FLayoutActiveBiomeSample StartSample;
		if (!TrySampleAtZ(SearchStartZBlockWorld, StartSample))
		{
			return false;
		}

		if (MatchesSolid(StartSample))
		{
			int32 HighestMatchingZ = SearchStartZBlockWorld;
			FLayoutActiveBiomeSample HighestMatchingSample = StartSample;

			// World-facing surface searches can begin inside tall authored terrain.
			// Recover the highest owned solid reachable within the caller's search
			// budget instead of clipping the surface to the starting Z.
			for (int32 ZOffset = 1; ZOffset < SafeDepth; ++ZOffset)
			{
				const int32 CandidateZ = SearchStartZBlockWorld + ZOffset;
				FLayoutActiveBiomeSample CandidateSample;
				if (!TrySampleAtZ(CandidateZ, CandidateSample))
				{
					return false;
				}

				if (!MatchesSolid(CandidateSample))
				{
					OutSurface.bIsValid = true;
					OutSurface.SurfaceBlockWorldPos =
						BuildBlockWorldPosition(HighestMatchingZ);
					OutSurface.BiomeSample = HighestMatchingSample;
					return true;
				}

				HighestMatchingZ = CandidateZ;
				HighestMatchingSample = CandidateSample;
			}

			OutSurface.bIsValid = true;
			OutSurface.SurfaceBlockWorldPos =
				BuildBlockWorldPosition(HighestMatchingZ);
			OutSurface.BiomeSample = HighestMatchingSample;
			return true;
		}

		for (int32 ZOffset = 0; ZOffset < SafeDepth; ++ZOffset)
		{
			const int32 CandidateZ = SearchStartZBlockWorld - ZOffset;
			FLayoutActiveBiomeSample CandidateSample;
			if (!TrySampleAtZ(CandidateZ, CandidateSample))
			{
				return false;
			}

			if (MatchesSolid(CandidateSample))
			{
				OutSurface.bIsValid = true;
				OutSurface.SurfaceBlockWorldPos =
					BuildBlockWorldPosition(CandidateZ);
				OutSurface.BiomeSample = CandidateSample;
				return true;
			}
		}

		return true;
	}
}

FLayoutActiveBiomeSampler::~FLayoutActiveBiomeSampler()
{
	for (FNodeLink& NodeLink : EditorNodes)
	{
		delete NodeLink.Node;
		NodeLink.Node = nullptr;
	}

	EditorNodes.clear();
}

bool FLayoutActiveBiomeSampler::Initialize(
	UObject* const Creator,
	const UWorldGenDef* const WorldGenDef,
	const int32 InSeed)
{
	CompiledRows.Reset();
	GenOnlyTestNode.reset();
	WorldGenNode.reset();
	BiomeOffsetXNode.reset();
	BiomeOffsetYNode.reset();
	BiomeOffsetZNode.reset();
	Seed = InSeed;

	if (WorldGenDef == nullptr)
	{
		return false;
	}

	GenOnlyTestNode = CompileConfiguredNode(
		Creator,
		WorldGenDef->GenOnlyTest,
		WorldGenDef->GenOnlyTestBP,
		WorldGenDef->GenOnlyTestRun,
		EditorNodes,
		false);
	WorldGenNode = CompileConfiguredNode(
		Creator,
		WorldGenDef->WorldGen,
		WorldGenDef->WorldGenBP,
		WorldGenDef->WorldGenRun,
		EditorNodes);
	BiomeOffsetXNode = CompileConfiguredNode(
		Creator,
		WorldGenDef->WorldBiomeOffsetX,
		WorldGenDef->WorldBiomeOffsetXBP,
		WorldGenDef->WorldBiomeOffsetXRun,
		EditorNodes,
		false);
	BiomeOffsetYNode = CompileConfiguredNode(
		Creator,
		WorldGenDef->WorldBiomeOffsetY,
		WorldGenDef->WorldBiomeOffsetYBP,
		WorldGenDef->WorldBiomeOffsetYRun,
		EditorNodes,
		false);
	BiomeOffsetZNode = CompileConfiguredNode(
		Creator,
		WorldGenDef->WorldBiomeOffsetZ,
		WorldGenDef->WorldBiomeOffsetZBP,
		WorldGenDef->WorldBiomeOffsetZRun,
		EditorNodes,
		false);

	if (WorldGenDef->WorldBiomesDT != nullptr)
	{
		if (WorldGenDef->WorldBiomesDT->GetRowStruct() != FBiomeDualData::StaticStruct())
		{
			return false;
		}

		int32 SourceRowIndex = 0;
		for (const TPair<FName, uint8*>& RowPair : WorldGenDef->WorldBiomesDT->GetRowMap())
		{
			const FBiomeDualData* const SourceRow = reinterpret_cast<const FBiomeDualData*>(RowPair.Value);
			if (SourceRow != nullptr && !SourceRow->BiomeHidden)
			{
				AddCompiledRow(Creator, *SourceRow, RowPair.Key, SourceRowIndex, EditorNodes, CompiledRows);
				++SourceRowIndex;
			}
		}
		return !CompiledRows.IsEmpty();
	}

	int32 SourceRowIndex = 0;
	for (const FBiomeDualData& SourceRow : WorldGenDef->WorldBiomes)
	{
		if (!SourceRow.BiomeHidden)
		{
			AddCompiledRow(Creator, SourceRow, FName(*SourceRow.BiomeName), SourceRowIndex, EditorNodes, CompiledRows);
			++SourceRowIndex;
		}
	}

	return !CompiledRows.IsEmpty();
}

bool FLayoutActiveBiomeSampler::SampleAtBlockPosition(
	const FIntVector BlockWorldPosition,
	const FLayoutNoiseCoordinateSettings& CoordinateSettings,
	FLayoutActiveBiomeSample& OutSample) const
{
	OutSample = FLayoutActiveBiomeSample();
	if (CompiledRows.IsEmpty())
	{
		return false;
	}

	const FVector BaseNoiseCoordinate = ULayoutNoiseCoordinateLibrary::BlockWorldPositionToNoiseCoordinate(
		BlockWorldPosition,
		CoordinateSettings);
	if (GenOnlyTestNode)
	{
		OutSample.bIsValid = true;
		OutSample.bHasTerrainContribution = true;
		OutSample.TerrainValue = EvaluateNodeAtNoiseCoordinate(GenOnlyTestNode, BaseNoiseCoordinate);
		OutSample.bTerrainSolid = !(OutSample.TerrainValue > 0.0f);
		return true;
	}

	const FVector DomainNoiseCoordinate = BaseNoiseCoordinate + FVector(
		BiomeOffsetXNode ? EvaluateNodeAtNoiseCoordinate(BiomeOffsetXNode, BaseNoiseCoordinate) : 0.0f,
		BiomeOffsetYNode ? EvaluateNodeAtNoiseCoordinate(BiomeOffsetYNode, BaseNoiseCoordinate) : 0.0f,
		BiomeOffsetZNode ? EvaluateNodeAtNoiseCoordinate(BiomeOffsetZNode, BaseNoiseCoordinate) : 0.0f);

	int32 WinningRowIndex = INDEX_NONE;
	float BiomePower = 0.0f;
	float TerrainValue = 0.0f;
	bool bSelectedGenA = true;
	for (int32 RowIndex = 0; RowIndex < CompiledRows.Num(); ++RowIndex)
	{
		const FCompiledRow& CandidateRow = CompiledRows[RowIndex];
		const float DomainValue = EvaluateNodeAtNoiseCoordinate(CandidateRow.DomainNode, DomainNoiseCoordinate);
		if (CandidateRow.bNoiseOnly)
		{
			const float DomainWeight = FMath::Clamp(DomainValue, 0.0f, 1.0f);
			if (DomainWeight > 0.0f)
			{
				TerrainValue += EvaluateNodeAtNoiseCoordinate(CandidateRow.GenANode, BaseNoiseCoordinate) * DomainWeight;
				OutSample.bHasTerrainContribution = true;
			}
			continue;
		}

		if (!DoesDomainContribute(DomainValue, CandidateRow.DomainOver, BiomePower))
		{
			continue;
		}

		const float DualSwitchValue = EvaluateNodeAtNoiseCoordinate(CandidateRow.DualSwitchNode, BaseNoiseCoordinate);
		const float GenAWeight = FMath::Max(CandidateRow.Overlap + DualSwitchValue, 0.0f);
		const float GenBWeight = FMath::Max(CandidateRow.Overlap - DualSwitchValue, 0.0f);
		const float RowTerrainValue =
			EvaluateNodeAtNoiseCoordinate(CandidateRow.GenANode, BaseNoiseCoordinate) * GenAWeight
			+ EvaluateNodeAtNoiseCoordinate(CandidateRow.GenBNode, BaseNoiseCoordinate) * GenBWeight;
		const float BlendFactor = FMath::Min(
			FMath::Max(DomainValue + CandidateRow.DomainOver - BiomePower, 0.0f),
			CandidateRow.DomainOver) / CandidateRow.DomainOver;
		TerrainValue += RowTerrainValue * BlendFactor;
		OutSample.bHasTerrainContribution |= GenAWeight > 0.0f || GenBWeight > 0.0f;

		if (DomainValue > BiomePower)
		{
			BiomePower = DomainValue;
			WinningRowIndex = RowIndex;
			bSelectedGenA = DualSwitchValue > 0.0f;
		}
	}

	TerrainValue = BiomePower <= 0.0f
		? 1.0f
		: TerrainValue + EvaluateNodeAtNoiseCoordinate(WorldGenNode, BaseNoiseCoordinate);

	OutSample.bIsValid = true;
	OutSample.TerrainValue = TerrainValue;
	OutSample.bTerrainSolid = !(TerrainValue > 0.0f);
	if (WinningRowIndex != INDEX_NONE)
	{
		const FCompiledRow& WinningRow = CompiledRows[WinningRowIndex];
		OutSample.bAnyPositiveDomain = true;
		OutSample.WinningRow = WinningRow.RowRef;
		OutSample.WinningDomainValue = BiomePower;
		OutSample.bSelectedGenA = bSelectedGenA;
	}
	return true;
}

bool FLayoutActiveBiomeSampler::SampleAtBlockPositionFromAnyRow(
	const FIntVector BlockWorldPosition,
	const FLayoutNoiseCoordinateSettings& CoordinateSettings,
	const TConstArrayView<FName> EligibleBiomeRowNames,
	FLayoutActiveBiomeSample& OutSample) const
{
	if (EligibleBiomeRowNames.IsEmpty()
		|| !SampleAtBlockPosition(BlockWorldPosition, CoordinateSettings, OutSample))
	{
		return false;
	}

	if (OutSample.bAnyPositiveDomain
		&& !DoesRowMatchAnyName(OutSample.WinningRow, EligibleBiomeRowNames))
	{
		OutSample.bAnyPositiveDomain = false;
		OutSample.WinningRow = FLayoutActiveBiomeRowRef();
		OutSample.WinningDomainValue = 0.0f;
	}
	return true;
}

bool FLayoutActiveBiomeSampler::DoesDomainContribute(
	const float DomainValue,
	const float DomainOver,
	const float BiomePower)
{
	return DomainValue + DomainOver > BiomePower;
}

bool FLayoutActiveBiomeSampler::SampleContributingTerrainAtBlockPositionFromAnyRow(
	const FIntVector BlockWorldPosition,
	const FLayoutNoiseCoordinateSettings& CoordinateSettings,
	const TConstArrayView<FName> EligibleBiomeRowNames,
	FLayoutActiveBiomeSample& OutSample) const
{
	OutSample = FLayoutActiveBiomeSample();
	if (CompiledRows.IsEmpty() || EligibleBiomeRowNames.IsEmpty())
	{
		return false;
	}

	float BiomePower = 0.0f;
	float BestContributionMargin = -FLT_MAX;
	for (const FCompiledRow& CandidateRow : CompiledRows)
	{
		if (!CandidateRow.DomainNode)
		{
			continue;
		}
		const float DomainValue = EvaluateNode(CandidateRow.DomainNode, BlockWorldPosition, CoordinateSettings);
		const float ContributionMargin = DomainValue + CandidateRow.DomainOver - BiomePower;
		if (DoesDomainContribute(DomainValue, CandidateRow.DomainOver, BiomePower)
			&& ContributionMargin > BestContributionMargin
			&& DoesRowMatchAnyName(CandidateRow.RowRef, EligibleBiomeRowNames))
		{
			const float DualSwitchValue = EvaluateNode(CandidateRow.DualSwitchNode, BlockWorldPosition, CoordinateSettings);
			const float GenAWeight = FMath::Max(0.0f, CandidateRow.Overlap + DualSwitchValue);
			const float GenBWeight = FMath::Max(0.0f, CandidateRow.Overlap - DualSwitchValue);
			const float TerrainValue =
				EvaluateNode(CandidateRow.GenANode, BlockWorldPosition, CoordinateSettings) * GenAWeight
				+ EvaluateNode(CandidateRow.GenBNode, BlockWorldPosition, CoordinateSettings) * GenBWeight;
			if ((GenAWeight > 0.0f || GenBWeight > 0.0f) && TerrainValue <= 0.0f)
			{
				BestContributionMargin = ContributionMargin;
				OutSample.bIsValid = true;
				OutSample.bAnyPositiveDomain = DomainValue > 0.0f;
				OutSample.bHasTerrainContribution = true;
				OutSample.WinningRow = CandidateRow.RowRef;
				OutSample.WinningDomainValue = DomainValue;
				OutSample.TerrainContributionMargin = ContributionMargin;
				OutSample.TerrainValue = TerrainValue;
				OutSample.bTerrainSolid = true;
				OutSample.bSelectedGenA = GenAWeight >= GenBWeight;
			}
		}
		BiomePower = FMath::Max(BiomePower, DomainValue);
	}

	if (!OutSample.bIsValid)
	{
		OutSample.bIsValid = true;
	}
	return true;
}

bool FLayoutActiveBiomeSampler::TryGetRowRefForSourceIndex(
	const int32 SourceRowIndex,
	FLayoutActiveBiomeRowRef& OutRowRef) const
{
	for (const FCompiledRow& Row : CompiledRows)
	{
		if (Row.RowRef.SourceRowIndex == SourceRowIndex)
		{
			OutRowRef = Row.RowRef;
			return true;
		}
	}
	OutRowRef = FLayoutActiveBiomeRowRef();
	return false;
}

bool FLayoutActiveBiomeSampler::FindEligibleBiomeSurface(
	const FName EligibleBiomeRowName,
	const FIntPoint BlockXY,
	const int32 SearchStartZBlockWorld,
	const int32 SearchDepthBlocks,
	const FLayoutNoiseCoordinateSettings& CoordinateSettings,
	FLayoutActiveBiomeSurfaceSample& OutSurface) const
{
	return FindTopOwnedSolidSurfaceInColumn(
		*this,
		BlockXY,
		SearchStartZBlockWorld,
		SearchDepthBlocks,
		CoordinateSettings,
		[this, EligibleBiomeRowName](const FLayoutActiveBiomeSample& Sample)
		{
			return Sample.bAnyPositiveDomain
				&& Sample.bTerrainSolid
				&& DoesRowMatchName(Sample.WinningRow, EligibleBiomeRowName);
		},
		OutSurface);
}

bool FLayoutActiveBiomeSampler::FindEligibleBiomeSurfaceFromAnyRow(
	const TConstArrayView<FName> EligibleBiomeRowNames,
	const FIntPoint BlockXY,
	const int32 SearchStartZBlockWorld,
	const int32 SearchDepthBlocks,
	const FLayoutNoiseCoordinateSettings& CoordinateSettings,
	FLayoutActiveBiomeSurfaceSample& OutSurface) const
{
	if (EligibleBiomeRowNames.IsEmpty())
	{
		return false;
	}

	return FindTopOwnedSolidSurfaceInColumn(
		*this,
		BlockXY,
		SearchStartZBlockWorld,
		SearchDepthBlocks,
		CoordinateSettings,
		[this, EligibleBiomeRowNames](const FLayoutActiveBiomeSample& Sample)
		{
			return Sample.bAnyPositiveDomain
				&& Sample.bTerrainSolid
				&& DoesRowMatchAnyName(Sample.WinningRow, EligibleBiomeRowNames);
		},
		OutSurface);
}

bool FLayoutActiveBiomeSampler::FindAnyActiveBiomeSurface(
	const FIntPoint BlockXY,
	const int32 SearchStartZBlockWorld,
	const int32 SearchDepthBlocks,
	const FLayoutNoiseCoordinateSettings& CoordinateSettings,
	FLayoutActiveBiomeSurfaceSample& OutSurface) const
{
	return FindTopOwnedSolidSurfaceInColumn(
		*this,
		BlockXY,
		SearchStartZBlockWorld,
		SearchDepthBlocks,
		CoordinateSettings,
		[](const FLayoutActiveBiomeSample& Sample)
		{
			return Sample.bAnyPositiveDomain && Sample.bTerrainSolid;
		},
		OutSurface);
}

bool FLayoutActiveBiomeSampler::SampleEligibleBiomeSurfaceGrid(
	const FName EligibleBiomeRowName,
	const FIntPoint MinBlockXY,
	const FIntPoint MaxBlockXY,
	const int32 SampleSpacingInBlocks,
	const int32 SearchStartZBlockWorld,
	const int32 SearchDepthBlocks,
	const FLayoutNoiseCoordinateSettings& CoordinateSettings,
	TArray<FLayoutReservationPocketSample>& OutSamples,
	TMap<FIntPoint, int32>* const OutSurfaceZBySampleGrid,
	TMap<FIntPoint, int32>* const OutSurfaceZByBlockXY) const
{
	OutSamples.Reset();
	if (OutSurfaceZBySampleGrid != nullptr)
	{
		OutSurfaceZBySampleGrid->Reset();
	}
	if (OutSurfaceZByBlockXY != nullptr)
	{
		OutSurfaceZByBlockXY->Reset();
	}

	if (CompiledRows.IsEmpty() || EligibleBiomeRowName.IsNone())
	{
		return false;
	}

	const int32 SafeSampleSpacing = FMath::Max(1, SampleSpacingInBlocks);
	int32 GridY = 0;
	for (int32 BlockY = MinBlockXY.Y; BlockY <= MaxBlockXY.Y; BlockY += SafeSampleSpacing, ++GridY)
	{
		int32 GridX = 0;
		for (int32 BlockX = MinBlockXY.X; BlockX <= MaxBlockXY.X; BlockX += SafeSampleSpacing, ++GridX)
		{
			const FIntPoint SampleGridXY(GridX, GridY);
			FLayoutActiveBiomeSurfaceSample Surface;
			const bool bSurfaceSearchCompleted = FindEligibleBiomeSurface(
				EligibleBiomeRowName,
				FIntPoint(BlockX, BlockY),
				SearchStartZBlockWorld,
				SearchDepthBlocks,
				CoordinateSettings,
				Surface);
			if (!bSurfaceSearchCompleted)
			{
				return false;
			}

			FLayoutReservationPocketSample& PocketSample = OutSamples.AddDefaulted_GetRef();
			PocketSample.SampleGridXY = SampleGridXY;
			PocketSample.BlockXY = FIntPoint(BlockX, BlockY);
			PocketSample.NoiseValue = Surface.bIsValid ? Surface.BiomeSample.WinningDomainValue : -1.0f;
			PocketSample.bHasSurfaceZ = Surface.bIsValid;
			PocketSample.SurfaceZBlockWorld = Surface.bIsValid ? Surface.SurfaceBlockWorldPos.Z : 0;
			if (Surface.bIsValid && OutSurfaceZBySampleGrid != nullptr)
			{
				OutSurfaceZBySampleGrid->Add(SampleGridXY, Surface.SurfaceBlockWorldPos.Z);
			}
			if (Surface.bIsValid && OutSurfaceZByBlockXY != nullptr)
			{
				OutSurfaceZByBlockXY->Add(PocketSample.BlockXY, Surface.SurfaceBlockWorldPos.Z);
			}
		}
	}

	return true;
}

bool FLayoutActiveBiomeSampler::DoesRowMatchName(
	const FLayoutActiveBiomeRowRef& RowRef,
	const FName RequestedName)
{
	return !RequestedName.IsNone()
		&& (RowRef.RowName == RequestedName || FName(*RowRef.BiomeName) == RequestedName);
}

bool FLayoutActiveBiomeSampler::DoesRowMatchAnyName(
	const FLayoutActiveBiomeRowRef& RowRef,
	const TConstArrayView<FName> RequestedNames)
{
	for (const FName RequestedName : RequestedNames)
	{
		if (DoesRowMatchName(RowRef, RequestedName))
		{
			return true;
		}
	}

	return false;
}

float FLayoutActiveBiomeSampler::EvaluateNode(
	const FastNoise::SmartNode<>& Node,
	const FIntVector BlockWorldPosition,
	const FLayoutNoiseCoordinateSettings& CoordinateSettings) const
{
	if (!Node)
	{
		return 1.0f;
	}

	const FVector NoiseCoordinate = ULayoutNoiseCoordinateLibrary::BlockWorldPositionToNoiseCoordinate(
		BlockWorldPosition,
		CoordinateSettings);
	return EvaluateNodeAtNoiseCoordinate(Node, NoiseCoordinate);
}

float FLayoutActiveBiomeSampler::EvaluateNodeAtNoiseCoordinate(
	const FastNoise::SmartNode<>& Node,
	const FVector& NoiseCoordinate) const
{
	if (!Node)
	{
		return 1.0f;
	}

	return Node->GenSingle3D(
		static_cast<float>(NoiseCoordinate.X),
		static_cast<float>(NoiseCoordinate.Y),
		static_cast<float>(NoiseCoordinate.Z),
		Seed);
}
