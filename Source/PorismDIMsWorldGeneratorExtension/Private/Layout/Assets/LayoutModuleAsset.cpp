// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutModuleAsset.h"

#include "ChunkWorldStructs/ChunkStructureTemplate.h"
#include "Layout/Diagnostics/LayoutEditorMessageLog.h"
#include "Layout/Types/LayoutEntryRootUtilities.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/DataValidation.h"

namespace
{
	TArray<FIntVector> BuildOccupiedLocalCells(const ULayoutModuleAsset* Module);
	FIntVector BuildOccupiedBoundsCells(const TArray<FIntVector>& OccupiedCells);

	const TCHAR* ToModuleAssetFaceDirectionText(const ELayoutFaceDirection Direction)
	{
		switch (Direction)
		{
		case ELayoutFaceDirection::PosX:
			return TEXT("PosX");
		case ELayoutFaceDirection::NegX:
			return TEXT("NegX");
		case ELayoutFaceDirection::PosY:
			return TEXT("PosY");
		case ELayoutFaceDirection::NegY:
			return TEXT("NegY");
		case ELayoutFaceDirection::PosZ:
			return TEXT("PosZ");
		case ELayoutFaceDirection::NegZ:
			return TEXT("NegZ");
		default:
			return TEXT("Unknown");
		}
	}

	void AppendModuleValidationResult(
		FDataValidationContext& Context,
		const FLayoutValidationResult& ValidationResult)
	{
		for (const FLayoutValidationMessage& Message : ValidationResult.Messages)
		{
			if (Message.Severity == ELayoutValidationSeverity::Error)
			{
				Context.AddError(FText::FromString(Message.Message));
			}
			else
			{
				Context.AddWarning(FText::FromString(Message.Message));
			}
		}
	}

	void CopyFaceRuleWithDirection(FLayoutModuleFaceRules& FaceRules, const ELayoutFaceDirection SourceDirection, const ELayoutFaceDirection TargetDirection)
	{
		const FLayoutFaceRule* SourceRule = FaceRules.FindRule(SourceDirection);
		FLayoutFaceRule* TargetRule = FaceRules.FindRule(TargetDirection);
		if (SourceRule == nullptr || TargetRule == nullptr)
		{
			return;
		}

		*TargetRule = *SourceRule;
		TargetRule->Direction = TargetDirection;
	}

	bool CanFaceConnectToFilledWalkableNeighbor(const FLayoutFaceRule& FaceRule)
	{
		if (FaceRule.ConnectedTraversalChannels.IsEmpty())
		{
			return false;
		}

		return FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor;
	}

	bool IsFaceTraversable(const FLayoutFaceRule* FaceRule)
	{
		return FaceRule != nullptr && FaceRule->IsTraversable();
	}

	bool UsesSingleCellOccupiedContract(const ULayoutModuleAsset* Module)
	{
		return Module != nullptr;
	}

	FIntVector ComputeDerivedTemplateDimensionsBlocks(const ULayoutModuleAsset* Module)
	{
		if (Module == nullptr)
		{
			return FIntVector::ZeroValue;
		}

		const UChunkStructureTemplate* LoadedTemplate = Module->Template.Get();
		if (LoadedTemplate == nullptr)
		{
			LoadedTemplate = Module->Template.LoadSynchronous();
		}

		return LoadedTemplate != nullptr ? LoadedTemplate->SizeInBlocks : FIntVector::ZeroValue;
	}

	FIntVector ComputeEffectiveTemplateDimensionsBlocks(const ULayoutModuleAsset* Module)
	{
		if (Module == nullptr)
		{
			return FIntVector::ZeroValue;
		}

		return ComputeDerivedTemplateDimensionsBlocks(Module);
	}

	TArray<FIntVector> BuildOccupiedLocalCells(const ULayoutModuleAsset* Module)
	{
		TArray<FIntVector> OccupiedCells;
		if (Module == nullptr)
		{
			return OccupiedCells;
		}

		OccupiedCells.Add(FIntVector::ZeroValue);
		return OccupiedCells;
	}

	FIntVector BuildOccupiedBoundsCells(const TArray<FIntVector>& OccupiedCells)
	{
		if (OccupiedCells.IsEmpty())
		{
			return FIntVector::ZeroValue;
		}

		FIntVector MaxCell = OccupiedCells[0];
		for (const FIntVector& OccupiedCell : OccupiedCells)
		{
			MaxCell.X = FMath::Max(MaxCell.X, OccupiedCell.X);
			MaxCell.Y = FMath::Max(MaxCell.Y, OccupiedCell.Y);
			MaxCell.Z = FMath::Max(MaxCell.Z, OccupiedCell.Z);
		}

		return FIntVector(
			FMath::Max(1, MaxCell.X + 1),
			FMath::Max(1, MaxCell.Y + 1),
			FMath::Max(1, MaxCell.Z + 1));
	}

	FString BuildModuleValidationMessage(
		const ULayoutModuleAsset* Module,
		const FString& Summary,
		const TArray<FString>& DetailLines)
	{
		FString Message = Summary;
		Message += FString::Printf(TEXT("\nModule: %s"), Module != nullptr ? *Module->GetName() : TEXT("<none>"));
		for (const FString& DetailLine : DetailLines)
		{
			if (!DetailLine.IsEmpty())
			{
				Message += TEXT("\n");
				Message += DetailLine;
			}
		}

		return Message;
	}

}

const FLayoutFaceRule* ULayoutModuleAsset::FindFaceRule(const ELayoutFaceDirection Direction) const
{
	return FaceRules.FindRule(Direction);
}

FLayoutModuleFaceRules ULayoutModuleAsset::GetEffectiveFaceRules() const
{
	FLayoutModuleFaceRules EffectiveRules = FaceRules;
	EffectiveRules.NormalizeDirections();

	switch (FaceSymmetryMode)
	{
	case ELayoutFaceSymmetryMode::MirrorXFromPosX:
		CopyFaceRuleWithDirection(EffectiveRules, ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX);
		break;
	case ELayoutFaceSymmetryMode::MirrorYFromPosY:
		CopyFaceRuleWithDirection(EffectiveRules, ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY);
		break;
	case ELayoutFaceSymmetryMode::MirrorXYFromPositive:
		CopyFaceRuleWithDirection(EffectiveRules, ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX);
		CopyFaceRuleWithDirection(EffectiveRules, ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY);
		break;
	case ELayoutFaceSymmetryMode::RadialHorizontalFromPosX:
		CopyFaceRuleWithDirection(EffectiveRules, ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX);
		CopyFaceRuleWithDirection(EffectiveRules, ELayoutFaceDirection::PosX, ELayoutFaceDirection::PosY);
		CopyFaceRuleWithDirection(EffectiveRules, ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegY);
		break;
	case ELayoutFaceSymmetryMode::Independent:
	default:
		break;
	}

		EffectiveRules.NormalizeDirections();
	return EffectiveRules;
	}

bool ULayoutModuleAsset::GetEffectiveFaceRule(const ELayoutFaceDirection Direction, FLayoutFaceRule& OutFaceRule) const
{
	const FLayoutModuleFaceRules EffectiveRules = GetEffectiveFaceRules();
	const FLayoutFaceRule* Rule = EffectiveRules.FindRule(Direction);
	if (Rule == nullptr)
	{
		return false;
	}

	OutFaceRule = *Rule;
	return true;
}

TArray<int32> ULayoutModuleAsset::GetEffectiveYawRotationSteps() const
{
	// Module-local yaw restrictions are not part of the active leaf contract.
	// Leaf modules always expose all four yaw rotations now, and the broader
	// shared-cell-size owners validate whether that is legal.
	return {0, 1, 2, 3};
}

void ULayoutModuleAsset::NormalizeFaceRuleDirections()
{
	FaceRules.NormalizeDirections();
}

void ULayoutModuleAsset::PostLoad()
{
	Super::PostLoad();
	NormalizeFaceRuleDirections();
}

FLayoutValidationResult ULayoutModuleAsset::ValidateModule() const
{
	FLayoutValidationResult Result;

	if (Template.IsNull())
	{
		Result.AddError(BuildModuleValidationMessage(
			this,
			TEXT("Layout module is missing its chunk structure template."),
			{
				TEXT("Problem: Template is null."),
				TEXT("Fix: Assign the chunk structure template that should be stamped when this module is realized.")
			}));
	}

	const FIntVector EffectiveTemplateDimensionsBlocks = GetEffectiveTemplateDimensionsBlocks();
	if (EffectiveTemplateDimensionsBlocks.X <= 0
		|| EffectiveTemplateDimensionsBlocks.Y <= 0
		|| EffectiveTemplateDimensionsBlocks.Z <= 0)
	{
		Result.AddError(BuildModuleValidationMessage(
			this,
			TEXT("Layout module has invalid template dimensions."),
			{
				FString::Printf(TEXT("TemplateDimensionsBlocks: %s"), *EffectiveTemplateDimensionsBlocks.ToString()),
				TEXT("Problem: Effective template dimensions must be positive on all axes."),
				TEXT("Fix: Author a template with a positive X, Y, and Z size. Leaf modules now assume one occupied cell and derive their template dimensions directly from the template.")
			}));
	}

	const TArray<ELayoutCellIntent> EffectiveSupportedCellIntents = GetEffectiveSupportedCellIntents();
	if (Roles.IsEmpty())
	{
		Result.AddError(BuildModuleValidationMessage(
			this,
			TEXT("Layout module has no roles."),
			{
				TEXT("Problem: The solver cannot infer which planned cells this module may satisfy."),
				TEXT("Fix: Add at least one role such as Boundary, Entry, Interior, or VerticalAccess.")
			}));
	}

	if (MinWalkableFaces < 0)
	{
		Result.AddError(BuildModuleValidationMessage(
			this,
			TEXT("Layout module has an invalid MinWalkableFaces value."),
			{
				FString::Printf(TEXT("MinWalkableFaces: %d"), MinWalkableFaces),
				TEXT("Problem: MinWalkableFaces cannot be negative."),
				TEXT("Fix: Set MinWalkableFaces to zero or a positive count.")
			}));
	}

	const FLayoutModuleFaceRules EffectiveFaceRules = GetEffectiveFaceRules();
	const bool bPosZTraversable = IsFaceTraversable(EffectiveFaceRules.FindRule(ELayoutFaceDirection::PosZ));
	const bool bHasExplicitVerticalAccessRole = Roles.Contains(ELayoutModuleRole::VerticalAccess);
	if (!Roles.IsEmpty() && bPosZTraversable && !bHasExplicitVerticalAccessRole)
	{
		Result.AddError(BuildModuleValidationMessage(
			this,
			TEXT("Layout module exposes vertical traversal without the VerticalAccess role."),
			{
				TEXT("Face: PosZ"),
				TEXT("Problem: PosZ is traversable, so the solver treats this as vertical-access content."),
				TEXT("Fix: Add the VerticalAccess role, or remove traversal from PosZ if this is not a stair, lift, or receiver.")
			}));
	}
	else if (!Roles.IsEmpty() && !bPosZTraversable && bHasExplicitVerticalAccessRole)
	{
		Result.AddError(BuildModuleValidationMessage(
			this,
			TEXT("Layout module declares VerticalAccess but does not expose a traversable top face."),
			{
				TEXT("Face: PosZ"),
				TEXT("Problem: VerticalAccess modules must provide upward traversal on PosZ."),
				TEXT("Fix: Add Connected Traversal Channels to PosZ, or remove the VerticalAccess role if this module is not part of a vertical connection.")
			}));
	}

	const TArray<FLayoutFaceRule> Rules = EffectiveFaceRules.ToArray();
	const FGameplayTagContainer EffectiveTraversalChannels = GetEffectiveTraversalChannels();
	bool bHasExplicitEntryFace = false;
	int32 AvailableWalkableFaceCount = 0;
	for (const FLayoutFaceRule& FaceRule : Rules)
	{
		if (CanFaceConnectToFilledWalkableNeighbor(FaceRule))
		{
			++AvailableWalkableFaceCount;
		}

		if (FaceRule.GetEffectiveConnectionTags().IsEmpty())
		{
			Result.AddError(BuildModuleValidationMessage(
				this,
				FString::Printf(TEXT("Face %s is missing a connection tag."), ToModuleAssetFaceDirectionText(FaceRule.Direction)),
				{
					FString::Printf(TEXT("Face: %s"), ToModuleAssetFaceDirectionText(FaceRule.Direction)),
					TEXT("Problem: Every face must describe its interface contract with at least one connection tag."),
					TEXT("Fix: Assign Solid, Open, Entry, or another valid connection tag to this face.")
				}));
		}
		if (FaceRule.GetEffectiveConnectionTags().HasTagExact(LayoutGameplayTags::FaceEntry)
			&& FaceRule.ConnectedTraversalChannels.IsEmpty())
		{
			Result.AddWarning(BuildModuleValidationMessage(
				this,
				FString::Printf(TEXT("Face %s is tagged as Entry but does not expose traversal."), ToModuleAssetFaceDirectionText(FaceRule.Direction)),
				{
					FString::Printf(TEXT("Face: %s"), ToModuleAssetFaceDirectionText(FaceRule.Direction)),
					TEXT("Problem: Entry faces should normally expose at least one traversal channel so reachability can anchor on the entry."),
					TEXT("Fix: Add Connected Traversal Channels to this face, or remove the Entry tag if this face is not the region ingress.")
				}));
		}

		bHasExplicitEntryFace |= LayoutEntryRootUtilities::FaceProvidesExplicitEntry(FaceRule);

		if ((FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor)
			&& FaceRule.GetEffectiveAllowedConnectionTags().IsEmpty())
		{
			Result.AddError(BuildModuleValidationMessage(
				this,
				FString::Printf(TEXT("Face %s can touch a filled neighbor but has no allowed connection tags."), ToModuleAssetFaceDirectionText(FaceRule.Direction)),
				{
					FString::Printf(TEXT("Face: %s"), ToModuleAssetFaceDirectionText(FaceRule.Direction)),
					FString::Printf(TEXT("Occupancy: %s"), *StaticEnum<ELayoutFaceOccupancyPolicy>()->GetNameStringByValue(static_cast<int64>(FaceRule.OccupancyPolicy))),
					TEXT("Problem: Filled-neighbor faces must state which opposing connection tags they accept."),
					TEXT("Fix: Add one or more Allowed Connection Tags that match the neighbor faces this module is meant to touch.")
				}));
		}
		if ((FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor)
			&& FaceRule.ConnectedTraversalChannels.IsEmpty())
		{
			Result.AddError(BuildModuleValidationMessage(
				this,
				FString::Printf(TEXT("Face %s requires a walkable neighbor but has no traversal channels."), ToModuleAssetFaceDirectionText(FaceRule.Direction)),
				{
					FString::Printf(TEXT("Face: %s"), ToModuleAssetFaceDirectionText(FaceRule.Direction)),
					FString::Printf(TEXT("Occupancy: %s"), *StaticEnum<ELayoutFaceOccupancyPolicy>()->GetNameStringByValue(static_cast<int64>(FaceRule.OccupancyPolicy))),
					TEXT("Problem: Walkable-neighbor occupancy requires Connected Traversal Channels on this face."),
					TEXT("Fix: Add the traversal channel that should pass through this face, or use a non-walkable occupancy policy.")
				}));
		}

		const bool bHorizontalFace = FaceRule.Direction == ELayoutFaceDirection::PosX
			|| FaceRule.Direction == ELayoutFaceDirection::NegX
			|| FaceRule.Direction == ELayoutFaceDirection::PosY
			|| FaceRule.Direction == ELayoutFaceDirection::NegY;
		if (FaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceTerrainSeam
			&& (!bHorizontalFace
				|| FaceRule.OccupancyPolicy != ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor))
		{
			Result.AddError(BuildModuleValidationMessage(
				this,
				FString::Printf(TEXT("Terrain seam face %s has an invalid seam contract."), ToModuleAssetFaceDirectionText(FaceRule.Direction)),
				{
					FString::Printf(TEXT("Face: %s"), ToModuleAssetFaceDirectionText(FaceRule.Direction)),
					TEXT("Problem: Terrain seam faces must be horizontal and require a filled neighbor."),
					TEXT("Fix: Use a horizontal face with Requires Filled Neighbor. Add Connected Traversal Channels only for a seam door or passage.")
				}));
		}
		if (FaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceExteriorOrTerrainSeam
			&& (!bHorizontalFace
				|| (FaceRule.OccupancyPolicy != ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor
					&& FaceRule.OccupancyPolicy != ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor)))
		{
			Result.AddError(BuildModuleValidationMessage(
				this,
				FString::Printf(TEXT("Exterior-or-terrain-seam face %s has an invalid shared-edge contract."), ToModuleAssetFaceDirectionText(FaceRule.Direction)),
				{
					FString::Printf(TEXT("Face: %s"), ToModuleAssetFaceDirectionText(FaceRule.Direction)),
					TEXT("Problem: Shared exterior/terrain-seam faces must be horizontal and permit both empty exterior and filled seam neighbors."),
					TEXT("Fix: Use Allows Empty Or Filled Neighbor for walls, or Allows Empty Or Traversable Filled Neighbor for doors and passages.")
				}));
		}
		if (bHorizontalFace && FaceRule.bRequireMatchingYawWithFilledNeighbor)
		{
			Result.AddWarning(BuildModuleValidationMessage(
				this,
				FString::Printf(TEXT("Horizontal face %s authors a deprecated matching-yaw requirement."), ToModuleAssetFaceDirectionText(FaceRule.Direction)),
				{
					FString::Printf(TEXT("Face: %s"), ToModuleAssetFaceDirectionText(FaceRule.Direction)),
					TEXT("Impact: The active path now ignores matching-yaw requirements on horizontal faces."),
					TEXT("Fix: Clear bRequireMatchingYawWithFilledNeighbor on horizontal faces, or keep it only for legacy fixture/import-export compatibility while the old field is being retired.")
				}));
		}
	}

	if (MinWalkableFaces > AvailableWalkableFaceCount)
	{
		Result.AddWarning(BuildModuleValidationMessage(
			this,
			TEXT("MinWalkableFaces is higher than this module can actually expose."),
			{
				FString::Printf(TEXT("Authored minimum: %d"), MinWalkableFaces),
				FString::Printf(TEXT("Available walkable faces: %d"), AvailableWalkableFaceCount),
				TEXT("Impact: The solver will clamp the effective minimum to the available face count."),
				TEXT("Fix: Lower MinWalkableFaces or add more walkable faces if the higher requirement is intentional.")
			}));
	}

	if (EffectiveSupportedCellIntents.Contains(ELayoutCellIntent::Entry) && !bHasExplicitEntryFace)
	{
		Result.AddWarning(BuildModuleValidationMessage(
			this,
			TEXT("Entry-capable module has no explicit traversable Entry face."),
			{
				TEXT("Problem: Entry modules should usually expose at least one traversable Layout.Face.Entry face."),
				TEXT("Fix: Tag the ingress face with Layout.Face.Entry and add traversal, or remove the Entry role if this module is not meant to satisfy entry cells.")
			}));
	}

	for (const FLayoutInternalAccessLink& Link : GetEffectiveInternalAccessLinks())
	{
		if (!Link.FromTraversalChannel.IsValid() || !EffectiveTraversalChannels.HasTagExact(Link.FromTraversalChannel))
		{
			Result.AddError(BuildModuleValidationMessage(
				this,
				TEXT("Internal access link references a missing source traversal channel."),
				{
					FString::Printf(TEXT("Source channel: %s"), Link.FromTraversalChannel.IsValid() ? *Link.FromTraversalChannel.ToString() : TEXT("<invalid>")),
					TEXT("Problem: The source traversal channel is not exposed by any face on this module."),
					TEXT("Fix: Add the traversal channel to a face, or change the internal access link to use an exposed channel.")
				}));
		}

		if (!Link.ToTraversalChannel.IsValid() || !EffectiveTraversalChannels.HasTagExact(Link.ToTraversalChannel))
		{
			Result.AddError(BuildModuleValidationMessage(
				this,
				TEXT("Internal access link references a missing destination traversal channel."),
				{
					FString::Printf(TEXT("Destination channel: %s"), Link.ToTraversalChannel.IsValid() ? *Link.ToTraversalChannel.ToString() : TEXT("<invalid>")),
					TEXT("Problem: The destination traversal channel is not exposed by any face on this module."),
					TEXT("Fix: Add the traversal channel to a face, or change the internal access link to use an exposed channel.")
				}));
		}

		if (Link.FromTraversalChannel.IsValid()
			&& Link.ToTraversalChannel.IsValid()
			&& Link.FromTraversalChannel == Link.ToTraversalChannel)
		{
			Result.AddWarning(FString::Printf(
				TEXT("Internal access link is redundant.\nModule: %s\nFrom: %s\nTo: %s\nProblem: Faces already share reachability when they expose the same traversal channel.\nFix: Remove this link unless you expect the channels to diverge later."),
				*GetName(),
				*Link.FromTraversalChannel.ToString(),
				*Link.ToTraversalChannel.ToString()));
		}
	}

	return Result;
}

bool ULayoutModuleAsset::SupportsIntent(const ELayoutCellIntent Intent) const
{
	return GetEffectiveSupportedCellIntents().Contains(Intent);
}

TArray<ELayoutModuleRole> ULayoutModuleAsset::GetEffectiveRoles() const
{
	return Roles;
}

TArray<ELayoutCellIntent> ULayoutModuleAsset::GetEffectiveSupportedCellIntents() const
{
	TArray<ELayoutCellIntent> Result;
	for (const ELayoutModuleRole Role : GetEffectiveRoles())
	{
		switch (Role)
		{
		case ELayoutModuleRole::Boundary:
			Result.AddUnique(ELayoutCellIntent::Boundary);
			break;
		case ELayoutModuleRole::Entry:
			Result.AddUnique(ELayoutCellIntent::Entry);
			Result.AddUnique(ELayoutCellIntent::Connector);
			break;
		case ELayoutModuleRole::Interior:
			Result.AddUnique(ELayoutCellIntent::Interior);
			Result.AddUnique(ELayoutCellIntent::Core);
			Result.AddUnique(ELayoutCellIntent::Connector);
			break;
		case ELayoutModuleRole::VerticalAccess:
			Result.AddUnique(ELayoutCellIntent::VerticalAccess);
			break;
		default:
			break;
		}
	}

	return Result;
}

bool ULayoutModuleAsset::HasEffectiveRole(const ELayoutModuleRole Role) const
{
	return GetEffectiveRoles().Contains(Role);
}

bool ULayoutModuleAsset::ExposesTraversalChannel(const FGameplayTag& TraversalChannel) const
{
	return TraversalChannel.IsValid()
		&& GetEffectiveTraversalChannels().HasTagExact(TraversalChannel);
}

FGameplayTagContainer ULayoutModuleAsset::GetEffectiveTraversalChannels() const
{
	FGameplayTagContainer Result;
	for (const FLayoutFaceRule& FaceRule : GetEffectiveFaceRules().ToArray())
	{
		Result.AppendTags(FaceRule.ConnectedTraversalChannels);
	}

	return Result;
}

TArray<FLayoutInternalAccessLink> ULayoutModuleAsset::GetEffectiveInternalAccessLinks() const
{
	return InternalAccessLinks;
}

FIntVector ULayoutModuleAsset::GetEffectiveCellSizeInBlocks() const
{
	return GetEffectiveTemplateDimensionsBlocks();
}

FIntVector ULayoutModuleAsset::GetEffectiveTemplateDimensionsBlocks() const
{
	return ComputeEffectiveTemplateDimensionsBlocks(this);
}

TArray<FIntVector> ULayoutModuleAsset::GetOccupiedLocalCells() const
{
	return BuildOccupiedLocalCells(this);
}

FIntVector ULayoutModuleAsset::GetOccupiedBoundsCells() const
{
	return BuildOccupiedBoundsCells(BuildOccupiedLocalCells(this));
}

#if WITH_EDITOR
void ULayoutModuleAsset::ValidateLayoutModuleInEditor() const
{
	PorismLayoutEditorMessageLog::ReportValidationResult(
		this,
		ValidateModule(),
		TEXT("Layout module validation passed."));
}

void ULayoutModuleAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	NormalizeFaceRuleDirections();
}

EDataValidationResult ULayoutModuleAsset::IsDataValid(FDataValidationContext& Context) const
{
	const FLayoutValidationResult ValidationResult = ValidateModule();
	AppendModuleValidationResult(Context, ValidationResult);
	return ValidationResult.IsValid() ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
