// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Async/LayoutChildSolveHandoff.h"
#include "Layout/Contracts/LayoutContractPipeline.h"

#include "Misc/Crc.h"

namespace
{
	void AppendGameplayTags(const FGameplayTagContainer& Tags, FString& InOutKey)
	{
		TArray<FGameplayTag> TagArray;
		Tags.GetGameplayTagArray(TagArray);
		TagArray.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
		{
			return Left.ToString() < Right.ToString();
		});
		for (const FGameplayTag& Tag : TagArray)
		{
			InOutKey += Tag.ToString();
			InOutKey += TEXT("|");
		}
	}

	void AppendIntVectorArray(const TArray<FIntVector>& Cells, FString& InOutKey)
	{
		for (const FIntVector& Cell : Cells)
		{
			InOutKey += Cell.ToString();
			InOutKey += TEXT("|");
		}
	}

	void AppendNameArray(const TArray<FLayoutId>& Names, FString& InOutKey)
	{
		TArray<FLayoutId> SortedNames = Names;
		SortedNames.Sort([](const FLayoutId Left, const FLayoutId Right)
		{
			return Left.LexicalLess(Right);
		});
		for (const FLayoutId Name : SortedNames)
		{
			InOutKey += Name.ToString();
			InOutKey += TEXT("|");
		}
	}

	void AppendChildStageMapping(const FLayoutChildStageMappingResult& Mapping, FString& InOutKey)
	{
		InOutKey += LexToString(static_cast<int32>(Mapping.StageClass));
		InOutKey += TEXT("@");
		InOutKey += Mapping.MappingId.ToString();
		InOutKey += TEXT("@");
		// Preserve identities without offers; changed deck occupancy invalidates reuse.
		if (Mapping.ChildLocalPlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
			{ return Cell.bIsTopBridgeOffer; }))
		{
			InOutKey += FLayoutContractPipeline::BuildStageMapId(Mapping.ChildLocalPlannedCells).ToString();
			InOutKey += TEXT("@");
		}
		InOutKey += Mapping.ParentRegionCellOffset.ToString();
		InOutKey += TEXT("@");
		for (const FLayoutChildStageMappedCell& Cell : Mapping.Cells)
		{
			if (Cell.bGeneratedByChildTopology) InOutKey += TEXT("Generated@");
			InOutKey += Cell.SourceChildCell.ToString();
			InOutKey += TEXT(">");
			InOutKey += Cell.MappedChildCell.ToString();
			InOutKey += TEXT(">");
			InOutKey += Cell.ParentCell.ToString();
			InOutKey += TEXT(":");
			InOutKey += LexToString(Cell.ModuleLevelIndex);
			InOutKey += TEXT(":");
			InOutKey += LexToString(Cell.TerrainStageIndex);
			InOutKey += TEXT("|");
		}
		for (const FLayoutFrozenTerrainStageCellRecord& Stage : Mapping.ChildLocalStageMap)
		{
			InOutKey += Stage.FootprintCellXY.ToString();
			InOutKey += TEXT(":");
			InOutKey += LexToString(Stage.TerrainStageIndex);
			InOutKey += TEXT(":");
			InOutKey += LexToString(Stage.VerticalShiftBlocks);
			InOutKey += TEXT(":");
			InOutKey += LexToString(Stage.ResolvedStageBaseBlockWorldZ);
			InOutKey += TEXT("|");
		}
		for (const FLayoutVerticalAccessHostGroup& Group : Mapping.ChildLocalVerticalAccessHostGroups)
		{
			InOutKey += Group.GroupId.ToString();
			InOutKey += TEXT("@");
			InOutKey += Group.DeckCell.ToString();
			InOutKey += TEXT("@");
			// Count alternatives change the child input contract even at identical host coordinates.
			InOutKey += Group.bAllowOmission ? TEXT("Range@") : TEXT("Required@");
			InOutKey += Group.bPreferOmission ? TEXT("Omit@") : TEXT("Place@");
			if (Group.bIsSupplemental) InOutKey += TEXT("Supplemental@");
			for (const FLayoutVerticalAccessHostOption& Option : Group.Options)
			{
				InOutKey += Option.LowerCell.ToString();
				InOutKey += TEXT(">");
				InOutKey += Option.UpperCell.ToString();
				// Equal roots can bind different landing faces and normal module domains.
				InOutKey += FString::Printf(TEXT("@%d:%s:%d:%s:%d:%s:%u:%u|"),
					Option.bHasExactCandidateWitness, *Option.LowerModuleSnapshotId.ToString(), Option.LowerYawRotationSteps,
					*Option.UpperModuleSnapshotId.ToString(), Option.UpperYawRotationSteps,
					*Option.UpperCandidateLocalCell.ToString(), static_cast<uint32>(Option.LowerTraversalPortFaceMask),
					static_cast<uint32>(Option.UpperTraversalPortFaceMask));
				for (const auto& Cell : Option.OccupiedCells) InOutKey += TEXT("occupied:") + Cell.ToString() + TEXT("|");
				for (const auto& Cell : Option.RequiredFilledSupportCells) InOutKey += TEXT("filled:") + Cell.ToString() + TEXT("|");
				for (const auto& Cell : Option.RequiredEmptyClearanceCells) InOutKey += TEXT("empty:") + Cell.ToString() + TEXT("|");
				for (const auto& Support : Option.FilledSupportCandidates)
				{
					InOutKey += FString::Printf(TEXT("support:%s:%s:%s:%d|"), *Support.Cell.ToString(),
						*Support.RootCell.ToString(), *Support.ModuleSnapshotId.ToString(), Support.YawRotationSteps);
				}
			}
		}
		AppendNameArray(Mapping.CrossedFrontierIds, InOutKey);
	}

	void AppendRequirementToAssertionMappings(
		const TArray<FLayoutCertifiedChildRequirementToAssertionMapping>& Mappings,
		FString& InOutKey)
	{
		TArray<FLayoutCertifiedChildRequirementToAssertionMapping> SortedMappings = Mappings;
		SortedMappings.Sort([](
			const FLayoutCertifiedChildRequirementToAssertionMapping& Left,
			const FLayoutCertifiedChildRequirementToAssertionMapping& Right)
		{
			const int32 RequirementCompare = Left.RequirementId.ToString().Compare(Right.RequirementId.ToString());
			return RequirementCompare == 0
				? Left.AssertionId.ToString() < Right.AssertionId.ToString()
				: RequirementCompare < 0;
		});
		for (const FLayoutCertifiedChildRequirementToAssertionMapping& Mapping : SortedMappings)
		{
			InOutKey += Mapping.RequirementId.ToString();
			InOutKey += TEXT("=>");
			InOutKey += Mapping.AssertionId.ToString();
			InOutKey += TEXT("|");
		}
	}

	void AppendTraversalAnchors(const TArray<FLayoutCommittedTraversalAnchor>& Anchors, FString& InOutKey)
	{
		for (const FLayoutCommittedTraversalAnchor& Anchor : Anchors)
		{
			InOutKey += Anchor.Cell.ToString();
			InOutKey += TEXT("@");
			InOutKey += Anchor.TraversalChannel.ToString();
			InOutKey += TEXT("|");
		}
	}

	void AppendEndpointAnchors(const TArray<FLayoutCommittedEndpointAnchor>& Anchors, FString& InOutKey)
	{
		for (const FLayoutCommittedEndpointAnchor& Anchor : Anchors)
		{
			InOutKey += Anchor.CommitmentId.ToString();
			InOutKey += TEXT("@");
			InOutKey += Anchor.LocalCell.ToString();
			InOutKey += TEXT("@");
			InOutKey += LexToString(static_cast<int32>(Anchor.FaceDirection));
			InOutKey += TEXT("@");
			InOutKey += LexToString(Anchor.RequiredWorldCenterBlockZ);
			InOutKey += TEXT("@");
			InOutKey += Anchor.ConnectionTag.ToString();
			InOutKey += TEXT("@");
			AppendGameplayTags(Anchor.AllowedConnectionTags, InOutKey);
			AppendGameplayTags(Anchor.TraversalChannels, InOutKey);
			InOutKey += Anchor.bRequireMatchingYawWithFilledNeighbor ? TEXT("1") : TEXT("0");
			InOutKey += TEXT("|");
		}
	}

	void AppendPartitionSeams(const TArray<FLayoutPartitionSeamRecord>& Seams, FString& InOutKey)
	{
		for (const FLayoutPartitionSeamRecord& Seam : Seams)
		{
			InOutKey += Seam.SeamId.ToString();
			InOutKey += TEXT("@");
			InOutKey += Seam.OwnerRegionDebugPath;
			InOutKey += TEXT("@");
			InOutKey += Seam.PassiveRegionDebugPath;
			InOutKey += TEXT("@");
			InOutKey += Seam.InterfaceFamily.ToString();
			InOutKey += TEXT("@");
			InOutKey += LexToString(static_cast<int32>(Seam.OwnerFaceDirection));
			InOutKey += TEXT("@");
			InOutKey += LexToString(static_cast<int32>(Seam.PassiveFaceDirection));
			InOutKey += TEXT("@");
			InOutKey += Seam.OwnerStartCell.ToString();
			InOutKey += TEXT("@");
			InOutKey += Seam.OwnerEndCell.ToString();
			InOutKey += TEXT("@");
			InOutKey += Seam.PassiveStartCell.ToString();
			InOutKey += TEXT("@");
			InOutKey += Seam.PassiveEndCell.ToString();
			InOutKey += TEXT("@");
			InOutKey += LexToString(Seam.SegmentCount);
			InOutKey += TEXT("@");
			InOutKey += Seam.bCountsTowardClosure ? TEXT("1") : TEXT("0");
			InOutKey += TEXT("@");
			InOutKey += Seam.ProviderId.ToString();
			InOutKey += TEXT("|");
		}
	}

	void AppendLevelCellSets(const TArray<FLayoutNegotiatedLevelCellSet>& LevelSets, FString& InOutKey)
	{
		for (const FLayoutNegotiatedLevelCellSet& LevelSet : LevelSets)
		{
			InOutKey += LexToString(LevelSet.Level);
			InOutKey += TEXT(":");
			AppendIntVectorArray(LevelSet.Cells, InOutKey);
			InOutKey += TEXT("|");
		}
	}

	void AppendNegotiatedInterfaces(const TArray<FLayoutNegotiatedLevelInterfaceContract>& Interfaces, FString& InOutKey)
	{
		for (const FLayoutNegotiatedLevelInterfaceContract& InterfaceContract : Interfaces)
		{
			InOutKey += LexToString(InterfaceContract.Level);
			InOutKey += TEXT(":");
			AppendEndpointAnchors(InterfaceContract.EndpointAnchors, InOutKey);
			AppendTraversalAnchors(InterfaceContract.TraversalAnchors, InOutKey);
			InOutKey += TEXT("|");
		}
	}

	void AppendNegotiatedSeamSets(const TArray<FLayoutNegotiatedLevelSeamSet>& SeamSets, FString& InOutKey)
	{
		for (const FLayoutNegotiatedLevelSeamSet& SeamSet : SeamSets)
		{
			InOutKey += LexToString(SeamSet.Level);
			InOutKey += TEXT(":");
			AppendPartitionSeams(SeamSet.Seams, InOutKey);
			InOutKey += TEXT("|");
		}
	}

	void AppendFrontierResponsibilities(
		const TArray<FLayoutNegotiatedHostVerticalAccessFrontierResponsibility>& Responsibilities,
		FString& InOutKey)
	{
		for (const FLayoutNegotiatedHostVerticalAccessFrontierResponsibility& Responsibility : Responsibilities)
		{
			InOutKey += LexToString(Responsibility.TerrainAscentFrontierId);
			InOutKey += TEXT("@");
			InOutKey += LexToString(static_cast<int32>(Responsibility.HostVerticalAccessResponsibility));
			InOutKey += TEXT("@");
			InOutKey += LexToString(Responsibility.CountedParentProviderCount);
			InOutKey += TEXT("@");
			AppendIntVectorArray(Responsibility.CountedParentVerticalAccessCells, InOutKey);
			AppendIntVectorArray(Responsibility.RetainedParentRouteSupportVerticalAccessCells, InOutKey);
			for (const FString& ChildPath : Responsibility.CountedChildProviderRegionDebugPaths)
			{
				InOutKey += ChildPath;
				InOutKey += TEXT("|");
			}
			InOutKey += TEXT("#");
		}
	}

	void AppendNegotiatedResponsibilityContract(
		const FLayoutNegotiatedChildResponsibilityContract& Contract,
		FString& InOutKey)
	{
		InOutKey += Contract.ParentRegionDebugPath;
		InOutKey += TEXT("@");
		InOutKey += Contract.ChildRegionDebugPath;
		InOutKey += TEXT("@");
		AppendLevelCellSets(Contract.ReplacementVolumeByLevel, InOutKey);
		AppendLevelCellSets(Contract.RetainedParentShellCellsByLevel, InOutKey);
		AppendLevelCellSets(Contract.ProofOnlyHostAscentParentShellCellsByLevel, InOutKey);
		AppendNegotiatedInterfaces(Contract.CommittedParentChildInterfacesByLevel, InOutKey);
		AppendNegotiatedSeamSets(Contract.CommittedSiblingInterfacesByLevel, InOutKey);
		InOutKey += LexToString(static_cast<int32>(Contract.HostVerticalAccessResponsibility));
		InOutKey += TEXT("@");
		InOutKey += LexToString(Contract.RequiredHostProviderCount);
		InOutKey += TEXT("@");
		InOutKey += Contract.bRequiresExactHostProviderCount ? TEXT("1") : TEXT("0");
		InOutKey += TEXT("@");
		InOutKey += LexToString(Contract.CountedParentProviderCount);
		InOutKey += TEXT("@");
		AppendIntVectorArray(Contract.CountedParentVerticalAccessCells, InOutKey);
		AppendIntVectorArray(Contract.RetainedParentRouteSupportVerticalAccessCells, InOutKey);
		for (const FString& ChildPath : Contract.CountedChildProviderRegionDebugPaths)
		{
			InOutKey += ChildPath;
			InOutKey += TEXT("|");
		}
		AppendFrontierResponsibilities(Contract.HostVerticalAccessFrontierResponsibilities, InOutKey);
		InOutKey += Contract.bHasRequiredHostIngressAnchor ? TEXT("1") : TEXT("0");
		InOutKey += TEXT("@");
		if (Contract.bHasRequiredHostIngressAnchor)
		{
			TArray<FLayoutCommittedEndpointAnchor> IngressAnchors;
			IngressAnchors.Add(Contract.RequiredHostIngressAnchor);
			AppendEndpointAnchors(IngressAnchors, InOutKey);
		}
		InOutKey += Contract.bHasRequiredHostEgressAnchor ? TEXT("1") : TEXT("0");
		InOutKey += TEXT("@");
		if (Contract.bHasRequiredHostEgressAnchor)
		{
			TArray<FLayoutCommittedEndpointAnchor> EgressAnchors;
			EgressAnchors.Add(Contract.RequiredHostEgressAnchor);
			AppendEndpointAnchors(EgressAnchors, InOutKey);
		}
		InOutKey += Contract.RequiredChildGenerallyConnectableAnchorPairId.ToString();
		InOutKey += TEXT("@");
		for (const int32 Level : Contract.RequiredChildInternalVerticalSpanLevels)
		{
			InOutKey += LexToString(Level);
			InOutKey += TEXT("|");
		}
		AppendIntVectorArray(Contract.RequiredChildInternalVerticalRouteCells, InOutKey);
	}

	void AppendRouteConstraints(const TArray<FLayoutRouteConstraintRecord>& Constraints, FString& InOutKey)
	{
		for (const FLayoutRouteConstraintRecord& Constraint : Constraints)
		{
			InOutKey += Constraint.ConstraintId.ToString();
			InOutKey += TEXT("@");
			InOutKey += Constraint.Cell.ToString();
			InOutKey += TEXT("@");
			InOutKey += LexToString(static_cast<int32>(Constraint.Intent));
			InOutKey += TEXT("@");
			InOutKey += Constraint.bScoreAsMainRoute ? TEXT("1") : TEXT("0");
			InOutKey += TEXT("@");
			for (const FLayoutRouteFaceRequirement& FaceRequirement : Constraint.FaceRequirements)
			{
				InOutKey += LexToString(static_cast<int32>(FaceRequirement.FaceDirection));
				InOutKey += TEXT(":");
				InOutKey += FaceRequirement.TraversalChannel.ToString();
				InOutKey += TEXT("|");
			}
			InOutKey += TEXT("#");
		}
	}

	void AppendCertifiedWitnessBundle(const FLayoutCertifiedChildWitnessBundle& Bundle, FString& InOutKey)
	{
		InOutKey += Bundle.SourceContentEntryId.ToString();
		InOutKey += TEXT("@");
		InOutKey += Bundle.StableChildId.ToString();
		InOutKey += TEXT("@");
		InOutKey += Bundle.BranchId.ToString();
		InOutKey += TEXT("@");
		InOutKey += Bundle.SelectedClosureSpanCapabilityId.ToString();
		InOutKey += TEXT("@");
		InOutKey += Bundle.SelectedSeamCapabilityId.ToString();
		InOutKey += TEXT("@");
		InOutKey += Bundle.SelectedSeamWitnessId.ToString();
		InOutKey += TEXT("@");
		AppendNameArray(Bundle.SelectedEndpointCapabilityIds, InOutKey);
		AppendNameArray(Bundle.SelectedTraversalCapabilityIds, InOutKey);
		AppendNameArray(Bundle.SelectedVerticalCapabilityIds, InOutKey);
		AppendNameArray(Bundle.DelegatedFeatureRequirementIds, InOutKey);
		AppendNameArray(Bundle.DelegatedClosureRequirementIds, InOutKey);
		AppendNameArray(Bundle.DelegatedSeamWitnessIds, InOutKey);
		AppendNameArray(Bundle.DelegatedJunctionWitnessIds, InOutKey);
		AppendNameArray(Bundle.DelegatedHostVerticalAccessWitnessIds, InOutKey);
		AppendNameArray(Bundle.AssertionIds, InOutKey);
		AppendNameArray(Bundle.ParentAssertionIds, InOutKey);
	}

	void AppendChildCapabilityEnvelope(const FLayoutChildCapabilityEnvelope& Envelope, FString& InOutKey)
	{
		InOutKey += Envelope.RegionDebugPath;
		InOutKey += TEXT("@");
		InOutKey += Envelope.SnapshotId.ToString();
		InOutKey += TEXT("@");
		for (const FLayoutChildCapabilityEndpoint& Capability : Envelope.EndpointCapabilities)
		{
			InOutKey += Capability.CapabilityId.ToString();
			InOutKey += TEXT("|");
		}
		for (const FLayoutChildCapabilitySpan& Capability : Envelope.SpanCapabilities)
		{
			InOutKey += Capability.CapabilityId.ToString();
			InOutKey += TEXT("|");
		}
		for (const FLayoutChildCapabilitySeam& Capability : Envelope.SeamCapabilities)
		{
			InOutKey += Capability.CapabilityId.ToString();
			InOutKey += TEXT("|");
		}
	}
}

bool FLayoutCertifiedChildWitnessBundle::IsEmpty() const
{
	return SourceContentEntryId.IsNone()
		&& StableChildId.IsNone()
		&& BranchId.IsNone()
		&& SelectedClosureSpanCapabilityId.IsNone()
		&& SelectedSeamCapabilityId.IsNone()
		&& SelectedSeamWitnessId.IsNone()
		&& SelectedEndpointCapabilityIds.IsEmpty()
		&& SelectedTraversalCapabilityIds.IsEmpty()
		&& SelectedVerticalCapabilityIds.IsEmpty()
		&& DelegatedFeatureRequirementIds.IsEmpty()
		&& DelegatedClosureRequirementIds.IsEmpty()
		&& DelegatedSeamWitnessIds.IsEmpty()
		&& DelegatedJunctionWitnessIds.IsEmpty()
		&& DelegatedHostVerticalAccessWitnessIds.IsEmpty()
		&& AssertionIds.IsEmpty()
		&& ParentAssertionIds.IsEmpty();
}

uint64 FLayoutChildSolveHandoff::ComputeDeterministicInputHash() const
{
	FString MaterialKey;
	MaterialKey.Reserve(4096);
	MaterialKey += ParentRegionDebugPath;
	MaterialKey += TEXT("@");
	MaterialKey += ChildRegionDebugPath;
	MaterialKey += TEXT("@");
	MaterialKey += ParentArtifactOrResumeId.ToString();
	MaterialKey += TEXT("@");
	MaterialKey += StableChildKey;
	MaterialKey += TEXT("@");
	MaterialKey += LexToString(AttemptIndex);
	MaterialKey += TEXT("@");
	MaterialKey += ChildRegionCellOffset.ToString();
	MaterialKey += TEXT("@");
	MaterialKey += bHasChildBlockWorldAnchor ? TEXT("1") : TEXT("0");
	MaterialKey += TEXT("@");
	MaterialKey += ChildBlockWorldAnchor.ToString();
	MaterialKey += TEXT("@");
	MaterialKey += ContentMetadata.SourceContentEntryId.ToString();
	MaterialKey += TEXT("@");
	MaterialKey += ContentMetadata.ChildProfilePath.ToString();
	MaterialKey += TEXT("@");
	MaterialKey += LexToString(static_cast<int32>(ContentMetadata.PlacementZone));
	MaterialKey += TEXT("@");
	MaterialKey += LexToString(static_cast<int32>(ContentMetadata.LevelPlacementPolicy));
	MaterialKey += TEXT("@");
	MaterialKey += LexToString(ContentMetadata.SpecificLevel);
	MaterialKey += TEXT("@");
	MaterialKey += ContentMetadata.bOptional ? TEXT("1") : TEXT("0");
	MaterialKey += TEXT("@");
	MaterialKey += ContentMetadata.bContributesHostVerticalAccess ? TEXT("1") : TEXT("0");
	MaterialKey += TEXT("@");
	AppendGameplayTags(ContentMetadata.ProvidedZoneFeatures, MaterialKey);
	AppendChildCapabilityEnvelope(ChildCapabilityEnvelope, MaterialKey);
	MaterialKey += DirectCommitment.ParentRegionDebugPath;
	MaterialKey += TEXT("@");
	MaterialKey += DirectCommitment.ChildRegionDebugPath;
	MaterialKey += TEXT("@");
	AppendChildStageMapping(DirectCommitment.StageMapping, MaterialKey);
	AppendEndpointAnchors(DirectCommitment.EndpointCommitments, MaterialKey);
	AppendTraversalAnchors(DirectCommitment.ParentTraversalIngressCommitments, MaterialKey);
	MaterialKey += DirectCommitment.bAllowsChildTraversalBridgeForCommittedContacts ? TEXT("1") : TEXT("0");
	MaterialKey += TEXT("@");
	AppendNegotiatedResponsibilityContract(DirectCommitment.NegotiatedResponsibilityContract, MaterialKey);
	AppendIntVectorArray(ProtectedStructuralCells, MaterialKey);
	AppendRouteConstraints(RequiredRouteConstraints, MaterialKey);
	for (const FLayoutId& RequirementId : DelegatedZoneFeatureRequirementIds)
	{
		MaterialKey += RequirementId.ToString();
		MaterialKey += TEXT("|");
	}
	for (const FLayoutId& RequirementId : DelegatedClosureRequirementIds)
	{
		MaterialKey += RequirementId.ToString();
		MaterialKey += TEXT("|");
	}
	MaterialKey += bHasCertifiedSelectedTraversalCapabilityArtifact ? TEXT("TraversalArtifact") : TEXT("NoTraversalArtifact");
	MaterialKey += TEXT("@");
	AppendNameArray(CertifiedSelectedTraversalCapabilityIds, MaterialKey);
	MaterialKey += bHasCertifiedParentAssertionSubsetArtifact ? TEXT("AssertionArtifact") : TEXT("NoAssertionArtifact");
	MaterialKey += TEXT("@");
	AppendNameArray(CertifiedParentAssertionSubsetIds, MaterialKey);
	MaterialKey += bHasCertifiedRequirementToAssertionArtifact ? TEXT("RequirementAssertionArtifact") : TEXT("NoRequirementAssertionArtifact");
	MaterialKey += TEXT("@");
	AppendRequirementToAssertionMappings(CertifiedRequirementToAssertionMappings, MaterialKey);
	MaterialKey += bRequiresCertifiedWitnessBundle ? TEXT("WitnessRequired") : TEXT("WitnessOptional");
	MaterialKey += TEXT("@");
	AppendCertifiedWitnessBundle(CertifiedWitnessBundle, MaterialKey);
	return static_cast<uint64>(FCrc::StrCrc32(*MaterialKey));
}

void FLayoutChildSolveHandoff::RefreshProofCertificate(const FLayoutId InCertificateId)
{
	ProofCertificate.CertificateId = InCertificateId.IsNone()
		? FLayoutId(*FString::Printf(TEXT("ChildCert.%s"), StableChildKey.IsEmpty() ? TEXT("Unnamed") : *StableChildKey))
		: InCertificateId;
	ProofCertificate.InputHash = ComputeDeterministicInputHash();
}

bool FLayoutChildSolveHandoff::ValidateNoLiveObjectCarriers(FString& OutFailureReason) const
{
	OutFailureReason.Reset();

	if (ParentRegionDebugPath.IsEmpty())
	{
		OutFailureReason = TEXT("Child solve handoff is missing ParentRegionDebugPath.");
		return false;
	}
	if (!DirectCommitment.ChildRegionDebugPath.IsEmpty()
		&& !DirectCommitment.StageMapping.IsValid())
	{
		OutFailureReason = TEXT("Certified child solve handoff is missing complete pointer-free stage-mapping authority.");
		return false;
	}

	if (ChildRegionDebugPath.IsEmpty())
	{
		OutFailureReason = TEXT("Child solve handoff is missing ChildRegionDebugPath.");
		return false;
	}

	if (StableChildKey.IsEmpty())
	{
		OutFailureReason = TEXT("Child solve handoff is missing StableChildKey.");
		return false;
	}

	if (!ContentMetadata.ChildProfilePath.IsValid())
	{
		OutFailureReason = TEXT("Child solve handoff is missing ChildProfilePath.");
		return false;
	}

	if (ProofCertificate.CertificateId.IsNone())
	{
		OutFailureReason = TEXT("Child solve handoff is missing proof certificate id.");
		return false;
	}

	if (bHasCertifiedRequirementToAssertionArtifact && CertifiedRequirementToAssertionMappings.IsEmpty())
	{
		OutFailureReason = TEXT("Child solve handoff has an authoritative requirement-to-assertion flag without explicit mappings.");
		return false;
	}
	if (bRequiresCertifiedWitnessBundle && CertifiedWitnessBundle.IsEmpty())
	{
		OutFailureReason = TEXT("Child solve handoff requires an exact certified witness bundle before dispatch.");
		return false;
	}
	if (!CertifiedWitnessBundle.SourceContentEntryId.IsNone()
		&& !ContentMetadata.SourceContentEntryId.IsNone()
		&& CertifiedWitnessBundle.SourceContentEntryId != ContentMetadata.SourceContentEntryId)
	{
		OutFailureReason = TEXT("Child solve handoff witness source content entry does not match child content metadata.");
		return false;
	}
	if (bRequiresCertifiedWitnessBundle)
	{
		if (!DirectCommitment.ParentTraversalIngressCommitments.IsEmpty()
			&& CertifiedWitnessBundle.SelectedTraversalCapabilityIds.IsEmpty())
		{
			OutFailureReason = TEXT("Child solve handoff requires selected traversal witness ids before dispatch.");
			return false;
		}

		const bool bRequiresVerticalWitness =
			!DirectCommitment.NegotiatedResponsibilityContract.RequiredChildGenerallyConnectableAnchorPairId.IsNone()
			|| !DirectCommitment.NegotiatedResponsibilityContract.RequiredChildInternalVerticalSpanLevels.IsEmpty()
			|| !DirectCommitment.NegotiatedResponsibilityContract.RequiredChildInternalVerticalRouteCells.IsEmpty();
		if (bRequiresVerticalWitness && CertifiedWitnessBundle.SelectedVerticalCapabilityIds.IsEmpty())
		{
			OutFailureReason = TEXT("Child solve handoff requires a selected vertical witness id before dispatch.");
			return false;
		}

		bool bRequiresChildSeamWitness = false;
		for (const FLayoutNegotiatedLevelSeamSet& SeamSet : DirectCommitment.NegotiatedResponsibilityContract.CommittedSiblingInterfacesByLevel)
		{
			for (const FLayoutPartitionSeamRecord& Seam : SeamSet.Seams)
			{
				if (Seam.OwnerRegionDebugPath == ChildRegionDebugPath || Seam.PassiveRegionDebugPath == ChildRegionDebugPath)
				{
					bRequiresChildSeamWitness = true;
					break;
				}
			}
			if (bRequiresChildSeamWitness)
			{
				break;
			}
		}
		if (bRequiresChildSeamWitness
			&& (CertifiedWitnessBundle.SelectedSeamCapabilityId.IsNone() || CertifiedWitnessBundle.SelectedSeamWitnessId.IsNone()))
		{
			OutFailureReason = TEXT("Child solve handoff requires selected seam witness ids before dispatch.");
			return false;
		}
	}

	const uint64 ExpectedInputHash = ComputeDeterministicInputHash();
	if (ProofCertificate.InputHash != ExpectedInputHash)
	{
		OutFailureReason = TEXT("Child solve handoff proof certificate hash does not match frozen inputs.");
		return false;
	}

	if (!DirectCommitment.ParentRegionDebugPath.IsEmpty()
		&& DirectCommitment.ParentRegionDebugPath != ParentRegionDebugPath)
	{
		OutFailureReason = TEXT("Child solve handoff parent path does not match direct commitment parent path.");
		return false;
	}

	if (!DirectCommitment.ChildRegionDebugPath.IsEmpty()
		&& DirectCommitment.ChildRegionDebugPath != ChildRegionDebugPath)
	{
		OutFailureReason = TEXT("Child solve handoff child path does not match direct commitment child path.");
		return false;
	}

	if (!DirectCommitment.NegotiatedResponsibilityContract.ParentRegionDebugPath.IsEmpty()
		&& DirectCommitment.NegotiatedResponsibilityContract.ParentRegionDebugPath != ParentRegionDebugPath)
	{
		OutFailureReason = TEXT("Child solve handoff parent path does not match negotiated responsibility parent path.");
		return false;
	}

	if (!DirectCommitment.NegotiatedResponsibilityContract.ChildRegionDebugPath.IsEmpty()
		&& DirectCommitment.NegotiatedResponsibilityContract.ChildRegionDebugPath != ChildRegionDebugPath)
	{
		OutFailureReason = TEXT("Child solve handoff child path does not match negotiated responsibility child path.");
		return false;
	}

	if (!ChildCapabilityEnvelope.RegionDebugPath.IsEmpty()
		&& ChildCapabilityEnvelope.RegionDebugPath != ChildRegionDebugPath)
	{
		OutFailureReason = TEXT("Child solve handoff child path does not match child capability envelope path.");
		return false;
	}

	return true;
}
