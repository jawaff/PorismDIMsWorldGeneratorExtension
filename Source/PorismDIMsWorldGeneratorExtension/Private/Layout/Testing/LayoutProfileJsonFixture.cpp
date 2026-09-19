// Copyright 2026 Spotted Loaf Studio

#include "Layout/Testing/LayoutProfileJsonFixture.h"

#include "ChunkWorldStructs/ChunkStructureTemplate.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Dom/JsonObject.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Runtime/LayoutWorldBindingRuntimeHelpers.h"
#include "Layout/Solver/LayoutChildRequestTemplateCacheBuilder.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Solver/LayoutWorldBindingSolveRequestBuilder.h"
#include "Layout/Types/LayoutTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

namespace
{
	constexpr int32 LayoutRecursiveFixtureSchemaVersion = 4;
	const FName FixtureReplayPlacementPolicyId(TEXT("FixtureReplay"));

	FLayoutWorldBindingPlacementPolicy BuildFixtureReplayPlacementPolicy()
	{
		FLayoutWorldBindingPlacementPolicy PlacementPolicy;
		return PlacementPolicy;
	}

	bool ShouldDeriveFixtureReplaySteppedTerrainSupport(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FLayoutImportedRootSteppedTerrainSupportContext* const SteppedTerrainSupportContext)
	{
		return SteppedTerrainSupportContext != nullptr
			&& SteppedTerrainSupportContext->ActiveBiomeSampler != nullptr
			&& RuntimeView.LayoutProfile != nullptr
			&& RuntimeView.LayoutProfile->bSupportsSteppedTerrainSolve
			&& RuntimeView.PlacementKind != ELayoutWorldBindingPlacementKind::None;
	}

	TSharedPtr<FLayoutChildRequestTemplateSnapshot> BuildImportedChildRequestTemplateSnapshot(
		const ULayoutProfileAsset* Profile,
		const int32 SnapshotSchemaVersion,
		LayoutChildRequestTemplateCacheBuilder::FRecursiveChildTemplateBuildState& BuildState)
	{
		return LayoutChildRequestTemplateCacheBuilder::BuildChildRequestTemplateSnapshot(
			Profile,
			BuildState,
			SnapshotSchemaVersion,
			[](const ULayoutProfileAsset* ChildProfile)
			{
				return FLayoutProfileSolver::BuildProfileSnapshot(ChildProfile);
			},
			[](const ULayoutRegionContentSetAsset* ChildContentSet)
			{
				return FLayoutProfileSolver::BuildContentSetSnapshot(ChildContentSet);
			},
			[](const ULayoutRegionContentSetAsset* ChildContentSet)
			{
				return FLayoutProfileSolver::BuildModuleCatalog(ChildContentSet);
			});
	}

	bool IsFixtureCompositeCellEarlier(
		const FLayoutCompositeModuleCell& Left,
		const FLayoutCompositeModuleCell& Right)
	{
		const auto CompareInt = [](const int32 LeftValue, const int32 RightValue) -> int32
		{
			return LeftValue < RightValue ? -1 : (LeftValue > RightValue ? 1 : 0);
		};

		int32 Comparison = CompareInt(Left.LocalCell.Z, Right.LocalCell.Z);
		if (Comparison == 0)
		{
			Comparison = CompareInt(Left.LocalCell.Y, Right.LocalCell.Y);
		}
		if (Comparison == 0)
		{
			Comparison = CompareInt(Left.LocalCell.X, Right.LocalCell.X);
		}
		if (Comparison == 0)
		{
			const FName LeftModuleId = Left.Module != nullptr ? Left.Module->GetFName() : NAME_None;
			const FName RightModuleId = Right.Module != nullptr ? Right.Module->GetFName() : NAME_None;
			Comparison = LeftModuleId.LexicalLess(RightModuleId) ? -1 : (RightModuleId.LexicalLess(LeftModuleId) ? 1 : 0);
		}
		if (Comparison == 0)
		{
			Comparison = CompareInt(Left.RelativeYawRotationSteps, Right.RelativeYawRotationSteps);
		}

		return Comparison < 0;
	}

	void CanonicalizeFixtureCompositeCells(TArray<FLayoutCompositeModuleCell>& Cells)
	{
		Cells.Sort([](const FLayoutCompositeModuleCell& Left, const FLayoutCompositeModuleCell& Right)
		{
			return IsFixtureCompositeCellEarlier(Left, Right);
		});
	}

	void VisitFixtureJsonObjectsRecursively(
		const TSharedPtr<FJsonObject>& RootObject,
		TFunctionRef<void(const TSharedPtr<FJsonObject>&)> Visitor)
	{
		if (!RootObject.IsValid())
		{
			return;
		}

		Visitor(RootObject);

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : RootObject->Values)
		{
			const TSharedPtr<FJsonValue>& Value = Pair.Value;
			if (!Value.IsValid())
			{
				continue;
			}

			if (Value->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject> ChildObject = Value->AsObject();
				if (!ChildObject.IsValid())
				{
					continue;
				}

				VisitFixtureJsonObjectsRecursively(ChildObject, Visitor);
				continue;
			}

			if (Value->Type != EJson::Array)
			{
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>& ChildArray = Value->AsArray();
			for (const TSharedPtr<FJsonValue>& ArrayValue : ChildArray)
			{
				if (!ArrayValue.IsValid() || ArrayValue->Type != EJson::Object)
				{
					continue;
				}

				const TSharedPtr<FJsonObject> ArrayObject = ArrayValue->AsObject();
				if (ArrayObject.IsValid())
				{
					VisitFixtureJsonObjectsRecursively(ArrayObject, Visitor);
				}
			}
		}
	}

	struct FImportedSparsePlacementRuleAuthoring
	{
		FInstancedStruct Rule;
		FName ContentSetName;
	};

	struct FImportedProfileAuthoring
	{
		TObjectPtr<ULayoutProfileAsset> Profile = nullptr;
		FName ContentSetId;
		TArray<FImportedSparsePlacementRuleAuthoring> SparseRules;
	};

	template <typename EnumType>
	FString EnumToString(const EnumType Value)
	{
		const UEnum* Enum = StaticEnum<EnumType>();
		return Enum != nullptr ? Enum->GetNameStringByValue(static_cast<int64>(Value)) : FString();
	}

	template <typename EnumType>
	EnumType StringToEnum(const FString& Value, const EnumType DefaultValue, TArray<FString>& Issues, const TCHAR* FieldName)
	{
		const UEnum* Enum = StaticEnum<EnumType>();
		if (Enum == nullptr)
		{
			return DefaultValue;
		}

		const int64 EnumValue = Enum->GetValueByNameString(Value);
		if (EnumValue == INDEX_NONE)
		{
			Issues.Add(FString::Printf(TEXT("Unknown enum value '%s' for %s."), *Value, FieldName));
			return DefaultValue;
		}

		return static_cast<EnumType>(EnumValue);
	}

	TSharedPtr<FJsonObject> VectorToJson(const FIntVector& Value)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("x"), Value.X);
		Object->SetNumberField(TEXT("y"), Value.Y);
		Object->SetNumberField(TEXT("z"), Value.Z);
		return Object;
	}

	FIntVector JsonToVector(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return FIntVector::ZeroValue;
		}

		return FIntVector(
			static_cast<int32>(Object->GetIntegerField(TEXT("x"))),
			static_cast<int32>(Object->GetIntegerField(TEXT("y"))),
			static_cast<int32>(Object->GetIntegerField(TEXT("z"))));
	}

	TSharedPtr<FJsonObject> PointToJson(const FIntPoint& Value)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("x"), Value.X);
		Object->SetNumberField(TEXT("y"), Value.Y);
		return Object;
	}

	FIntPoint JsonToPoint(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return FIntPoint::ZeroValue;
		}

		return FIntPoint(
			static_cast<int32>(Object->GetIntegerField(TEXT("x"))),
			static_cast<int32>(Object->GetIntegerField(TEXT("y"))));
	}

	TMap<FString, FIntVector> BuildFixtureTemplateSizesByModuleIdentityFromCompiledSnapshot(
		const TSharedPtr<FJsonObject>& CompiledSnapshotObject)
	{
		TMap<FString, FIntVector> TemplateSizesByIdentity;
		if (!CompiledSnapshotObject.IsValid())
		{
			return TemplateSizesByIdentity;
		}

		VisitFixtureJsonObjectsRecursively(CompiledSnapshotObject, [&TemplateSizesByIdentity](const TSharedPtr<FJsonObject>& Object)
		{
			const TArray<TSharedPtr<FJsonValue>>* JsonModules = nullptr;
			Object->TryGetArrayField(TEXT("modules"), JsonModules);
			if (JsonModules == nullptr)
			{
				return;
			}

			for (const TSharedPtr<FJsonValue>& JsonModuleValue : *JsonModules)
			{
				const TSharedPtr<FJsonObject> ModuleObject = JsonModuleValue.IsValid() ? JsonModuleValue->AsObject() : nullptr;
				if (!ModuleObject.IsValid())
				{
					continue;
				}

				const FIntVector TemplateDimensions = ModuleObject->HasField(TEXT("template_dimensions_blocks"))
					? JsonToVector(ModuleObject->GetObjectField(TEXT("template_dimensions_blocks")))
					: (ModuleObject->HasField(TEXT("cell_size_in_blocks"))
						? JsonToVector(ModuleObject->GetObjectField(TEXT("cell_size_in_blocks")))
						: FIntVector::ZeroValue);
				if (TemplateDimensions.X <= 0 || TemplateDimensions.Y <= 0 || TemplateDimensions.Z <= 0)
				{
					continue;
				}

				const FString SnapshotId = ModuleObject->GetStringField(TEXT("snapshot_id"));
				const FString DebugName = ModuleObject->GetStringField(TEXT("debug_name"));
				const FString TemplateName = ModuleObject->GetStringField(TEXT("template_name"));
				if (!SnapshotId.IsEmpty())
				{
					TemplateSizesByIdentity.Add(SnapshotId, TemplateDimensions);
				}
				if (!DebugName.IsEmpty())
				{
					TemplateSizesByIdentity.Add(DebugName, TemplateDimensions);
				}
				if (!TemplateName.IsEmpty())
				{
					TemplateSizesByIdentity.Add(TemplateName, TemplateDimensions);
				}
			}
		});

		return TemplateSizesByIdentity;
	}

	TMap<FString, FIntVector> BuildFixtureSharedCellSizesByIdentityFromCompiledSnapshot(
		const TSharedPtr<FJsonObject>& CompiledSnapshotObject)
	{
		TMap<FString, FIntVector> SharedCellSizesByIdentity;
		if (!CompiledSnapshotObject.IsValid())
		{
			return SharedCellSizesByIdentity;
		}

		VisitFixtureJsonObjectsRecursively(CompiledSnapshotObject, [&SharedCellSizesByIdentity](const TSharedPtr<FJsonObject>& Object)
		{
			if (!Object->HasField(TEXT("shared_cell_size_in_blocks")))
			{
				return;
			}

			const FIntVector SharedCellSize = JsonToVector(Object->GetObjectField(TEXT("shared_cell_size_in_blocks")));
			if (SharedCellSize == FIntVector::ZeroValue)
			{
				return;
			}

			const FString SnapshotId = Object->GetStringField(TEXT("snapshot_id"));
			const FString DebugName = Object->GetStringField(TEXT("debug_name"));
			if (!SnapshotId.IsEmpty())
			{
				SharedCellSizesByIdentity.Add(SnapshotId, SharedCellSize);
			}
			if (!DebugName.IsEmpty())
			{
				SharedCellSizesByIdentity.Add(DebugName, SharedCellSize);
			}
		});

		return SharedCellSizesByIdentity;
	}

	FIntVector FindFirstPositiveFixtureVector(const TMap<FString, FIntVector>& ValuesByIdentity)
	{
		for (const TPair<FString, FIntVector>& Pair : ValuesByIdentity)
		{
			if (Pair.Value.X > 0 && Pair.Value.Y > 0 && Pair.Value.Z > 0)
			{
				return Pair.Value;
			}
		}

		return FIntVector::ZeroValue;
	}

	bool HasPositiveFixtureSharedCellSize(const FIntVector& SharedCellSizeInBlocks)
	{
		return SharedCellSizeInBlocks.X > 0
			&& SharedCellSizeInBlocks.Y > 0
			&& SharedCellSizeInBlocks.Z > 0;
	}

	bool TryResolveDerivedFixtureContentSetSharedCellSizeInBlocks(
		const ULayoutRegionContentSetAsset* ContentSet,
		FIntVector& OutSharedCellSizeInBlocks)
	{
		if (ContentSet == nullptr)
		{
			return false;
		}

		for (const FLayoutRegionContentEntry& Entry : ContentSet->Entries)
		{
			if (Entry.ContentKind != ELayoutRegionContentKind::Module)
			{
				continue;
			}

			const bool bHasLeafModule = Entry.ModuleSettings.Module != nullptr;
			const bool bHasCompositeModule = Entry.ModuleSettings.CompositeModule != nullptr;
			if (bHasLeafModule == bHasCompositeModule)
			{
				continue;
			}

			const FIntVector DerivedSharedCellSizeInBlocks = bHasLeafModule
				? Entry.ModuleSettings.Module->GetEffectiveCellSizeInBlocks()
				: Entry.ModuleSettings.CompositeModule->GetSharedCellSizeInBlocks();
			if (HasPositiveFixtureSharedCellSize(DerivedSharedCellSizeInBlocks))
			{
				OutSharedCellSizeInBlocks = DerivedSharedCellSizeInBlocks;
				return true;
			}
		}

		return false;
	}

	bool TryResolveRecursiveFixtureContentSetSharedCellSizeInBlocks(
		const ULayoutRegionContentSetAsset* ContentSet,
		TSet<const ULayoutRegionContentSetAsset*>& VisitedContentSets,
		FIntVector& OutSharedCellSizeInBlocks)
	{
		if (ContentSet == nullptr || VisitedContentSets.Contains(ContentSet))
		{
			return false;
		}

		VisitedContentSets.Add(ContentSet);

		if (TryResolveDerivedFixtureContentSetSharedCellSizeInBlocks(ContentSet, OutSharedCellSizeInBlocks))
		{
			return true;
		}

		for (const FLayoutRegionContentEntry& Entry : ContentSet->Entries)
		{
			if (Entry.ContentKind != ELayoutRegionContentKind::ChildRegion)
			{
				continue;
			}

			const ULayoutProfileAsset* ChildProfile = Entry.ChildRegionSettings.RegionProfile;
			if (ChildProfile == nullptr || ChildProfile->ContentSet == nullptr)
			{
				continue;
			}

			if (TryResolveRecursiveFixtureContentSetSharedCellSizeInBlocks(
				ChildProfile->ContentSet,
				VisitedContentSets,
				OutSharedCellSizeInBlocks))
			{
				return true;
			}
		}

		return false;
	}

	bool TryResolveRecursiveFixtureContentSetSharedCellSizeInBlocks(
		const ULayoutRegionContentSetAsset* ContentSet,
		FIntVector& OutSharedCellSizeInBlocks)
	{
		TSet<const ULayoutRegionContentSetAsset*> VisitedContentSets;
		return TryResolveRecursiveFixtureContentSetSharedCellSizeInBlocks(
			ContentSet,
			VisitedContentSets,
			OutSharedCellSizeInBlocks);
	}

	FIntVector FindFixtureContentSetSharedCellSizeByIdentity(
		const FName ContentSetId,
		const ULayoutRegionContentSetAsset* ContentSet,
		const TMap<FString, FIntVector>& SharedCellSizesByIdentity)
	{
		if (const FIntVector* SharedCellSizeById = SharedCellSizesByIdentity.Find(ContentSetId.ToString()))
		{
			return *SharedCellSizeById;
		}

		if (ContentSet != nullptr)
		{
			if (const FIntVector* SharedCellSizeByName = SharedCellSizesByIdentity.Find(ContentSet->GetName()))
			{
				return *SharedCellSizeByName;
			}
		}

		return FIntVector::ZeroValue;
	}

	void NormalizeImportedRecursiveFixtureOneCellLeafContract(
		const TMap<FName, TObjectPtr<ULayoutRegionContentSetAsset>>& ContentSetsById,
		const TMap<FString, FIntVector>& SharedCellSizesByIdentity,
		const FIntVector& DefaultSharedCellSize)
	{
		for (const TPair<FName, TObjectPtr<ULayoutRegionContentSetAsset>>& Pair : ContentSetsById)
		{
			ULayoutRegionContentSetAsset* const ContentSet = Pair.Value.Get();
			if (ContentSet == nullptr)
			{
				continue;
			}

			FIntVector DerivedSharedCellSizeInBlocks = FIntVector::ZeroValue;
			const bool bHasDerivedSharedCellSize =
				TryResolveDerivedFixtureContentSetSharedCellSizeInBlocks(
					ContentSet,
					DerivedSharedCellSizeInBlocks);
			const FIntVector ImportedSharedCellSizeInBlocks =
				FindFixtureContentSetSharedCellSizeByIdentity(
					Pair.Key,
					ContentSet,
					SharedCellSizesByIdentity);

			const FIntVector EffectiveSharedCellSize =
				bHasDerivedSharedCellSize
					? DerivedSharedCellSizeInBlocks
					: (HasPositiveFixtureSharedCellSize(ImportedSharedCellSizeInBlocks)
						? ImportedSharedCellSizeInBlocks
						: DefaultSharedCellSize);
			if (!HasPositiveFixtureSharedCellSize(EffectiveSharedCellSize))
			{
				continue;
			}

			for (FLayoutRegionContentEntry& Entry : ContentSet->Entries)
			{
				ULayoutModuleAsset* const Module = Entry.ModuleSettings.Module;
				if (Module == nullptr)
				{
					continue;
				}

				if (Module->GetEffectiveTemplateDimensionsBlocks().X > 0
					&& Module->GetEffectiveTemplateDimensionsBlocks().Y > 0
					&& Module->GetEffectiveTemplateDimensionsBlocks().Z > 0)
				{
					continue;
				}

				UChunkStructureTemplate* Template = Module->Template.Get();
				if (Template == nullptr)
				{
					Template = NewObject<UChunkStructureTemplate>(
						Module,
						MakeUniqueObjectName(
							Module,
							UChunkStructureTemplate::StaticClass(),
							FName(*(Module->GetName() + TEXT("_Template")))));
					Module->Template = Template;
				}

				Template->SizeInBlocks = EffectiveSharedCellSize;
			}
		}
	}

	TSharedPtr<FJsonObject> BoundsPolicyToJson(const FLayoutBoundsPolicy& Policy)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("mode"), EnumToString(Policy.Mode));
		Object->SetObjectField(TEXT("min_cells"), VectorToJson(Policy.MinCells));
		Object->SetObjectField(TEXT("max_cells"), VectorToJson(Policy.MaxCells));
		TArray<TSharedPtr<FJsonValue>> JsonMaskCells;
		for (const FIntVector& MaskCell : Policy.FootprintMask)
		{
			JsonMaskCells.Add(MakeShared<FJsonValueObject>(VectorToJson(MaskCell)));
		}
		Object->SetArrayField(TEXT("footprint_mask"), JsonMaskCells);
		Object->SetNumberField(TEXT("inset_cells"), Policy.InsetCells);
		Object->SetNumberField(TEXT("min_level"), Policy.MinLevel);
		Object->SetNumberField(TEXT("max_level"), Policy.MaxLevel);
		return Object;
	}

	FLayoutBoundsPolicy JsonToBoundsPolicy(const TSharedPtr<FJsonObject>& Object, TArray<FString>& Issues, const FString& FieldPath)
	{
		FLayoutBoundsPolicy Policy;
		if (!Object.IsValid())
		{
			Issues.Add(FString::Printf(TEXT("Invalid bounds policy object in %s."), *FieldPath));
			return Policy;
		}

		Policy.Mode = StringToEnum(Object->GetStringField(TEXT("mode")), ELayoutBoundsPolicyMode::SolvedFootprint, Issues, TEXT("mode"));
		Policy.MinCells = JsonToVector(Object->GetObjectField(TEXT("min_cells")));
		Policy.MaxCells = JsonToVector(Object->GetObjectField(TEXT("max_cells")));
		const TArray<TSharedPtr<FJsonValue>>* JsonMaskCells = nullptr;
		Object->TryGetArrayField(TEXT("footprint_mask"), JsonMaskCells);
		if (JsonMaskCells != nullptr)
		{
			for (int32 MaskIndex = 0; MaskIndex < JsonMaskCells->Num(); ++MaskIndex)
			{
				const TSharedPtr<FJsonObject> MaskCellObject = (*JsonMaskCells)[MaskIndex].IsValid() ? (*JsonMaskCells)[MaskIndex]->AsObject() : nullptr;
				if (!MaskCellObject.IsValid())
				{
					Issues.Add(FString::Printf(TEXT("Invalid footprint mask cell object at %s.footprint_mask[%d]."), *FieldPath, MaskIndex));
					continue;
				}

				Policy.FootprintMask.Add(JsonToVector(MaskCellObject));
			}
		}
		Policy.InsetCells = static_cast<int32>(Object->GetIntegerField(TEXT("inset_cells")));
		Policy.MinLevel = static_cast<int32>(Object->GetIntegerField(TEXT("min_level")));
		Policy.MaxLevel = static_cast<int32>(Object->GetIntegerField(TEXT("max_level")));
		return Policy;
	}

	TSharedPtr<FJsonObject> ClosureRequirementToJson(const FLayoutClosureRequirement& Requirement)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("closure_id"), Requirement.ClosureId.ToString());
		Object->SetStringField(TEXT("zone"), EnumToString(Requirement.Zone));
		Object->SetObjectField(TEXT("bounds_policy"), BoundsPolicyToJson(Requirement.BoundsPolicy));
		Object->SetNumberField(TEXT("min_thickness_cells"), Requirement.MinThicknessCells);
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> ClosureRequirementsToJson(const TArray<FLayoutClosureRequirement>& Requirements)
	{
		TArray<TSharedPtr<FJsonValue>> JsonRequirements;
		for (const FLayoutClosureRequirement& Requirement : Requirements)
		{
			JsonRequirements.Add(MakeShared<FJsonValueObject>(ClosureRequirementToJson(Requirement)));
		}
		return JsonRequirements;
	}

	TArray<FLayoutClosureRequirement> JsonToClosureRequirements(const TArray<TSharedPtr<FJsonValue>>* JsonRequirements, TArray<FString>& Issues, const FString& FieldPath)
	{
		TArray<FLayoutClosureRequirement> Requirements;
		if (JsonRequirements == nullptr)
		{
			return Requirements;
		}

		for (int32 RequirementIndex = 0; RequirementIndex < JsonRequirements->Num(); ++RequirementIndex)
		{
			const TSharedPtr<FJsonObject> RequirementObject = (*JsonRequirements)[RequirementIndex].IsValid() ? (*JsonRequirements)[RequirementIndex]->AsObject() : nullptr;
			if (!RequirementObject.IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Invalid boundary closure requirement object at %s[%d]."), *FieldPath, RequirementIndex));
				continue;
			}

			FLayoutClosureRequirement& Requirement = Requirements.AddDefaulted_GetRef();
			Requirement.ClosureId = FName(*RequirementObject->GetStringField(TEXT("closure_id")));
			FString ZoneString;
			if (!RequirementObject->TryGetStringField(TEXT("zone"), ZoneString))
			{
				RequirementObject->TryGetStringField(TEXT("boundary_zone"), ZoneString);
			}
			Requirement.Zone = StringToEnum(ZoneString, ELayoutPlacementZone::Perimeter, Issues, TEXT("zone"));
			const TSharedPtr<FJsonObject>* BoundsPolicyObject = nullptr;
			if (!RequirementObject->TryGetObjectField(TEXT("bounds_policy"), BoundsPolicyObject) || BoundsPolicyObject == nullptr || !BoundsPolicyObject->IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Missing bounds_policy object at %s[%d]."), *FieldPath, RequirementIndex));
			}
			else
			{
				Requirement.BoundsPolicy = JsonToBoundsPolicy(
					*BoundsPolicyObject,
					Issues,
					FString::Printf(TEXT("%s[%d].bounds_policy"), *FieldPath, RequirementIndex));
			}
			Requirement.MinThicknessCells = static_cast<int32>(RequirementObject->GetIntegerField(TEXT("min_thickness_cells")));
		}

		return Requirements;
	}

	TSharedPtr<FJsonObject> ClosureProviderIntentToJson(const FLayoutClosureProviderIntent& ProviderIntent)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("provider_intent_id"), ProviderIntent.ProviderIntentId.ToString());
		Object->SetStringField(TEXT("zone"), EnumToString(ProviderIntent.Zone));
		Object->SetStringField(TEXT("closure_id"), ProviderIntent.ClosureId.ToString());
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> ClosureProviderIntentsToJson(const TArray<FLayoutClosureProviderIntent>& ProviderIntents)
	{
		TArray<TSharedPtr<FJsonValue>> JsonProviderIntents;
		for (const FLayoutClosureProviderIntent& ProviderIntent : ProviderIntents)
		{
			JsonProviderIntents.Add(MakeShared<FJsonValueObject>(ClosureProviderIntentToJson(ProviderIntent)));
		}
		return JsonProviderIntents;
	}

	TArray<FLayoutClosureProviderIntent> JsonToClosureProviderIntents(const TArray<TSharedPtr<FJsonValue>>* JsonProviderIntents, TArray<FString>& Issues, const FString& FieldPath)
	{
		TArray<FLayoutClosureProviderIntent> ProviderIntents;
		if (JsonProviderIntents == nullptr)
		{
			return ProviderIntents;
		}

		for (int32 ProviderIntentIndex = 0; ProviderIntentIndex < JsonProviderIntents->Num(); ++ProviderIntentIndex)
		{
			const TSharedPtr<FJsonObject> ProviderIntentObject = (*JsonProviderIntents)[ProviderIntentIndex].IsValid() ? (*JsonProviderIntents)[ProviderIntentIndex]->AsObject() : nullptr;
			if (!ProviderIntentObject.IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Invalid closure provider intent object at %s[%d]."), *FieldPath, ProviderIntentIndex));
				continue;
			}

			FLayoutClosureProviderIntent& ProviderIntent = ProviderIntents.AddDefaulted_GetRef();
			ProviderIntent.ProviderIntentId = FName(*ProviderIntentObject->GetStringField(TEXT("provider_intent_id")));
			FString ZoneString;
			if (!ProviderIntentObject->TryGetStringField(TEXT("zone"), ZoneString))
			{
				ProviderIntentObject->TryGetStringField(TEXT("boundary_zone"), ZoneString);
			}
			ProviderIntent.Zone = StringToEnum(ZoneString, ELayoutPlacementZone::Perimeter, Issues, TEXT("zone"));
			const FString ClosureIdString = ProviderIntentObject->GetStringField(TEXT("closure_id"));
			ProviderIntent.ClosureId = ClosureIdString.IsEmpty() ? NAME_None : FName(*ClosureIdString);
		}

		return ProviderIntents;
	}

	TArray<TSharedPtr<FJsonValue>> TagsToJson(const FGameplayTagContainer& Tags);
	FGameplayTagContainer JsonToTags(const TArray<TSharedPtr<FJsonValue>>* JsonTags, TArray<FString>& Issues, const FString& FieldPath);

	TSharedPtr<FJsonObject> ZoneFeatureRequirementToJson(const FLayoutZoneFeatureRequirement& Requirement)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("requirement_id"), Requirement.RequirementId.ToString());
		Object->SetStringField(TEXT("zone"), EnumToString(Requirement.Zone));
		Object->SetArrayField(TEXT("required_features"), TagsToJson(Requirement.RequiredFeatures));
		Object->SetStringField(TEXT("match_mode"), EnumToString(Requirement.MatchMode));
		Object->SetNumberField(TEXT("min_count"), Requirement.MinCount);
		Object->SetNumberField(TEXT("max_count"), Requirement.MaxCount);
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> ZoneFeatureRequirementsToJson(const TArray<FLayoutZoneFeatureRequirement>& Requirements)
	{
		TArray<TSharedPtr<FJsonValue>> JsonRequirements;
		for (const FLayoutZoneFeatureRequirement& Requirement : Requirements)
		{
			JsonRequirements.Add(MakeShared<FJsonValueObject>(ZoneFeatureRequirementToJson(Requirement)));
		}
		return JsonRequirements;
	}

	TArray<FLayoutZoneFeatureRequirement> JsonToZoneFeatureRequirements(const TArray<TSharedPtr<FJsonValue>>* JsonRequirements, TArray<FString>& Issues, const FString& FieldPath)
	{
		TArray<FLayoutZoneFeatureRequirement> Requirements;
		if (JsonRequirements == nullptr)
		{
			return Requirements;
		}

		for (int32 RequirementIndex = 0; RequirementIndex < JsonRequirements->Num(); ++RequirementIndex)
		{
			const TSharedPtr<FJsonObject> RequirementObject = (*JsonRequirements)[RequirementIndex].IsValid() ? (*JsonRequirements)[RequirementIndex]->AsObject() : nullptr;
			if (!RequirementObject.IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Invalid zone feature requirement object at %s[%d]."), *FieldPath, RequirementIndex));
				continue;
			}

			FLayoutZoneFeatureRequirement& Requirement = Requirements.AddDefaulted_GetRef();
			Requirement.RequirementId = FName(*RequirementObject->GetStringField(TEXT("requirement_id")));
			Requirement.Zone = StringToEnum(
				RequirementObject->GetStringField(TEXT("zone")),
				ELayoutPlacementZone::Any,
				Issues,
				TEXT("zone"));
			const TArray<TSharedPtr<FJsonValue>>* JsonRequiredFeatures = nullptr;
			RequirementObject->TryGetArrayField(TEXT("required_features"), JsonRequiredFeatures);
			Requirement.RequiredFeatures = JsonToTags(JsonRequiredFeatures, Issues, FieldPath + TEXT(".required_features"));
			Requirement.MatchMode = StringToEnum(
				RequirementObject->GetStringField(TEXT("match_mode")),
				ELayoutZoneFeatureMatchMode::Any,
				Issues,
				TEXT("match_mode"));
			Requirement.MinCount = static_cast<int32>(RequirementObject->GetIntegerField(TEXT("min_count")));
			Requirement.MaxCount = static_cast<int32>(RequirementObject->GetIntegerField(TEXT("max_count")));
		}

		return Requirements;
	}

	TSharedPtr<FJsonObject> SeamProviderIntentToJson(const FLayoutSeamProviderIntent& SeamIntent)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("seam_intent_id"), SeamIntent.SeamIntentId.ToString());
		Object->SetStringField(TEXT("junction_usage"), EnumToString(SeamIntent.JunctionUsage));
		Object->SetStringField(TEXT("interface_family"), SeamIntent.InterfaceFamily.ToString());
		Object->SetBoolField(TEXT("can_own_seam"), SeamIntent.bCanOwnSeam);
		Object->SetBoolField(TEXT("can_accept_seam"), SeamIntent.bCanAcceptSeam);
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> SeamProviderIntentsToJson(const TArray<FLayoutSeamProviderIntent>& SeamIntents)
	{
		TArray<TSharedPtr<FJsonValue>> JsonSeamIntents;
		for (const FLayoutSeamProviderIntent& SeamIntent : SeamIntents)
		{
			JsonSeamIntents.Add(MakeShared<FJsonValueObject>(SeamProviderIntentToJson(SeamIntent)));
		}
		return JsonSeamIntents;
	}

	TArray<FLayoutSeamProviderIntent> JsonToSeamProviderIntents(const TArray<TSharedPtr<FJsonValue>>* JsonSeamIntents, TArray<FString>& Issues, const FString& FieldPath)
	{
		TArray<FLayoutSeamProviderIntent> SeamIntents;
		if (JsonSeamIntents == nullptr)
		{
			return SeamIntents;
		}

		for (int32 SeamIntentIndex = 0; SeamIntentIndex < JsonSeamIntents->Num(); ++SeamIntentIndex)
		{
			const TSharedPtr<FJsonObject> SeamIntentObject = (*JsonSeamIntents)[SeamIntentIndex].IsValid() ? (*JsonSeamIntents)[SeamIntentIndex]->AsObject() : nullptr;
			if (!SeamIntentObject.IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Invalid seam provider intent object at %s[%d]."), *FieldPath, SeamIntentIndex));
				continue;
			}

			FLayoutSeamProviderIntent& SeamIntent = SeamIntents.AddDefaulted_GetRef();
			SeamIntent.SeamIntentId = FName(*SeamIntentObject->GetStringField(TEXT("seam_intent_id")));
			FString JunctionUsageString;
			if (SeamIntentObject->TryGetStringField(
					TEXT("junction_usage"),
					JunctionUsageString)
				&& !JunctionUsageString.IsEmpty())
			{
				SeamIntent.JunctionUsage = StringToEnum(
					JunctionUsageString,
					ELayoutSeamJunctionUsage::Both,
					Issues,
					TEXT("junction_usage"));
			}
			SeamIntent.InterfaceFamily = FGameplayTag::RequestGameplayTag(FName(*SeamIntentObject->GetStringField(TEXT("interface_family"))), false);
			SeamIntent.bCanOwnSeam = SeamIntentObject->GetBoolField(TEXT("can_own_seam"));
			SeamIntent.bCanAcceptSeam = SeamIntentObject->GetBoolField(TEXT("can_accept_seam"));
		}

		return SeamIntents;
	}

	TArray<TSharedPtr<FJsonValue>> TagsToJson(const FGameplayTagContainer& Tags)
	{
		TArray<FGameplayTag> TagArray;
		Tags.GetGameplayTagArray(TagArray);
		TagArray.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
		{
			return Left.ToString() < Right.ToString();
		});

		TArray<TSharedPtr<FJsonValue>> JsonTags;
		for (const FGameplayTag& Tag : TagArray)
		{
			JsonTags.Add(MakeShared<FJsonValueString>(Tag.ToString()));
		}
		return JsonTags;
	}

	FGameplayTagContainer JsonToTags(const TArray<TSharedPtr<FJsonValue>>* JsonTags, TArray<FString>& Issues, const FString& FieldPath)
	{
		FGameplayTagContainer Tags;
		if (JsonTags == nullptr)
		{
			return Tags;
		}

		for (const TSharedPtr<FJsonValue>& JsonTag : *JsonTags)
		{
			const FString TagString = JsonTag.IsValid() ? JsonTag->AsString() : FString();
			if (TagString.IsEmpty())
			{
				continue;
			}

			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagString), false);
			if (!Tag.IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Unknown gameplay tag '%s' in %s."), *TagString, *FieldPath));
				continue;
			}

			Tags.AddTag(Tag);
		}
		return Tags;
	}

	TSharedPtr<FJsonObject> FaceRuleToJson(const FLayoutFaceRule& Rule)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("direction"), EnumToString(Rule.Direction));
		Object->SetStringField(TEXT("connection_tag"), Rule.ConnectionTag.IsValid() ? Rule.ConnectionTag.ToString() : FString());
		Object->SetArrayField(TEXT("allowed_connection_tags"), TagsToJson(Rule.AllowedConnectionTags));
		Object->SetStringField(TEXT("occupancy_policy"), EnumToString(Rule.OccupancyPolicy));
		Object->SetArrayField(TEXT("connected_traversal_channels"), TagsToJson(Rule.ConnectedTraversalChannels));
		Object->SetStringField(TEXT("boundary_requirement"), EnumToString(Rule.BoundaryRequirement));
		Object->SetBoolField(TEXT("require_matching_yaw_with_filled_neighbor"), Rule.bRequireMatchingYawWithFilledNeighbor);
		return Object;
	}

	FLayoutFaceRule JsonToFaceRule(const TSharedPtr<FJsonObject>& Object, TArray<FString>& Issues, const FString& FieldPath)
	{
		FLayoutFaceRule Rule;
		if (!Object.IsValid())
		{
			Issues.Add(FString::Printf(TEXT("Invalid face rule object in %s."), *FieldPath));
			return Rule;
		}

		Rule.Direction = StringToEnum(Object->GetStringField(TEXT("direction")), ELayoutFaceDirection::PosX, Issues, TEXT("direction"));
		const FString ConnectionTagString = Object->GetStringField(TEXT("connection_tag"));
		const bool bHasConnectionTagString = !ConnectionTagString.IsEmpty() && !ConnectionTagString.Equals(TEXT("None"), ESearchCase::IgnoreCase);
		Rule.ConnectionTag = bHasConnectionTagString ? FGameplayTag::RequestGameplayTag(FName(*ConnectionTagString), false) : FGameplayTag();
		if (bHasConnectionTagString && !Rule.ConnectionTag.IsValid())
		{
			Issues.Add(FString::Printf(TEXT("Unknown gameplay tag '%s' in %s.connection_tag."), *ConnectionTagString, *FieldPath));
		}

		const TArray<TSharedPtr<FJsonValue>>* AllowedConnectionTags = nullptr;
		Object->TryGetArrayField(TEXT("allowed_connection_tags"), AllowedConnectionTags);
		Rule.AllowedConnectionTags = JsonToTags(AllowedConnectionTags, Issues, FieldPath + TEXT(".allowed_connection_tags"));
		Rule.OccupancyPolicy = StringToEnum(Object->GetStringField(TEXT("occupancy_policy")), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, Issues, TEXT("occupancy_policy"));

		const TArray<TSharedPtr<FJsonValue>>* ConnectedTraversalChannels = nullptr;
		if (!Object->TryGetArrayField(TEXT("connected_traversal_channels"), ConnectedTraversalChannels))
		{
			Object->TryGetArrayField(TEXT("connected_walkable_areas"), ConnectedTraversalChannels);
		}
		Rule.ConnectedTraversalChannels = JsonToTags(ConnectedTraversalChannels, Issues, FieldPath + TEXT(".connected_traversal_channels"));
		// Deserialize boundary_requirement: prefer string enum, fall back to legacy bool fields
		FString BoundaryRequirementStr;
		if (Object->TryGetStringField(TEXT("boundary_requirement"), BoundaryRequirementStr))
		{
			Rule.BoundaryRequirement = StringToEnum(BoundaryRequirementStr, ELayoutFaceBoundaryRequirement::MustFaceInterior, Issues, TEXT("boundary_requirement"));
		}
		else
		{
			bool bLegacyBoundary = false;
			if (Object->TryGetBoolField(TEXT("boundary_requirement"), bLegacyBoundary)
				|| Object->TryGetBoolField(TEXT("must_face_region_boundary"), bLegacyBoundary))
			{
				Rule.BoundaryRequirement = bLegacyBoundary
					? ELayoutFaceBoundaryRequirement::MustFaceExterior
					: ELayoutFaceBoundaryRequirement::MustFaceInterior;
			}
		}
		Rule.bRequireMatchingYawWithFilledNeighbor = Object->GetBoolField(TEXT("require_matching_yaw_with_filled_neighbor"));
		return Rule;
	}

	TSharedPtr<FJsonObject> InternalAccessLinkToJson(const FLayoutInternalAccessLink& Link)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("from_traversal_channel"), Link.FromTraversalChannel.ToString());
		Object->SetStringField(TEXT("to_traversal_channel"), Link.ToTraversalChannel.ToString());
		Object->SetBoolField(TEXT("bidirectional"), Link.bBidirectional);
		return Object;
	}

	FLayoutInternalAccessLink JsonToInternalAccessLink(const TSharedPtr<FJsonObject>& Object, TArray<FString>& Issues, const FString& FieldPath)
	{
		FLayoutInternalAccessLink Link;
		if (!Object.IsValid())
		{
			Issues.Add(FString::Printf(TEXT("Invalid internal access link object in %s."), *FieldPath));
			return Link;
		}

		FString FromTagString;
		if (!Object->TryGetStringField(TEXT("from_traversal_channel"), FromTagString))
		{
			Object->TryGetStringField(TEXT("from_walkable_area"), FromTagString);
		}
		FString ToTagString;
		if (!Object->TryGetStringField(TEXT("to_traversal_channel"), ToTagString))
		{
			Object->TryGetStringField(TEXT("to_walkable_area"), ToTagString);
		}
		Link.FromTraversalChannel = FGameplayTag::RequestGameplayTag(FName(*FromTagString), false);
		Link.ToTraversalChannel = FGameplayTag::RequestGameplayTag(FName(*ToTagString), false);
		Link.bBidirectional = Object->GetBoolField(TEXT("bidirectional"));
		if (!FromTagString.IsEmpty() && !Link.FromTraversalChannel.IsValid())
		{
			Issues.Add(FString::Printf(TEXT("Unknown gameplay tag '%s' in %s.from_traversal_channel."), *FromTagString, *FieldPath));
		}
		if (!ToTagString.IsEmpty() && !Link.ToTraversalChannel.IsValid())
		{
			Issues.Add(FString::Printf(TEXT("Unknown gameplay tag '%s' in %s.to_traversal_channel."), *ToTagString, *FieldPath));
		}
		return Link;
	}

	TArray<TSharedPtr<FJsonValue>> RolesToJson(const TArray<ELayoutModuleRole>& Roles)
	{
		TArray<TSharedPtr<FJsonValue>> JsonRoles;
		for (const ELayoutModuleRole Role : Roles)
		{
			JsonRoles.Add(MakeShared<FJsonValueString>(EnumToString(Role)));
		}
		return JsonRoles;
	}

	TArray<ELayoutModuleRole> JsonToRoles(const TArray<TSharedPtr<FJsonValue>>* JsonRoles, TArray<FString>& Issues)
	{
		TArray<ELayoutModuleRole> Roles;
		if (JsonRoles == nullptr)
		{
			return Roles;
		}

		for (const TSharedPtr<FJsonValue>& JsonRole : *JsonRoles)
		{
			Roles.AddUnique(StringToEnum(JsonRole.IsValid() ? JsonRole->AsString() : FString(), ELayoutModuleRole::Interior, Issues, TEXT("roles")));
		}
		return Roles;
	}

	TArray<TSharedPtr<FJsonValue>> IntsToJson(const TArray<int32>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> JsonValues;
		for (const int32 Value : Values)
		{
			JsonValues.Add(MakeShared<FJsonValueNumber>(Value));
		}
		return JsonValues;
	}

	TArray<int32> JsonToInts(const TArray<TSharedPtr<FJsonValue>>* JsonValues)
	{
		TArray<int32> Values;
		if (JsonValues == nullptr)
		{
			return Values;
		}

		for (const TSharedPtr<FJsonValue>& JsonValue : *JsonValues)
		{
			Values.Add(static_cast<int32>(JsonValue.IsValid() ? JsonValue->AsNumber() : 0.0));
		}
		return Values;
	}

	TSharedPtr<FJsonObject> SparsePlacementRuleToJson(const FInstancedStruct& Rule)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		const FLayoutSparsePlacementRuleBase* const BaseRule = Rule.GetPtr<FLayoutSparsePlacementRuleBase>();
		if (BaseRule == nullptr)
		{
			Object->SetStringField(TEXT("rule_type"), TEXT("unsupported"));
			return Object;
		}

		Object->SetStringField(TEXT("rule_id"), BaseRule->RuleId.ToString());
		Object->SetStringField(TEXT("placement_zone"), EnumToString(BaseRule->PlacementZone));
		Object->SetStringField(TEXT("level_placement_policy"), EnumToString(BaseRule->LevelPlacementPolicy));
		Object->SetNumberField(TEXT("specific_level"), BaseRule->SpecificLevel);
		if (Rule.GetPtr<FLayoutSparsePreserveTerrainRule>() != nullptr)
		{
			Object->SetStringField(TEXT("rule_type"), TEXT("preserve_terrain"));
			return Object;
		}

		const FLayoutSparseContentRuleBase* const ContentRule = Rule.GetPtr<FLayoutSparseContentRuleBase>();
		if (ContentRule == nullptr)
		{
			Object->SetStringField(TEXT("rule_type"), TEXT("unsupported"));
			return Object;
		}
		Object->SetStringField(TEXT("candidate_source"), EnumToString(ContentRule->CandidateSource));
		Object->SetStringField(TEXT("content_set_name"), ContentRule->ContentSet != nullptr ? ContentRule->ContentSet->GetName() : FString());
		Object->SetNumberField(TEXT("min_spacing_cells"), ContentRule->MinSpacingCells);
		if (const FLayoutSparseExactPlacementRule* ExactRule = Rule.GetPtr<FLayoutSparseExactPlacementRule>())
		{
			Object->SetStringField(TEXT("rule_type"), TEXT("exact"));
			Object->SetNumberField(TEXT("count"), ExactRule->Count);
		}
		else if (const FLayoutSparseRangePlacementRule* RangeRule = Rule.GetPtr<FLayoutSparseRangePlacementRule>())
		{
			Object->SetStringField(TEXT("rule_type"), TEXT("range"));
			Object->SetNumberField(TEXT("min_count"), RangeRule->MinCount);
			Object->SetNumberField(TEXT("max_count"), RangeRule->MaxCount);
		}
		else if (Rule.GetPtr<FLayoutSparseFillAvailablePlacementRule>() != nullptr)
		{
			Object->SetStringField(TEXT("rule_type"), TEXT("fill_available"));
		}
		else
		{
			Object->SetStringField(TEXT("rule_type"), TEXT("unsupported"));
		}
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> SparsePlacementRulesToJson(const TArray<FInstancedStruct>& Rules)
	{
		TArray<TSharedPtr<FJsonValue>> JsonRules;
		for (const FInstancedStruct& Rule : Rules)
		{
			JsonRules.Add(MakeShared<FJsonValueObject>(SparsePlacementRuleToJson(Rule)));
		}
		return JsonRules;
	}

	TArray<FImportedSparsePlacementRuleAuthoring> JsonToSparsePlacementRules(
		const TArray<TSharedPtr<FJsonValue>>* JsonRules,
		TArray<FString>& Issues,
		const FString& FieldPath)
	{
		TArray<FImportedSparsePlacementRuleAuthoring> Rules;
		if (JsonRules == nullptr)
		{
			return Rules;
		}

		for (int32 RuleIndex = 0; RuleIndex < JsonRules->Num(); ++RuleIndex)
		{
			const TSharedPtr<FJsonObject> RuleObject = (*JsonRules)[RuleIndex].IsValid() ? (*JsonRules)[RuleIndex]->AsObject() : nullptr;
			if (!RuleObject.IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Invalid sparse placement rule object at %s[%d]."), *FieldPath, RuleIndex));
				continue;
			}

			FImportedSparsePlacementRuleAuthoring ImportedRule;
			const FString RuleType = RuleObject->GetStringField(TEXT("rule_type"));
			if (RuleType == TEXT("preserve_terrain"))
			{
				ImportedRule.Rule.InitializeAs<FLayoutSparsePreserveTerrainRule>();
			}
			else if (RuleType == TEXT("exact"))
			{
				ImportedRule.Rule.InitializeAs<FLayoutSparseExactPlacementRule>();
			}
			else if (RuleType == TEXT("range"))
			{
				ImportedRule.Rule.InitializeAs<FLayoutSparseRangePlacementRule>();
			}
			else if (RuleType == TEXT("fill_available"))
			{
				ImportedRule.Rule.InitializeAs<FLayoutSparseFillAvailablePlacementRule>();
			}
			else
			{
				Issues.Add(FString::Printf(TEXT("Unsupported sparse placement rule type '%s' at %s[%d]."), *RuleType, *FieldPath, RuleIndex));
				continue;
			}

			FLayoutSparsePlacementRuleBase* const BaseRule = ImportedRule.Rule.GetMutablePtr<FLayoutSparsePlacementRuleBase>();
			BaseRule->RuleId = FName(*RuleObject->GetStringField(TEXT("rule_id")));
			BaseRule->PlacementZone = StringToEnum(RuleObject->GetStringField(TEXT("placement_zone")), ELayoutPlacementZone::Interior, Issues, TEXT("placement_zone"));
			BaseRule->LevelPlacementPolicy = StringToEnum(RuleObject->GetStringField(TEXT("level_placement_policy")), ELayoutLevelPlacementPolicy::AnyLevel, Issues, TEXT("level_placement_policy"));
			BaseRule->SpecificLevel = static_cast<int32>(RuleObject->GetIntegerField(TEXT("specific_level")));
			if (FLayoutSparseContentRuleBase* const ContentRule = ImportedRule.Rule.GetMutablePtr<FLayoutSparseContentRuleBase>())
			{
				ContentRule->CandidateSource = StringToEnum(RuleObject->GetStringField(TEXT("candidate_source")), ELayoutSparseCandidateSource::PreserveSupportedTerrain, Issues, TEXT("candidate_source"));
				ContentRule->MinSpacingCells = static_cast<int32>(RuleObject->GetIntegerField(TEXT("min_spacing_cells")));
				ImportedRule.ContentSetName = FName(*RuleObject->GetStringField(TEXT("content_set_name")));
			}
			if (FLayoutSparseExactPlacementRule* const ExactRule = ImportedRule.Rule.GetMutablePtr<FLayoutSparseExactPlacementRule>())
			{
				ExactRule->Count = static_cast<int32>(RuleObject->GetIntegerField(TEXT("count")));
			}
			if (FLayoutSparseRangePlacementRule* const RangeRule = ImportedRule.Rule.GetMutablePtr<FLayoutSparseRangePlacementRule>())
			{
				RangeRule->MinCount = static_cast<int32>(RuleObject->GetIntegerField(TEXT("min_count")));
				RangeRule->MaxCount = static_cast<int32>(RuleObject->GetIntegerField(TEXT("max_count")));
			}
			Rules.Add(MoveTemp(ImportedRule));
		}

		return Rules;
	}

	TSharedPtr<FJsonObject> ModuleAuthoringToJson(const ULayoutModuleAsset* Module)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		if (Module == nullptr)
		{
			return Object;
		}

		Object->SetStringField(TEXT("id"), Module->GetFName().ToString());
		Object->SetStringField(TEXT("name"), Module->GetName());
		Object->SetStringField(TEXT("template_name"), Module->Template.IsNull() ? FString() : Module->Template.GetAssetName());
		Object->SetObjectField(TEXT("template_dimensions_blocks"), VectorToJson(Module->GetEffectiveCellSizeInBlocks()));
		Object->SetArrayField(TEXT("roles"), RolesToJson(Module->Roles));
		Object->SetStringField(TEXT("face_symmetry_mode"), EnumToString(Module->FaceSymmetryMode));
		Object->SetNumberField(TEXT("min_walkable_faces"), Module->MinWalkableFaces);

		TArray<TSharedPtr<FJsonValue>> JsonFaceRules;
		for (const FLayoutFaceRule& Rule : Module->FaceRules.ToArray())
		{
			JsonFaceRules.Add(MakeShared<FJsonValueObject>(FaceRuleToJson(Rule)));
		}
		Object->SetArrayField(TEXT("face_rules"), JsonFaceRules);

		TArray<TSharedPtr<FJsonValue>> JsonInternalLinks;
		for (const FLayoutInternalAccessLink& Link : Module->InternalAccessLinks)
		{
			JsonInternalLinks.Add(MakeShared<FJsonValueObject>(InternalAccessLinkToJson(Link)));
		}
		Object->SetArrayField(TEXT("internal_access_links"), JsonInternalLinks);
		return Object;
	}

	TSharedPtr<FJsonObject> CompositeModuleAuthoringToJson(const ULayoutCompositeModuleAsset* Composite)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		if (Composite == nullptr)
		{
			return Object;
		}

		Object->SetStringField(TEXT("id"), Composite->GetFName().ToString());
		Object->SetStringField(TEXT("name"), Composite->GetName());
		TArray<TSharedPtr<FJsonValue>> JsonCells;
		TArray<FLayoutCompositeModuleCell> SortedCells = Composite->Cells;
		CanonicalizeFixtureCompositeCells(SortedCells);
		for (const FLayoutCompositeModuleCell& Cell : SortedCells)
		{
			TSharedPtr<FJsonObject> CellObject = MakeShared<FJsonObject>();
			CellObject->SetStringField(TEXT("module_id"), Cell.Module != nullptr ? Cell.Module->GetFName().ToString() : FString());
			CellObject->SetObjectField(TEXT("local_cell"), VectorToJson(Cell.LocalCell));
			CellObject->SetNumberField(TEXT("relative_yaw_rotation_steps"), Cell.RelativeYawRotationSteps);
			JsonCells.Add(MakeShared<FJsonValueObject>(CellObject));
		}
		Object->SetArrayField(TEXT("cells"), JsonCells);
		return Object;
	}

	ULayoutModuleAsset* RebuildModuleFromAuthoringObject(
		const TSharedPtr<FJsonObject>& ModuleObject,
		UObject* AssetOuter,
		const TMap<FString, FIntVector>& TemplateSizesByIdentity,
		const FIntVector& FallbackTemplateSize,
		TArray<FString>& OutIssues,
		const FString& FieldPath)
	{
		if (!ModuleObject.IsValid())
		{
			OutIssues.Add(FString::Printf(TEXT("Invalid module authoring object in %s."), *FieldPath));
			return nullptr;
		}

		const FString ModuleName = ModuleObject->GetStringField(TEXT("name"));
		ULayoutModuleAsset* Module = NewObject<ULayoutModuleAsset>(
			AssetOuter,
			MakeUniqueObjectName(AssetOuter, ULayoutModuleAsset::StaticClass(), FName(*(ModuleName.IsEmpty() ? TEXT("ImportedModule") : ModuleName))));

		const FString TemplateName = ModuleObject->GetStringField(TEXT("template_name"));
		UChunkStructureTemplate* Template = NewObject<UChunkStructureTemplate>(
			AssetOuter,
			MakeUniqueObjectName(AssetOuter, UChunkStructureTemplate::StaticClass(), FName(*(TemplateName.IsEmpty() ? Module->GetName() + TEXT("_Template") : TemplateName))));
		const FString ModuleId = ModuleObject->HasField(TEXT("id")) ? ModuleObject->GetStringField(TEXT("id")) : FString();
		FIntVector ResolvedTemplateSize = ModuleObject->HasField(TEXT("template_dimensions_blocks"))
			? JsonToVector(ModuleObject->GetObjectField(TEXT("template_dimensions_blocks")))
			: FIntVector::ZeroValue;
		const FIntVector* CompiledTemplateSize = !ModuleId.IsEmpty() ? TemplateSizesByIdentity.Find(ModuleId) : nullptr;
		if (CompiledTemplateSize == nullptr && !ModuleName.IsEmpty())
		{
			CompiledTemplateSize = TemplateSizesByIdentity.Find(ModuleName);
		}
		if (CompiledTemplateSize == nullptr && !TemplateName.IsEmpty())
		{
			CompiledTemplateSize = TemplateSizesByIdentity.Find(TemplateName);
		}
		if (!HasPositiveFixtureSharedCellSize(ResolvedTemplateSize))
		{
			ResolvedTemplateSize = CompiledTemplateSize != nullptr
				? *CompiledTemplateSize
				: FallbackTemplateSize;
		}
		Template->SizeInBlocks = ResolvedTemplateSize;
		Module->Template = Template;

		const TArray<TSharedPtr<FJsonValue>>* JsonRoles = nullptr;
		ModuleObject->TryGetArrayField(TEXT("roles"), JsonRoles);
		Module->Roles = JsonToRoles(JsonRoles, OutIssues);
		Module->FaceSymmetryMode = StringToEnum(ModuleObject->GetStringField(TEXT("face_symmetry_mode")), ELayoutFaceSymmetryMode::Independent, OutIssues, TEXT("face_symmetry_mode"));
		Module->MinWalkableFaces = static_cast<int32>(ModuleObject->GetIntegerField(TEXT("min_walkable_faces")));

		const TArray<TSharedPtr<FJsonValue>>* JsonFaceRules = nullptr;
		ModuleObject->TryGetArrayField(TEXT("face_rules"), JsonFaceRules);
		if (JsonFaceRules != nullptr)
		{
			for (int32 FaceIndex = 0; FaceIndex < JsonFaceRules->Num(); ++FaceIndex)
			{
				Module->FaceRules.SetRule(JsonToFaceRule(
					(*JsonFaceRules)[FaceIndex].IsValid() ? (*JsonFaceRules)[FaceIndex]->AsObject() : nullptr,
					OutIssues,
					FString::Printf(TEXT("%s.face_rules[%d]"), *FieldPath, FaceIndex)));
			}
		}
		Module->NormalizeFaceRuleDirections();

		const TArray<TSharedPtr<FJsonValue>>* JsonInternalLinks = nullptr;
		ModuleObject->TryGetArrayField(TEXT("internal_access_links"), JsonInternalLinks);
		if (JsonInternalLinks != nullptr)
		{
			for (int32 LinkIndex = 0; LinkIndex < JsonInternalLinks->Num(); ++LinkIndex)
			{
				Module->InternalAccessLinks.Add(JsonToInternalAccessLink(
					(*JsonInternalLinks)[LinkIndex].IsValid() ? (*JsonInternalLinks)[LinkIndex]->AsObject() : nullptr,
					OutIssues,
					FString::Printf(TEXT("%s.internal_access_links[%d]"), *FieldPath, LinkIndex)));
			}
		}

		return Module;
	}

	ULayoutCompositeModuleAsset* RebuildCompositeModuleFromAuthoringObject(
		const TSharedPtr<FJsonObject>& CompositeObject,
		UObject* AssetOuter,
		const TMap<FName, TObjectPtr<ULayoutModuleAsset>>& ModulesById,
		TArray<FString>& OutIssues,
		const FString& FieldPath)
	{
		if (!CompositeObject.IsValid())
		{
			OutIssues.Add(FString::Printf(TEXT("Invalid composite-module authoring object in %s."), *FieldPath));
			return nullptr;
		}

		const FString CompositeName = CompositeObject->GetStringField(TEXT("name"));
		ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(
			AssetOuter,
			MakeUniqueObjectName(AssetOuter, ULayoutCompositeModuleAsset::StaticClass(), FName(*(CompositeName.IsEmpty() ? TEXT("ImportedCompositeModule") : CompositeName))));

		const TArray<TSharedPtr<FJsonValue>>* JsonCells = nullptr;
		CompositeObject->TryGetArrayField(TEXT("cells"), JsonCells);
		if (JsonCells == nullptr)
		{
			return Composite;
		}

		for (int32 CellIndex = 0; CellIndex < JsonCells->Num(); ++CellIndex)
		{
			const TSharedPtr<FJsonObject> CellObject = (*JsonCells)[CellIndex].IsValid() ? (*JsonCells)[CellIndex]->AsObject() : nullptr;
			if (!CellObject.IsValid())
			{
				OutIssues.Add(FString::Printf(TEXT("Invalid composite-module cell object at %s.cells[%d]."), *FieldPath, CellIndex));
				continue;
			}

			FLayoutCompositeModuleCell& Cell = Composite->Cells.AddDefaulted_GetRef();
			const FName ModuleId(*CellObject->GetStringField(TEXT("module_id")));
			if (const TObjectPtr<ULayoutModuleAsset>* Module = ModulesById.Find(ModuleId))
			{
				Cell.Module = *Module;
			}
			else
			{
				OutIssues.Add(FString::Printf(TEXT("Composite module '%s' references unknown module id '%s'."), *Composite->GetName(), *ModuleId.ToString()));
			}

			Cell.LocalCell = JsonToVector(CellObject->GetObjectField(TEXT("local_cell")));
			Cell.RelativeYawRotationSteps = static_cast<int32>(CellObject->GetIntegerField(TEXT("relative_yaw_rotation_steps")));
		}

		CanonicalizeFixtureCompositeCells(Composite->Cells);

		return Composite;
	}

	TSharedPtr<FJsonObject> ContentSetAuthoringToJson(const ULayoutRegionContentSetAsset* ContentSet)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		if (ContentSet == nullptr)
		{
			return Object;
		}

		Object->SetStringField(TEXT("id"), ContentSet->GetFName().ToString());
		Object->SetStringField(TEXT("name"), ContentSet->GetName());
		TArray<TSharedPtr<FJsonValue>> JsonEntries;
		for (const FLayoutRegionContentEntry& Entry : ContentSet->Entries)
		{
			TSharedPtr<FJsonObject> EntryObject = MakeShared<FJsonObject>();
			EntryObject->SetStringField(TEXT("entry_id"), Entry.EntryId.ToString());
			EntryObject->SetStringField(TEXT("content_kind"), EnumToString(Entry.ContentKind));
			EntryObject->SetNumberField(TEXT("weight"), Entry.Weight);
			EntryObject->SetArrayField(TEXT("provided_zone_features"), TagsToJson(Entry.ProvidedZoneFeatures));
			EntryObject->SetArrayField(TEXT("closure_provider_intents"), ClosureProviderIntentsToJson(Entry.ClosureProviderIntents));
			EntryObject->SetArrayField(TEXT("seam_provider_intents"), SeamProviderIntentsToJson(Entry.SeamProviderIntents));
			if (Entry.ContentKind == ELayoutRegionContentKind::Module)
			{
				if (Entry.ModuleSettings.Module != nullptr)
				{
					EntryObject->SetStringField(TEXT("module_id"), Entry.ModuleSettings.Module->GetFName().ToString());
				}

				if (Entry.ModuleSettings.CompositeModule != nullptr)
				{
					EntryObject->SetStringField(TEXT("composite_module_id"), Entry.ModuleSettings.CompositeModule->GetFName().ToString());
				}

				EntryObject->SetStringField(TEXT("module_placement_zone"), EnumToString(Entry.ModuleSettings.PlacementZone));
				EntryObject->SetStringField(TEXT("module_level_placement_policy"), EnumToString(Entry.ModuleSettings.LevelPlacementPolicy));
				EntryObject->SetNumberField(TEXT("module_specific_level"), Entry.ModuleSettings.SpecificLevel);
				EntryObject->SetBoolField(TEXT("module_optional"), Entry.ModuleSettings.bOptional);
			}
			else
			{
				EntryObject->SetStringField(TEXT("child_profile_id"), Entry.ChildRegionSettings.RegionProfile != nullptr ? Entry.ChildRegionSettings.RegionProfile->GetFName().ToString() : FString());
				EntryObject->SetStringField(TEXT("child_placement_zone"), EnumToString(Entry.ChildRegionSettings.PlacementZone));
				EntryObject->SetStringField(TEXT("child_level_placement_policy"), EnumToString(Entry.ChildRegionSettings.LevelPlacementPolicy));
				EntryObject->SetNumberField(TEXT("child_specific_level"), Entry.ChildRegionSettings.SpecificLevel);
				EntryObject->SetBoolField(TEXT("child_optional"), Entry.ChildRegionSettings.bOptional);
				EntryObject->SetBoolField(TEXT("child_contributes_host_vertical_access"), Entry.ChildRegionSettings.bContributesHostVerticalAccess);
			}
			JsonEntries.Add(MakeShared<FJsonValueObject>(EntryObject));
		}
		Object->SetArrayField(TEXT("entries"), JsonEntries);
		return Object;
	}

	TSharedPtr<FJsonObject> LevelFillRuleToJson(const FLayoutLevelFillRule& Rule)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("rule_id"), Rule.RuleId.ToString());
		Object->SetStringField(TEXT("level_placement_policy"), EnumToString(Rule.LevelPlacementPolicy));
		Object->SetNumberField(TEXT("specific_level"), Rule.SpecificLevel);
		Object->SetStringField(TEXT("fill_mode"), EnumToString(Rule.FillMode));
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> LevelFillRulesToJson(const TArray<FLayoutLevelFillRule>& Rules)
	{
		TArray<TSharedPtr<FJsonValue>> JsonRules;
		for (const FLayoutLevelFillRule& Rule : Rules)
		{
			JsonRules.Add(MakeShared<FJsonValueObject>(LevelFillRuleToJson(Rule)));
		}
		return JsonRules;
	}

	TArray<FLayoutLevelFillRule> JsonToLevelFillRules(const TArray<TSharedPtr<FJsonValue>>* JsonRules, TArray<FString>& Issues, const FString& FieldPath)
	{
		TArray<FLayoutLevelFillRule> Rules;
		if (JsonRules == nullptr)
		{
			return Rules;
		}

		for (int32 RuleIndex = 0; RuleIndex < JsonRules->Num(); ++RuleIndex)
		{
			const TSharedPtr<FJsonObject> RuleObject = (*JsonRules)[RuleIndex].IsValid() ? (*JsonRules)[RuleIndex]->AsObject() : nullptr;
			if (!RuleObject.IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Invalid level fill rule object at %s[%d]."), *FieldPath, RuleIndex));
				continue;
			}

			FLayoutLevelFillRule& Rule = Rules.AddDefaulted_GetRef();
			Rule.RuleId = FName(*RuleObject->GetStringField(TEXT("rule_id")));
			Rule.LevelPlacementPolicy = StringToEnum(RuleObject->GetStringField(TEXT("level_placement_policy")), ELayoutLevelPlacementPolicy::AnyLevel, Issues, TEXT("level_placement_policy"));
			Rule.SpecificLevel = static_cast<int32>(RuleObject->GetIntegerField(TEXT("specific_level")));
			Rule.FillMode = StringToEnum(RuleObject->GetStringField(TEXT("fill_mode")), ELayoutLevelFillMode::Default, Issues, TEXT("fill_mode"));
		}

		return Rules;
	}

	TSharedPtr<FJsonObject> ReservedOpenSpaceRuleToJson(const FLayoutReservedOpenSpaceRule& Rule)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("rule_id"), Rule.RuleId.ToString());
		Object->SetStringField(TEXT("placement_zone"), EnumToString(Rule.PlacementZone));
		Object->SetStringField(TEXT("level_placement_policy"), EnumToString(Rule.LevelPlacementPolicy));
		Object->SetNumberField(TEXT("specific_level"), Rule.SpecificLevel);
		Object->SetNumberField(TEXT("reserved_percent"), Rule.ReservedPercent);
		Object->SetNumberField(TEXT("min_reserved_cells"), Rule.MinReservedCells);
		Object->SetNumberField(TEXT("max_reserved_cells"), Rule.MaxReservedCells);
		Object->SetStringField(TEXT("terrain_behavior"), EnumToString(Rule.TerrainBehavior));
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> ReservedOpenSpaceRulesToJson(const TArray<FLayoutReservedOpenSpaceRule>& Rules)
	{
		TArray<TSharedPtr<FJsonValue>> JsonRules;
		for (const FLayoutReservedOpenSpaceRule& Rule : Rules)
		{
			JsonRules.Add(MakeShared<FJsonValueObject>(ReservedOpenSpaceRuleToJson(Rule)));
		}
		return JsonRules;
	}

	TArray<FLayoutReservedOpenSpaceRule> JsonToReservedOpenSpaceRules(const TArray<TSharedPtr<FJsonValue>>* JsonRules, TArray<FString>& Issues, const FString& FieldPath)
	{
		TArray<FLayoutReservedOpenSpaceRule> Rules;
		if (JsonRules == nullptr)
		{
			return Rules;
		}

		for (int32 RuleIndex = 0; RuleIndex < JsonRules->Num(); ++RuleIndex)
		{
			const TSharedPtr<FJsonObject> RuleObject = (*JsonRules)[RuleIndex].IsValid() ? (*JsonRules)[RuleIndex]->AsObject() : nullptr;
			if (!RuleObject.IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Invalid reserved open-space rule object at %s[%d]."), *FieldPath, RuleIndex));
				continue;
			}

			FLayoutReservedOpenSpaceRule& Rule = Rules.AddDefaulted_GetRef();
			Rule.RuleId = FName(*RuleObject->GetStringField(TEXT("rule_id")));
			Rule.PlacementZone = StringToEnum(RuleObject->GetStringField(TEXT("placement_zone")), ELayoutPlacementZone::Interior, Issues, TEXT("placement_zone"));
			Rule.LevelPlacementPolicy = StringToEnum(RuleObject->GetStringField(TEXT("level_placement_policy")), ELayoutLevelPlacementPolicy::AnyLevel, Issues, TEXT("level_placement_policy"));
			Rule.SpecificLevel = static_cast<int32>(RuleObject->GetIntegerField(TEXT("specific_level")));
			Rule.ReservedPercent = static_cast<float>(RuleObject->GetNumberField(TEXT("reserved_percent")));
			Rule.MinReservedCells = static_cast<int32>(RuleObject->GetIntegerField(TEXT("min_reserved_cells")));
			Rule.MaxReservedCells = static_cast<int32>(RuleObject->GetIntegerField(TEXT("max_reserved_cells")));
		FString TerrainBehavior;
		if (RuleObject->TryGetStringField(TEXT("terrain_behavior"), TerrainBehavior))
		{
			Rule.TerrainBehavior = StringToEnum(
				TerrainBehavior,
				ELayoutReservedOpenTerrainBehavior::LeaveExistingTerrain,
				Issues,
				TEXT("terrain_behavior"));
		}
		}

		return Rules;
	}


	TSharedPtr<FJsonObject> ProfileAuthoringToJson(const ULayoutProfileAsset* Profile)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		if (Profile == nullptr)
		{
			return Object;
		}

		Object->SetStringField(TEXT("id"), Profile->GetFName().ToString());
		Object->SetStringField(TEXT("name"), Profile->GetName());
		Object->SetObjectField(TEXT("minimum_footprint_in_cells"), PointToJson(Profile->MinimumFootprintInCells));
		Object->SetObjectField(TEXT("maximum_footprint_in_cells"), PointToJson(Profile->MaximumFootprintInCells));
		Object->SetNumberField(TEXT("level_count"), Profile->LevelCount);
		Object->SetStringField(TEXT("entry_count_mode"), EnumToString(Profile->EntryCountMode));
		Object->SetNumberField(TEXT("entry_count"), Profile->EntryCount);
		Object->SetNumberField(TEXT("min_entry_count"), Profile->MinEntryCount);
		Object->SetNumberField(TEXT("max_entry_count"), Profile->MaxEntryCount);
		Object->SetStringField(TEXT("vertical_access_count_mode"), EnumToString(Profile->VerticalAccessCountMode));
		Object->SetNumberField(TEXT("vertical_access_count"), Profile->VerticalAccessCount);
		Object->SetNumberField(TEXT("min_vertical_access_count"), Profile->MinVerticalAccessCount);
		Object->SetNumberField(TEXT("max_vertical_access_count"), Profile->MaxVerticalAccessCount);
		Object->SetBoolField(TEXT("restrict_vertical_access_modules_to_vertical_access_cells"), Profile->bRestrictVerticalAccessModulesToVerticalAccessCells);
		Object->SetStringField(TEXT("content_set_id"), Profile->ContentSet != nullptr ? Profile->ContentSet->GetFName().ToString() : FString());
		Object->SetArrayField(TEXT("closure_requirements"), ClosureRequirementsToJson(Profile->ClosureRequirements));
		Object->SetArrayField(TEXT("zone_feature_requirements"), ZoneFeatureRequirementsToJson(Profile->ZoneFeatureRequirements));
		Object->SetArrayField(TEXT("level_fill_rules"), LevelFillRulesToJson(Profile->LevelFillRules));
		Object->SetArrayField(TEXT("reserved_open_space_rules"), ReservedOpenSpaceRulesToJson(Profile->ReservedOpenSpaceRules));
		Object->SetArrayField(TEXT("sparse_placement_rules"), SparsePlacementRulesToJson(Profile->SparsePlacementRules));
		Object->SetBoolField(TEXT("require_all_traversal_channels_reachable"), Profile->bRequireAllTraversalChannelsReachable);
		Object->SetBoolField(TEXT("supports_stepped_terrain_solve"), Profile->bSupportsSteppedTerrainSolve);
		Object->SetBoolField(TEXT("enable_terrain_seams"), Profile->bEnableTerrainSeams);
		return Object;
	}

	void CollectRecursiveFixtureAssets(
		const ULayoutProfileAsset* Profile,
		const ULayoutRegionContentSetAsset* ContentSet,
		TMap<FName, const ULayoutProfileAsset*>& OutProfiles,
		TMap<FName, const ULayoutRegionContentSetAsset*>& OutContentSets,
		TMap<FName, const ULayoutModuleAsset*>& OutModules,
		TMap<FName, const ULayoutCompositeModuleAsset*>& OutComposites)
	{
		if (Profile != nullptr)
		{
			if (!OutProfiles.Contains(Profile->GetFName()))
			{
				OutProfiles.Add(Profile->GetFName(), Profile);
				if (Profile->ContentSet != nullptr)
				{
					CollectRecursiveFixtureAssets(Profile, Profile->ContentSet, OutProfiles, OutContentSets, OutModules, OutComposites);
				}
				for (const FInstancedStruct& Rule : Profile->SparsePlacementRules)
				{
					const FLayoutSparseContentRuleBase* const ContentRule =
						Rule.GetPtr<FLayoutSparseContentRuleBase>();
					if (ContentRule != nullptr && ContentRule->ContentSet != nullptr)
					{
						CollectRecursiveFixtureAssets(nullptr, ContentRule->ContentSet, OutProfiles, OutContentSets, OutModules, OutComposites);
					}
				}
			}
		}

		if (ContentSet == nullptr || OutContentSets.Contains(ContentSet->GetFName()))
		{
			return;
		}

		OutContentSets.Add(ContentSet->GetFName(), ContentSet);
		for (const FLayoutRegionContentEntry& Entry : ContentSet->Entries)
		{
			if (Entry.ContentKind == ELayoutRegionContentKind::Module)
			{
				if (Entry.ModuleSettings.Module != nullptr)
				{
					OutModules.Add(Entry.ModuleSettings.Module->GetFName(), Entry.ModuleSettings.Module);
				}
				if (Entry.ModuleSettings.CompositeModule != nullptr)
				{
					OutComposites.Add(Entry.ModuleSettings.CompositeModule->GetFName(), Entry.ModuleSettings.CompositeModule);
					for (const FLayoutCompositeModuleCell& Cell : Entry.ModuleSettings.CompositeModule->Cells)
					{
						if (Cell.Module != nullptr)
						{
							OutModules.Add(Cell.Module->GetFName(), Cell.Module);
						}
					}
				}
			}
			else if (Entry.ChildRegionSettings.RegionProfile != nullptr)
			{
				CollectRecursiveFixtureAssets(
					Entry.ChildRegionSettings.RegionProfile,
					Entry.ChildRegionSettings.RegionProfile->ContentSet,
					OutProfiles,
					OutContentSets,
					OutModules,
					OutComposites);
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> SupportedIntentsToJson(const TArray<ELayoutCellIntent>& Intents)
	{
		TArray<TSharedPtr<FJsonValue>> JsonIntents;
		for (const ELayoutCellIntent Intent : Intents)
		{
			JsonIntents.Add(MakeShared<FJsonValueString>(EnumToString(Intent)));
		}
		return JsonIntents;
	}

	TArray<ELayoutCellIntent> JsonToSupportedIntents(const TArray<TSharedPtr<FJsonValue>>* JsonIntents, TArray<FString>& Issues)
	{
		TArray<ELayoutCellIntent> Intents;
		if (JsonIntents == nullptr)
		{
			return Intents;
		}

		for (const TSharedPtr<FJsonValue>& JsonIntent : *JsonIntents)
		{
			Intents.AddUnique(StringToEnum(
				JsonIntent.IsValid() ? JsonIntent->AsString() : FString(),
				ELayoutCellIntent::Interior,
				Issues,
				TEXT("supported_intents")));
		}
		return Intents;
	}

	TSharedPtr<FJsonObject> ValidationMessageToJson(const FLayoutValidationMessage& Message)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("severity"), EnumToString(Message.Severity));
		Object->SetStringField(TEXT("message"), Message.Message);
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> ValidationMessagesToJson(const TArray<FLayoutValidationMessage>& Messages)
	{
		TArray<TSharedPtr<FJsonValue>> JsonMessages;
		for (const FLayoutValidationMessage& Message : Messages)
		{
			JsonMessages.Add(MakeShared<FJsonValueObject>(ValidationMessageToJson(Message)));
		}
		return JsonMessages;
	}

	TArray<FLayoutValidationMessage> JsonToValidationMessages(const TArray<TSharedPtr<FJsonValue>>* JsonMessages, TArray<FString>& Issues, const FString& FieldPath)
	{
		TArray<FLayoutValidationMessage> Messages;
		if (JsonMessages == nullptr)
		{
			return Messages;
		}

		for (int32 MessageIndex = 0; MessageIndex < JsonMessages->Num(); ++MessageIndex)
		{
			const TSharedPtr<FJsonObject> MessageObject = (*JsonMessages)[MessageIndex].IsValid() ? (*JsonMessages)[MessageIndex]->AsObject() : nullptr;
			if (!MessageObject.IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Invalid validation message object at %s[%d]."), *FieldPath, MessageIndex));
				continue;
			}

			FLayoutValidationMessage& Message = Messages.AddDefaulted_GetRef();
			Message.Severity = StringToEnum(
				MessageObject->GetStringField(TEXT("severity")),
				ELayoutValidationSeverity::Error,
				Issues,
				TEXT("severity"));
			Message.Message = MessageObject->GetStringField(TEXT("message"));
		}

		return Messages;
	}

	TSharedPtr<FJsonObject> ProofRecordToJson(const FLayoutProofRecord& ProofRecord)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("proof_id"), ProofRecord.ProofId.ToString());
		Object->SetStringField(TEXT("proof_kind"), EnumToString(ProofRecord.ProofKind));
		Object->SetStringField(TEXT("target_id"), ProofRecord.TargetId.ToString());
		TArray<TSharedPtr<FJsonValue>> JsonSourceIds;
		for (const FLayoutId& SourceId : ProofRecord.SourceIds)
		{
			JsonSourceIds.Add(MakeShared<FJsonValueString>(SourceId.ToString()));
		}
		Object->SetArrayField(TEXT("source_ids"), JsonSourceIds);
		Object->SetStringField(TEXT("proof_summary"), ProofRecord.ProofSummary);
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> ProofRecordsToJson(const TArray<FLayoutProofRecord>& ProofRecords)
	{
		TArray<TSharedPtr<FJsonValue>> JsonProofRecords;
		for (const FLayoutProofRecord& ProofRecord : ProofRecords)
		{
			JsonProofRecords.Add(MakeShared<FJsonValueObject>(ProofRecordToJson(ProofRecord)));
		}
		return JsonProofRecords;
	}

	TArray<FLayoutProofRecord> JsonToProofRecords(const TArray<TSharedPtr<FJsonValue>>* JsonProofRecords, TArray<FString>& Issues, const FString& FieldPath)
	{
		TArray<FLayoutProofRecord> ProofRecords;
		if (JsonProofRecords == nullptr)
		{
			return ProofRecords;
		}

		for (int32 ProofIndex = 0; ProofIndex < JsonProofRecords->Num(); ++ProofIndex)
		{
			const TSharedPtr<FJsonObject> ProofObject = (*JsonProofRecords)[ProofIndex].IsValid() ? (*JsonProofRecords)[ProofIndex]->AsObject() : nullptr;
			if (!ProofObject.IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Invalid proof record object at %s[%d]."), *FieldPath, ProofIndex));
				continue;
			}

			FLayoutProofRecord& ProofRecord = ProofRecords.AddDefaulted_GetRef();
			ProofRecord.ProofId = FLayoutId(*ProofObject->GetStringField(TEXT("proof_id")));
			ProofRecord.ProofKind = StringToEnum(
				ProofObject->GetStringField(TEXT("proof_kind")),
				ELayoutProofKind::SnapshotCopy,
				Issues,
				TEXT("proof_kind"));
			ProofRecord.TargetId = FLayoutId(*ProofObject->GetStringField(TEXT("target_id")));
			const TArray<TSharedPtr<FJsonValue>>* JsonSourceIds = nullptr;
			ProofObject->TryGetArrayField(TEXT("source_ids"), JsonSourceIds);
			if (JsonSourceIds != nullptr)
			{
				for (const TSharedPtr<FJsonValue>& JsonSourceId : *JsonSourceIds)
				{
					ProofRecord.SourceIds.Add(FLayoutId(*JsonSourceId->AsString()));
				}
			}
			ProofRecord.ProofSummary = ProofObject->GetStringField(TEXT("proof_summary"));
		}

		return ProofRecords;
	}

	TSharedPtr<FJsonObject> ValidationAssertionToJson(const FLayoutValidationAssertionRecord& Assertion)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("assertion_id"), Assertion.AssertionId.ToString());
		Object->SetStringField(TEXT("assertion_kind"), EnumToString(Assertion.AssertionKind));
		Object->SetBoolField(TEXT("passed"), Assertion.bPassed);
		TArray<TSharedPtr<FJsonValue>> JsonRelatedIds;
		for (const FLayoutId& RelatedId : Assertion.RelatedIds)
		{
			JsonRelatedIds.Add(MakeShared<FJsonValueString>(RelatedId.ToString()));
		}
		Object->SetArrayField(TEXT("related_ids"), JsonRelatedIds);
		Object->SetStringField(TEXT("failure_reason"), Assertion.FailureReason);
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> ValidationAssertionsToJson(const TArray<FLayoutValidationAssertionRecord>& Assertions)
	{
		TArray<TSharedPtr<FJsonValue>> JsonAssertions;
		for (const FLayoutValidationAssertionRecord& Assertion : Assertions)
		{
			JsonAssertions.Add(MakeShared<FJsonValueObject>(ValidationAssertionToJson(Assertion)));
		}
		return JsonAssertions;
	}

	TArray<FLayoutValidationAssertionRecord> JsonToValidationAssertions(const TArray<TSharedPtr<FJsonValue>>* JsonAssertions, TArray<FString>& Issues, const FString& FieldPath)
	{
		TArray<FLayoutValidationAssertionRecord> Assertions;
		if (JsonAssertions == nullptr)
		{
			return Assertions;
		}

		for (int32 AssertionIndex = 0; AssertionIndex < JsonAssertions->Num(); ++AssertionIndex)
		{
			const TSharedPtr<FJsonObject> AssertionObject = (*JsonAssertions)[AssertionIndex].IsValid() ? (*JsonAssertions)[AssertionIndex]->AsObject() : nullptr;
			if (!AssertionObject.IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Invalid validation assertion object at %s[%d]."), *FieldPath, AssertionIndex));
				continue;
			}

			FLayoutValidationAssertionRecord& Assertion = Assertions.AddDefaulted_GetRef();
			Assertion.AssertionId = FLayoutId(*AssertionObject->GetStringField(TEXT("assertion_id")));
			Assertion.AssertionKind = StringToEnum(
				AssertionObject->GetStringField(TEXT("assertion_kind")),
				ELayoutValidationAssertionKind::AssetValidationPassed,
				Issues,
				TEXT("assertion_kind"));
			Assertion.bPassed = AssertionObject->GetBoolField(TEXT("passed"));
			const TArray<TSharedPtr<FJsonValue>>* JsonRelatedIds = nullptr;
			AssertionObject->TryGetArrayField(TEXT("related_ids"), JsonRelatedIds);
			if (JsonRelatedIds != nullptr)
			{
				for (const TSharedPtr<FJsonValue>& JsonRelatedId : *JsonRelatedIds)
				{
					Assertion.RelatedIds.Add(FLayoutId(*JsonRelatedId->AsString()));
				}
			}
			Assertion.FailureReason = AssertionObject->GetStringField(TEXT("failure_reason"));
		}

		return Assertions;
	}

	TSharedPtr<FJsonObject> DerivedEndpointOfferToJson(const FLayoutDerivedEndpointOffer& Offer)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("offer_id"), Offer.OfferId.ToString());
		Object->SetObjectField(TEXT("local_cell"), VectorToJson(Offer.LocalCell));
		Object->SetStringField(TEXT("face_direction"), EnumToString(Offer.FaceDirection));
		Object->SetStringField(TEXT("connection_tag"), Offer.ConnectionTag.ToString());
		Object->SetArrayField(TEXT("allowed_connection_tags"), TagsToJson(Offer.AllowedConnectionTags));
		Object->SetArrayField(TEXT("traversal_channels"), TagsToJson(Offer.TraversalChannels));
		Object->SetArrayField(TEXT("roles"), RolesToJson(Offer.Roles));
		Object->SetStringField(TEXT("occupancy_policy"), EnumToString(Offer.OccupancyPolicy));
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> DerivedEndpointOffersToJson(const TArray<FLayoutDerivedEndpointOffer>& Offers)
	{
		TArray<TSharedPtr<FJsonValue>> JsonOffers;
		for (const FLayoutDerivedEndpointOffer& Offer : Offers)
		{
			JsonOffers.Add(MakeShared<FJsonValueObject>(DerivedEndpointOfferToJson(Offer)));
		}
		return JsonOffers;
	}

	TArray<FLayoutDerivedEndpointOffer> JsonToDerivedEndpointOffers(const TArray<TSharedPtr<FJsonValue>>* JsonOffers, TArray<FString>& Issues, const FString& FieldPath)
	{
		TArray<FLayoutDerivedEndpointOffer> Offers;
		if (JsonOffers == nullptr)
		{
			return Offers;
		}

		for (int32 OfferIndex = 0; OfferIndex < JsonOffers->Num(); ++OfferIndex)
		{
			const TSharedPtr<FJsonObject> OfferObject = (*JsonOffers)[OfferIndex].IsValid() ? (*JsonOffers)[OfferIndex]->AsObject() : nullptr;
			if (!OfferObject.IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Invalid derived endpoint offer object at %s[%d]."), *FieldPath, OfferIndex));
				continue;
			}

			FLayoutDerivedEndpointOffer& Offer = Offers.AddDefaulted_GetRef();
			Offer.OfferId = FLayoutId(*OfferObject->GetStringField(TEXT("offer_id")));
			Offer.LocalCell = JsonToVector(OfferObject->GetObjectField(TEXT("local_cell")));
			Offer.FaceDirection = StringToEnum(OfferObject->GetStringField(TEXT("face_direction")), ELayoutFaceDirection::PosX, Issues, TEXT("face_direction"));
			const FString ConnectionTagString = OfferObject->GetStringField(TEXT("connection_tag"));
			Offer.ConnectionTag = ConnectionTagString.IsEmpty() ? FGameplayTag() : FGameplayTag::RequestGameplayTag(FName(*ConnectionTagString), false);
			const TArray<TSharedPtr<FJsonValue>>* JsonAllowedTags = nullptr;
			OfferObject->TryGetArrayField(TEXT("allowed_connection_tags"), JsonAllowedTags);
			Offer.AllowedConnectionTags = JsonToTags(JsonAllowedTags, Issues, FieldPath + TEXT(".allowed_connection_tags"));
			const TArray<TSharedPtr<FJsonValue>>* JsonTraversalChannels = nullptr;
			OfferObject->TryGetArrayField(TEXT("traversal_channels"), JsonTraversalChannels);
			Offer.TraversalChannels = JsonToTags(JsonTraversalChannels, Issues, FieldPath + TEXT(".traversal_channels"));
			const TArray<TSharedPtr<FJsonValue>>* JsonRoles = nullptr;
			OfferObject->TryGetArrayField(TEXT("roles"), JsonRoles);
			Offer.Roles = JsonToRoles(JsonRoles, Issues);
			Offer.OccupancyPolicy = StringToEnum(OfferObject->GetStringField(TEXT("occupancy_policy")), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, Issues, TEXT("occupancy_policy"));
		}

		return Offers;
	}

	TSharedPtr<FJsonObject> DerivedSpanOfferToJson(const FLayoutDerivedSpanOffer& Offer)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("span_offer_id"), Offer.SpanOfferId.ToString());
		Object->SetStringField(TEXT("closure_id"), Offer.ClosureId.ToString());
		Object->SetObjectField(TEXT("local_cell"), VectorToJson(Offer.LocalCell));
		Object->SetStringField(TEXT("face_direction"), EnumToString(Offer.FaceDirection));
		Object->SetStringField(TEXT("connection_tag"), Offer.ConnectionTag.ToString());
		Object->SetArrayField(TEXT("allowed_connection_tags"), TagsToJson(Offer.AllowedConnectionTags));
		Object->SetArrayField(TEXT("roles"), RolesToJson(Offer.Roles));
		Object->SetNumberField(TEXT("thickness_cells"), Offer.ThicknessCells);
		Object->SetBoolField(TEXT("seals_boundary"), Offer.bSealsBoundary);
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> DerivedSpanOffersToJson(const TArray<FLayoutDerivedSpanOffer>& Offers)
	{
		TArray<TSharedPtr<FJsonValue>> JsonOffers;
		for (const FLayoutDerivedSpanOffer& Offer : Offers)
		{
			JsonOffers.Add(MakeShared<FJsonValueObject>(DerivedSpanOfferToJson(Offer)));
		}
		return JsonOffers;
	}

	TArray<FLayoutDerivedSpanOffer> JsonToDerivedSpanOffers(const TArray<TSharedPtr<FJsonValue>>* JsonOffers, TArray<FString>& Issues, const FString& FieldPath)
	{
		TArray<FLayoutDerivedSpanOffer> Offers;
		if (JsonOffers == nullptr)
		{
			return Offers;
		}

		for (int32 OfferIndex = 0; OfferIndex < JsonOffers->Num(); ++OfferIndex)
		{
			const TSharedPtr<FJsonObject> OfferObject = (*JsonOffers)[OfferIndex].IsValid() ? (*JsonOffers)[OfferIndex]->AsObject() : nullptr;
			if (!OfferObject.IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Invalid derived span offer object at %s[%d]."), *FieldPath, OfferIndex));
				continue;
			}

			FLayoutDerivedSpanOffer& Offer = Offers.AddDefaulted_GetRef();
			Offer.SpanOfferId = FLayoutId(*OfferObject->GetStringField(TEXT("span_offer_id")));
			FString ClosureIdString;
			if (OfferObject->TryGetStringField(TEXT("closure_id"), ClosureIdString) && !ClosureIdString.IsEmpty())
			{
				Offer.ClosureId = FName(*ClosureIdString);
			}
			Offer.LocalCell = JsonToVector(OfferObject->GetObjectField(TEXT("local_cell")));
			Offer.FaceDirection = StringToEnum(OfferObject->GetStringField(TEXT("face_direction")), ELayoutFaceDirection::PosX, Issues, TEXT("face_direction"));
			const FString ConnectionTagString = OfferObject->GetStringField(TEXT("connection_tag"));
			Offer.ConnectionTag = ConnectionTagString.IsEmpty() ? FGameplayTag() : FGameplayTag::RequestGameplayTag(FName(*ConnectionTagString), false);
			const TArray<TSharedPtr<FJsonValue>>* JsonAllowedTags = nullptr;
			OfferObject->TryGetArrayField(TEXT("allowed_connection_tags"), JsonAllowedTags);
			Offer.AllowedConnectionTags = JsonToTags(JsonAllowedTags, Issues, FieldPath + TEXT(".allowed_connection_tags"));
			const TArray<TSharedPtr<FJsonValue>>* JsonRoles = nullptr;
			OfferObject->TryGetArrayField(TEXT("roles"), JsonRoles);
			Offer.Roles = JsonToRoles(JsonRoles, Issues);
			double ThicknessCells = 1.0;
			OfferObject->TryGetNumberField(TEXT("thickness_cells"), ThicknessCells);
			Offer.ThicknessCells = FMath::Max(1, FMath::RoundToInt(ThicknessCells));
			bool bSealsBoundary = true;
			if (OfferObject->TryGetBoolField(TEXT("seals_boundary"), bSealsBoundary))
			{
				Offer.bSealsBoundary = bSealsBoundary;
			}
			else
			{
				Offer.bSealsBoundary = OfferObject->GetBoolField(TEXT("seals_exterior"));
			}
		}

		return Offers;
	}

	TSharedPtr<FJsonObject> DerivedVerticalAccessContractToJson(const FLayoutDerivedVerticalAccessContract& Contract)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("contract_id"), Contract.ContractId.ToString());
		Object->SetObjectField(TEXT("local_cell"), VectorToJson(Contract.LocalCell));
		Object->SetArrayField(TEXT("source_traversal_channels"), TagsToJson(Contract.SourceTraversalChannels));
		Object->SetArrayField(TEXT("exit_traversal_channels"), TagsToJson(Contract.ExitTraversalChannels));
		Object->SetStringField(TEXT("exit_face_direction"), EnumToString(Contract.ExitFaceDirection));
		Object->SetBoolField(TEXT("require_matching_yaw_at_exit"), Contract.bRequireMatchingYawAtExit);
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> DerivedVerticalAccessContractsToJson(const TArray<FLayoutDerivedVerticalAccessContract>& Contracts)
	{
		TArray<TSharedPtr<FJsonValue>> JsonContracts;
		for (const FLayoutDerivedVerticalAccessContract& Contract : Contracts)
		{
			JsonContracts.Add(MakeShared<FJsonValueObject>(DerivedVerticalAccessContractToJson(Contract)));
		}
		return JsonContracts;
	}

	TArray<FLayoutDerivedVerticalAccessContract> JsonToDerivedVerticalAccessContracts(const TArray<TSharedPtr<FJsonValue>>* JsonContracts, TArray<FString>& Issues, const FString& FieldPath)
	{
		TArray<FLayoutDerivedVerticalAccessContract> Contracts;
		if (JsonContracts == nullptr)
		{
			return Contracts;
		}

		for (int32 ContractIndex = 0; ContractIndex < JsonContracts->Num(); ++ContractIndex)
		{
			const TSharedPtr<FJsonObject> ContractObject = (*JsonContracts)[ContractIndex].IsValid() ? (*JsonContracts)[ContractIndex]->AsObject() : nullptr;
			if (!ContractObject.IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Invalid derived vertical-access contract object at %s[%d]."), *FieldPath, ContractIndex));
				continue;
			}

			FLayoutDerivedVerticalAccessContract& Contract = Contracts.AddDefaulted_GetRef();
			Contract.ContractId = FLayoutId(*ContractObject->GetStringField(TEXT("contract_id")));
			Contract.LocalCell = JsonToVector(ContractObject->GetObjectField(TEXT("local_cell")));
			const TArray<TSharedPtr<FJsonValue>>* JsonSourceTraversalChannels = nullptr;
			ContractObject->TryGetArrayField(TEXT("source_traversal_channels"), JsonSourceTraversalChannels);
			Contract.SourceTraversalChannels = JsonToTags(JsonSourceTraversalChannels, Issues, FieldPath + TEXT(".source_traversal_channels"));
			const TArray<TSharedPtr<FJsonValue>>* JsonExitTraversalChannels = nullptr;
			ContractObject->TryGetArrayField(TEXT("exit_traversal_channels"), JsonExitTraversalChannels);
			Contract.ExitTraversalChannels = JsonToTags(JsonExitTraversalChannels, Issues, FieldPath + TEXT(".exit_traversal_channels"));
			Contract.ExitFaceDirection = StringToEnum(ContractObject->GetStringField(TEXT("exit_face_direction")), ELayoutFaceDirection::PosZ, Issues, TEXT("exit_face_direction"));
			Contract.bRequireMatchingYawAtExit = ContractObject->GetBoolField(TEXT("require_matching_yaw_at_exit"));
		}

		return Contracts;
	}

	TSharedPtr<FJsonObject> LocalCellFaceRuleSnapshotToJson(const FLayoutLocalCellFaceRuleSnapshot& Snapshot)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetObjectField(TEXT("local_cell"), VectorToJson(Snapshot.LocalCell));
		Object->SetStringField(TEXT("template_path"), Snapshot.TemplatePath.ToString());
		Object->SetNumberField(TEXT("relative_yaw_rotation_steps"), Snapshot.RelativeYawRotationSteps);
		Object->SetArrayField(TEXT("roles"), RolesToJson(Snapshot.Roles));
		Object->SetArrayField(TEXT("supported_intents"), SupportedIntentsToJson(Snapshot.SupportedCellIntents));
		TArray<TSharedPtr<FJsonValue>> JsonFaceRules;
		for (const FLayoutFaceRule& Rule : Snapshot.ExposedFaceRules)
		{
			JsonFaceRules.Add(MakeShared<FJsonValueObject>(FaceRuleToJson(Rule)));
		}
		Object->SetArrayField(TEXT("exposed_face_rules"), JsonFaceRules);
		return Object;
	}

	TArray<FLayoutLocalCellFaceRuleSnapshot> JsonToLocalCellFaceRuleSnapshots(
		const TArray<TSharedPtr<FJsonValue>>* JsonSnapshots,
		TArray<FString>& Issues,
		const FString& FieldPath)
	{
		TArray<FLayoutLocalCellFaceRuleSnapshot> Snapshots;
		if (JsonSnapshots == nullptr)
		{
			return Snapshots;
		}

		for (int32 SnapshotIndex = 0; SnapshotIndex < JsonSnapshots->Num(); ++SnapshotIndex)
		{
			const TSharedPtr<FJsonObject> SnapshotObject = (*JsonSnapshots)[SnapshotIndex].IsValid() ? (*JsonSnapshots)[SnapshotIndex]->AsObject() : nullptr;
			if (!SnapshotObject.IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Invalid local cell face snapshot object at %s[%d]."), *FieldPath, SnapshotIndex));
				continue;
			}

			FLayoutLocalCellFaceRuleSnapshot& Snapshot = Snapshots.AddDefaulted_GetRef();
			Snapshot.LocalCell = JsonToVector(SnapshotObject->GetObjectField(TEXT("local_cell")));
			FString TemplatePath;
			if (SnapshotObject->TryGetStringField(TEXT("template_path"), TemplatePath))
			{
				Snapshot.TemplatePath = FSoftObjectPath(TemplatePath);
			}
			double RelativeYawRotationSteps = 0.0;
			if (SnapshotObject->TryGetNumberField(TEXT("relative_yaw_rotation_steps"), RelativeYawRotationSteps))
			{
				Snapshot.RelativeYawRotationSteps = static_cast<int32>(RelativeYawRotationSteps);
			}
			const TArray<TSharedPtr<FJsonValue>>* JsonRoles = nullptr;
			SnapshotObject->TryGetArrayField(TEXT("roles"), JsonRoles);
			Snapshot.Roles = JsonToRoles(JsonRoles, Issues);
			const TArray<TSharedPtr<FJsonValue>>* JsonSupportedIntents = nullptr;
			SnapshotObject->TryGetArrayField(TEXT("supported_intents"), JsonSupportedIntents);
			Snapshot.SupportedCellIntents = JsonToSupportedIntents(JsonSupportedIntents, Issues);
			const TArray<TSharedPtr<FJsonValue>>* JsonFaceRules = nullptr;
			SnapshotObject->TryGetArrayField(TEXT("exposed_face_rules"), JsonFaceRules);
			if (JsonFaceRules != nullptr)
			{
				for (int32 FaceIndex = 0; FaceIndex < JsonFaceRules->Num(); ++FaceIndex)
				{
					Snapshot.ExposedFaceRules.Add(JsonToFaceRule(
						(*JsonFaceRules)[FaceIndex].IsValid() ? (*JsonFaceRules)[FaceIndex]->AsObject() : nullptr,
						Issues,
						FString::Printf(TEXT("%s[%d].exposed_face_rules[%d]"), *FieldPath, SnapshotIndex, FaceIndex)));
				}
			}
		}

		return Snapshots;
	}

	TSharedPtr<FJsonObject> DerivedInternalTraversalLinkToJson(const FLayoutDerivedInternalTraversalLink& Link)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("link_id"), Link.LinkId.ToString());
		Object->SetObjectField(TEXT("from_local_cell"), VectorToJson(Link.FromLocalCell));
		Object->SetStringField(TEXT("from_traversal_channel"), Link.FromTraversalChannel.ToString());
		Object->SetObjectField(TEXT("to_local_cell"), VectorToJson(Link.ToLocalCell));
		Object->SetStringField(TEXT("to_traversal_channel"), Link.ToTraversalChannel.ToString());
		Object->SetBoolField(TEXT("bidirectional"), Link.bBidirectional);
		return Object;
	}

	TArray<FLayoutDerivedInternalTraversalLink> JsonToDerivedInternalTraversalLinks(
		const TArray<TSharedPtr<FJsonValue>>* JsonLinks,
		TArray<FString>& Issues,
		const FString& FieldPath)
	{
		TArray<FLayoutDerivedInternalTraversalLink> Links;
		if (JsonLinks == nullptr)
		{
			return Links;
		}

		for (int32 LinkIndex = 0; LinkIndex < JsonLinks->Num(); ++LinkIndex)
		{
			const TSharedPtr<FJsonObject> LinkObject = (*JsonLinks)[LinkIndex].IsValid() ? (*JsonLinks)[LinkIndex]->AsObject() : nullptr;
			if (!LinkObject.IsValid())
			{
				Issues.Add(FString::Printf(TEXT("Invalid derived internal traversal link object at %s[%d]."), *FieldPath, LinkIndex));
				continue;
			}

			FLayoutDerivedInternalTraversalLink& Link = Links.AddDefaulted_GetRef();
			Link.LinkId = FLayoutId(*LinkObject->GetStringField(TEXT("link_id")));
			Link.FromLocalCell = JsonToVector(LinkObject->GetObjectField(TEXT("from_local_cell")));
			Link.FromTraversalChannel = FGameplayTag::RequestGameplayTag(FName(*LinkObject->GetStringField(TEXT("from_traversal_channel"))), false);
			Link.ToLocalCell = JsonToVector(LinkObject->GetObjectField(TEXT("to_local_cell")));
			Link.ToTraversalChannel = FGameplayTag::RequestGameplayTag(FName(*LinkObject->GetStringField(TEXT("to_traversal_channel"))), false);
			Link.bBidirectional = LinkObject->GetBoolField(TEXT("bidirectional"));
		}

		return Links;
	}

	TSharedPtr<FJsonObject> ModuleSnapshotToJson(const FLayoutModuleSolveSnapshot& Snapshot)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("snapshot_id"), Snapshot.SnapshotId.ToString());
		Object->SetNumberField(TEXT("snapshot_schema_version"), Snapshot.SnapshotSchemaVersion);
		Object->SetStringField(TEXT("debug_name"), Snapshot.DebugName.ToString());
		Object->SetStringField(TEXT("template_name"), Snapshot.Template.IsNull() ? FString() : Snapshot.Template.GetAssetName());
		Object->SetObjectField(TEXT("cell_size_in_blocks"), VectorToJson(Snapshot.CellSizeInBlocks));
		Object->SetObjectField(TEXT("bounds_cells"), VectorToJson(Snapshot.BoundsCells));
		Object->SetObjectField(TEXT("template_dimensions_blocks"), VectorToJson(Snapshot.TemplateDimensionsBlocks));
		TArray<TSharedPtr<FJsonValue>> JsonOccupiedLocalCells;
		for (const FIntVector& Cell : Snapshot.OccupiedLocalCells)
		{
			JsonOccupiedLocalCells.Add(MakeShared<FJsonValueObject>(VectorToJson(Cell)));
		}
		Object->SetArrayField(TEXT("occupied_local_cells"), JsonOccupiedLocalCells);
		TArray<TSharedPtr<FJsonValue>> JsonLocalCellFaceRules;
		for (const FLayoutLocalCellFaceRuleSnapshot& CellSnapshot : Snapshot.GeneratedLocalCellFaceRules)
		{
			JsonLocalCellFaceRules.Add(MakeShared<FJsonValueObject>(LocalCellFaceRuleSnapshotToJson(CellSnapshot)));
		}
		Object->SetArrayField(TEXT("generated_local_cell_face_rules"), JsonLocalCellFaceRules);
		Object->SetArrayField(TEXT("roles"), RolesToJson(Snapshot.Roles));
		Object->SetArrayField(TEXT("supported_intents"), SupportedIntentsToJson(Snapshot.SupportedCellIntents));
		Object->SetArrayField(TEXT("root_supported_intents"), SupportedIntentsToJson(Snapshot.RootSupportedCellIntents));
		Object->SetArrayField(TEXT("allowed_yaw_rotation_steps"), IntsToJson(Snapshot.AllowedYawRotationSteps));
		TArray<TSharedPtr<FJsonValue>> JsonFaceRules;
		for (const FLayoutFaceRule& Rule : Snapshot.EffectiveFaceRules.ToArray())
		{
			JsonFaceRules.Add(MakeShared<FJsonValueObject>(FaceRuleToJson(Rule)));
		}
		Object->SetArrayField(TEXT("effective_face_rules"), JsonFaceRules);
		Object->SetArrayField(TEXT("traversal_channels"), TagsToJson(Snapshot.TraversalChannels));
		TArray<TSharedPtr<FJsonValue>> JsonInternalLinks;
		for (const FLayoutInternalAccessLink& Link : Snapshot.InternalAccessLinks)
		{
			JsonInternalLinks.Add(MakeShared<FJsonValueObject>(InternalAccessLinkToJson(Link)));
		}
		Object->SetArrayField(TEXT("internal_access_links"), JsonInternalLinks);
		TArray<TSharedPtr<FJsonValue>> JsonDerivedInternalLinks;
		for (const FLayoutDerivedInternalTraversalLink& Link : Snapshot.DerivedInternalTraversalLinks)
		{
			JsonDerivedInternalLinks.Add(MakeShared<FJsonValueObject>(DerivedInternalTraversalLinkToJson(Link)));
		}
		Object->SetArrayField(TEXT("derived_internal_traversal_links"), JsonDerivedInternalLinks);
		Object->SetNumberField(TEXT("min_traversable_neighbor_faces"), Snapshot.MinTraversableNeighborFaces);
		Object->SetNumberField(TEXT("weight"), Snapshot.Weight);
		Object->SetStringField(TEXT("placement_zone"), EnumToString(Snapshot.PlacementZone));
		Object->SetStringField(TEXT("level_placement_policy"), EnumToString(Snapshot.LevelPlacementPolicy));
		Object->SetNumberField(TEXT("specific_level"), Snapshot.SpecificLevel);
		Object->SetBoolField(TEXT("optional"), Snapshot.bOptional);
		Object->SetArrayField(TEXT("validation_messages"), ValidationMessagesToJson(Snapshot.Validation.Messages));
		Object->SetArrayField(TEXT("derived_endpoint_offers"), DerivedEndpointOffersToJson(Snapshot.DerivedEndpointOffers));
		Object->SetArrayField(TEXT("derived_span_offers"), DerivedSpanOffersToJson(Snapshot.DerivedSpanOffers));
		Object->SetArrayField(TEXT("derived_vertical_access_contracts"), DerivedVerticalAccessContractsToJson(Snapshot.DerivedVerticalAccessContracts));
		Object->SetArrayField(TEXT("proof_records"), ProofRecordsToJson(Snapshot.ProofRecords));
		Object->SetArrayField(TEXT("validation_assertions"), ValidationAssertionsToJson(Snapshot.ValidationAssertions));
		return Object;
	}

	FLayoutModuleSolveSnapshot JsonToModuleSnapshot(
		const TSharedPtr<FJsonObject>& Object,
		const TMap<FName, TObjectPtr<ULayoutModuleAsset>>& ModulesBySnapshotId,
		const TMap<FName, TObjectPtr<ULayoutCompositeModuleAsset>>& CompositeModulesBySnapshotId,
		TArray<FString>& Issues,
		const FString& FieldPath)
	{
		FLayoutModuleSolveSnapshot Snapshot;
		if (!Object.IsValid())
		{
			Issues.Add(FString::Printf(TEXT("Invalid module snapshot object in %s."), *FieldPath));
			return Snapshot;
		}

		Snapshot.SnapshotId = FLayoutId(*Object->GetStringField(TEXT("snapshot_id")));
		Snapshot.SnapshotSchemaVersion = static_cast<int32>(Object->GetIntegerField(TEXT("snapshot_schema_version")));
		Snapshot.DebugName = FName(*Object->GetStringField(TEXT("debug_name")));
		Snapshot.CellSizeInBlocks = JsonToVector(Object->GetObjectField(TEXT("cell_size_in_blocks")));
		if (Object->HasField(TEXT("bounds_cells")))
		{
			Snapshot.BoundsCells = JsonToVector(Object->GetObjectField(TEXT("bounds_cells")));
		}
		if (Object->HasField(TEXT("template_dimensions_blocks")))
		{
			Snapshot.TemplateDimensionsBlocks = JsonToVector(Object->GetObjectField(TEXT("template_dimensions_blocks")));
		}
		const TArray<TSharedPtr<FJsonValue>>* JsonOccupiedLocalCells = nullptr;
		Object->TryGetArrayField(TEXT("occupied_local_cells"), JsonOccupiedLocalCells);
		if (JsonOccupiedLocalCells != nullptr)
		{
			for (int32 CellIndex = 0; CellIndex < JsonOccupiedLocalCells->Num(); ++CellIndex)
			{
				Snapshot.OccupiedLocalCells.Add(JsonToVector(
					(*JsonOccupiedLocalCells)[CellIndex].IsValid() ? (*JsonOccupiedLocalCells)[CellIndex]->AsObject() : nullptr));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* JsonLocalCellFaceRules = nullptr;
		Object->TryGetArrayField(TEXT("generated_local_cell_face_rules"), JsonLocalCellFaceRules);
		Snapshot.GeneratedLocalCellFaceRules = JsonToLocalCellFaceRuleSnapshots(
			JsonLocalCellFaceRules,
			Issues,
			FieldPath + TEXT(".generated_local_cell_face_rules"));
		const TArray<TSharedPtr<FJsonValue>>* JsonRoles = nullptr;
		Object->TryGetArrayField(TEXT("roles"), JsonRoles);
		Snapshot.Roles = JsonToRoles(JsonRoles, Issues);
		const TArray<TSharedPtr<FJsonValue>>* JsonSupportedIntents = nullptr;
		Object->TryGetArrayField(TEXT("supported_intents"), JsonSupportedIntents);
		Snapshot.SupportedCellIntents = JsonToSupportedIntents(JsonSupportedIntents, Issues);
		const TArray<TSharedPtr<FJsonValue>>* JsonRootSupportedIntents = nullptr;
		Object->TryGetArrayField(TEXT("root_supported_intents"), JsonRootSupportedIntents);
		Snapshot.RootSupportedCellIntents = JsonRootSupportedIntents != nullptr
			? JsonToSupportedIntents(JsonRootSupportedIntents, Issues)
			: Snapshot.SupportedCellIntents;
		const TArray<TSharedPtr<FJsonValue>>* JsonYawRotationSteps = nullptr;
		Object->TryGetArrayField(TEXT("allowed_yaw_rotation_steps"), JsonYawRotationSteps);
		Snapshot.AllowedYawRotationSteps = JsonToInts(JsonYawRotationSteps);
		const TArray<TSharedPtr<FJsonValue>>* JsonFaceRules = nullptr;
		Object->TryGetArrayField(TEXT("effective_face_rules"), JsonFaceRules);
		if (JsonFaceRules != nullptr)
		{
			for (int32 FaceIndex = 0; FaceIndex < JsonFaceRules->Num(); ++FaceIndex)
			{
				Snapshot.EffectiveFaceRules.SetRule(JsonToFaceRule(
					(*JsonFaceRules)[FaceIndex].IsValid() ? (*JsonFaceRules)[FaceIndex]->AsObject() : nullptr,
					Issues,
					FString::Printf(TEXT("%s.effective_face_rules[%d]"), *FieldPath, FaceIndex)));
			}
		}
		Snapshot.EffectiveFaceRules.NormalizeDirections();
		const TArray<TSharedPtr<FJsonValue>>* JsonTraversalChannels = nullptr;
		Object->TryGetArrayField(TEXT("traversal_channels"), JsonTraversalChannels);
		Snapshot.TraversalChannels = JsonToTags(JsonTraversalChannels, Issues, FieldPath + TEXT(".traversal_channels"));
		const TArray<TSharedPtr<FJsonValue>>* JsonInternalLinks = nullptr;
		Object->TryGetArrayField(TEXT("internal_access_links"), JsonInternalLinks);
		if (JsonInternalLinks != nullptr)
		{
			for (int32 LinkIndex = 0; LinkIndex < JsonInternalLinks->Num(); ++LinkIndex)
			{
				Snapshot.InternalAccessLinks.Add(JsonToInternalAccessLink(
					(*JsonInternalLinks)[LinkIndex].IsValid() ? (*JsonInternalLinks)[LinkIndex]->AsObject() : nullptr,
					Issues,
					FString::Printf(TEXT("%s.internal_access_links[%d]"), *FieldPath, LinkIndex)));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* JsonDerivedInternalLinks = nullptr;
		Object->TryGetArrayField(TEXT("derived_internal_traversal_links"), JsonDerivedInternalLinks);
		Snapshot.DerivedInternalTraversalLinks = JsonToDerivedInternalTraversalLinks(
			JsonDerivedInternalLinks,
			Issues,
			FieldPath + TEXT(".derived_internal_traversal_links"));
		Snapshot.MinTraversableNeighborFaces = static_cast<int32>(Object->GetIntegerField(TEXT("min_traversable_neighbor_faces")));
		Snapshot.Weight = static_cast<int32>(Object->GetIntegerField(TEXT("weight")));
		if (Object->HasField(TEXT("placement_zone")))
		{
			Snapshot.PlacementZone = StringToEnum(Object->GetStringField(TEXT("placement_zone")), ELayoutPlacementZone::Any, Issues, TEXT("placement_zone"));
		}
		if (Object->HasField(TEXT("level_placement_policy")))
		{
			Snapshot.LevelPlacementPolicy = StringToEnum(Object->GetStringField(TEXT("level_placement_policy")), ELayoutLevelPlacementPolicy::AnyLevel, Issues, TEXT("level_placement_policy"));
		}
		if (Object->HasField(TEXT("specific_level")))
		{
			Snapshot.SpecificLevel = static_cast<int32>(Object->GetIntegerField(TEXT("specific_level")));
		}
		Object->TryGetBoolField(TEXT("optional"), Snapshot.bOptional);
		const TArray<TSharedPtr<FJsonValue>>* JsonValidationMessages = nullptr;
		Object->TryGetArrayField(TEXT("validation_messages"), JsonValidationMessages);
		Snapshot.Validation.Messages = JsonToValidationMessages(JsonValidationMessages, Issues, FieldPath + TEXT(".validation_messages"));
		const TArray<TSharedPtr<FJsonValue>>* JsonDerivedEndpointOffers = nullptr;
		Object->TryGetArrayField(TEXT("derived_endpoint_offers"), JsonDerivedEndpointOffers);
		Snapshot.DerivedEndpointOffers = JsonToDerivedEndpointOffers(JsonDerivedEndpointOffers, Issues, FieldPath + TEXT(".derived_endpoint_offers"));
		const TArray<TSharedPtr<FJsonValue>>* JsonDerivedSpanOffers = nullptr;
		Object->TryGetArrayField(TEXT("derived_span_offers"), JsonDerivedSpanOffers);
		Snapshot.DerivedSpanOffers = JsonToDerivedSpanOffers(JsonDerivedSpanOffers, Issues, FieldPath + TEXT(".derived_span_offers"));
		const TArray<TSharedPtr<FJsonValue>>* JsonDerivedVerticalAccessContracts = nullptr;
		Object->TryGetArrayField(TEXT("derived_vertical_access_contracts"), JsonDerivedVerticalAccessContracts);
		Snapshot.DerivedVerticalAccessContracts = JsonToDerivedVerticalAccessContracts(JsonDerivedVerticalAccessContracts, Issues, FieldPath + TEXT(".derived_vertical_access_contracts"));
		const TArray<TSharedPtr<FJsonValue>>* JsonProofRecords = nullptr;
		Object->TryGetArrayField(TEXT("proof_records"), JsonProofRecords);
		Snapshot.ProofRecords = JsonToProofRecords(JsonProofRecords, Issues, FieldPath + TEXT(".proof_records"));
		const TArray<TSharedPtr<FJsonValue>>* JsonAssertions = nullptr;
		Object->TryGetArrayField(TEXT("validation_assertions"), JsonAssertions);
		Snapshot.ValidationAssertions = JsonToValidationAssertions(JsonAssertions, Issues, FieldPath + TEXT(".validation_assertions"));
		// Catalog identity includes the entry contract when one asset has several uses.
		// Asset libraries remain asset-keyed; keep the qualified solve identity intact.
		FName SourceAssetId(*Snapshot.SnapshotId.ToString(), FNAME_Find);
		const FString EntryPrefix = Snapshot.DebugName.ToString() + TEXT(".Entry.");
		const FString SnapshotIdText = Snapshot.SnapshotId.ToString();
		if (!ModulesBySnapshotId.Contains(SourceAssetId) && !CompositeModulesBySnapshotId.Contains(SourceAssetId)
			&& SnapshotIdText.StartsWith(EntryPrefix, ESearchCase::CaseSensitive)
			&& SnapshotIdText.Len() > EntryPrefix.Len())
		{
			SourceAssetId = Snapshot.DebugName;
			Snapshot.SourceContentEntryId = FName(*SnapshotIdText.RightChop(EntryPrefix.Len()));
		}
		if (const TObjectPtr<ULayoutModuleAsset>* Module = ModulesBySnapshotId.Find(SourceAssetId))
		{
			Snapshot.SourceModule = *Module;
			Snapshot.Template = (*Module)->Template;
		}
		else if (const TObjectPtr<ULayoutCompositeModuleAsset>* CompositeModule = CompositeModulesBySnapshotId.Find(SourceAssetId))
		{
			Snapshot.SourceCompositeModule = *CompositeModule;
		}
		else if (ModulesBySnapshotId.Num() > 0 || CompositeModulesBySnapshotId.Num() > 0)
		{
			Issues.Add(FString::Printf(TEXT("Compiled snapshot references unknown module snapshot id '%s' in %s."), *Snapshot.SnapshotId.ToString(), *FieldPath));
		}
		return Snapshot;
	}

	TSharedPtr<FJsonObject> ModuleCatalogToJson(const FLayoutModuleCatalog& Snapshot)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("snapshot_id"), Snapshot.SnapshotId.ToString());
		Object->SetNumberField(TEXT("snapshot_schema_version"), Snapshot.SnapshotSchemaVersion);
		Object->SetStringField(TEXT("debug_name"), Snapshot.DebugName.ToString());
		Object->SetObjectField(TEXT("shared_cell_size_in_blocks"), VectorToJson(Snapshot.SharedCellSizeInBlocks));
		Object->SetArrayField(TEXT("validation_messages"), ValidationMessagesToJson(Snapshot.Validation.Messages));
		Object->SetArrayField(TEXT("proof_records"), ProofRecordsToJson(Snapshot.ProofRecords));
		Object->SetArrayField(TEXT("validation_assertions"), ValidationAssertionsToJson(Snapshot.ValidationAssertions));
		TArray<TSharedPtr<FJsonValue>> JsonModules;
		for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : Snapshot.Modules)
		{
			JsonModules.Add(MakeShared<FJsonValueObject>(ModuleSnapshotToJson(ModuleSnapshot)));
		}
		Object->SetArrayField(TEXT("modules"), JsonModules);
		return Object;
	}

	FLayoutModuleCatalog JsonToModuleCatalog(
		const TSharedPtr<FJsonObject>& Object,
		const TMap<FName, TObjectPtr<ULayoutModuleAsset>>& ModulesBySnapshotId,
		const TMap<FName, TObjectPtr<ULayoutCompositeModuleAsset>>& CompositeModulesBySnapshotId,
		TArray<FString>& Issues,
		const FString& FieldPath)
	{
		FLayoutModuleCatalog Snapshot;
		if (!Object.IsValid())
		{
			Issues.Add(FString::Printf(TEXT("Invalid module-set snapshot object in %s."), *FieldPath));
			return Snapshot;
		}

		Snapshot.SnapshotId = FLayoutId(*Object->GetStringField(TEXT("snapshot_id")));
		Snapshot.SnapshotSchemaVersion = static_cast<int32>(Object->GetIntegerField(TEXT("snapshot_schema_version")));
		Snapshot.DebugName = FName(*Object->GetStringField(TEXT("debug_name")));
		Snapshot.SharedCellSizeInBlocks = JsonToVector(Object->GetObjectField(TEXT("shared_cell_size_in_blocks")));
		const TArray<TSharedPtr<FJsonValue>>* JsonValidationMessages = nullptr;
		Object->TryGetArrayField(TEXT("validation_messages"), JsonValidationMessages);
		Snapshot.Validation.Messages = JsonToValidationMessages(JsonValidationMessages, Issues, FieldPath + TEXT(".validation_messages"));
		const TArray<TSharedPtr<FJsonValue>>* JsonProofRecords = nullptr;
		Object->TryGetArrayField(TEXT("proof_records"), JsonProofRecords);
		Snapshot.ProofRecords = JsonToProofRecords(JsonProofRecords, Issues, FieldPath + TEXT(".proof_records"));
		const TArray<TSharedPtr<FJsonValue>>* JsonAssertions = nullptr;
		Object->TryGetArrayField(TEXT("validation_assertions"), JsonAssertions);
		Snapshot.ValidationAssertions = JsonToValidationAssertions(JsonAssertions, Issues, FieldPath + TEXT(".validation_assertions"));
		const TArray<TSharedPtr<FJsonValue>>* JsonModules = nullptr;
		Object->TryGetArrayField(TEXT("modules"), JsonModules);
		if (JsonModules != nullptr)
		{
			for (int32 ModuleIndex = 0; ModuleIndex < JsonModules->Num(); ++ModuleIndex)
			{
				Snapshot.Modules.Add(JsonToModuleSnapshot(
					(*JsonModules)[ModuleIndex].IsValid() ? (*JsonModules)[ModuleIndex]->AsObject() : nullptr,
					ModulesBySnapshotId,
					CompositeModulesBySnapshotId,
					Issues,
					FString::Printf(TEXT("%s.modules[%d]"), *FieldPath, ModuleIndex)));
			}
		}
		return Snapshot;
	}

	TSharedPtr<FJsonObject> ChildRequestTemplateSnapshotToJson(const FLayoutChildRequestTemplateSnapshot& Snapshot);
	TSharedPtr<FLayoutChildRequestTemplateSnapshot> JsonToChildRequestTemplateSnapshot(
		const TSharedPtr<FJsonObject>& Object,
		TArray<FString>& Issues,
		const FString& FieldPath);

	TSharedPtr<FJsonObject> ContentSetEntrySnapshotToJson(const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("entry_id"), EntrySnapshot.EntryId.ToString());
		Object->SetStringField(TEXT("content_kind"), EnumToString(EntrySnapshot.ContentKind));
		Object->SetNumberField(TEXT("weight"), EntrySnapshot.Weight);
		Object->SetArrayField(TEXT("provided_zone_features"), TagsToJson(EntrySnapshot.ProvidedZoneFeatures));
		Object->SetArrayField(TEXT("closure_provider_intents"), ClosureProviderIntentsToJson(EntrySnapshot.ClosureProviderIntents));
		Object->SetArrayField(TEXT("seam_provider_intents"), SeamProviderIntentsToJson(EntrySnapshot.SeamProviderIntents));
		Object->SetNumberField(TEXT("module_snapshot_index"), EntrySnapshot.ModuleSnapshotIndex);
		Object->SetStringField(TEXT("module_placement_zone"), EnumToString(EntrySnapshot.ModulePlacementZone));
		Object->SetStringField(TEXT("module_level_placement_policy"), EnumToString(EntrySnapshot.ModuleLevelPlacementPolicy));
		Object->SetNumberField(TEXT("module_specific_level"), EntrySnapshot.ModuleSpecificLevel);
		Object->SetBoolField(TEXT("module_optional"), EntrySnapshot.bModuleOptional);
		Object->SetStringField(TEXT("child_profile_snapshot_id"), EntrySnapshot.ChildProfileSnapshotId.ToString());
		Object->SetStringField(TEXT("child_placement_zone"), EnumToString(EntrySnapshot.ChildPlacementZone));
		Object->SetStringField(TEXT("child_level_placement_policy"), EnumToString(EntrySnapshot.ChildLevelPlacementPolicy));
		Object->SetNumberField(TEXT("child_specific_level"), EntrySnapshot.ChildSpecificLevel);
		Object->SetBoolField(TEXT("child_optional"), EntrySnapshot.bChildOptional);
		Object->SetBoolField(TEXT("child_contributes_host_vertical_access"), EntrySnapshot.bChildContributesHostVerticalAccess);
		Object->SetStringField(TEXT("child_content_set_snapshot_id"), EntrySnapshot.ChildContentSetSnapshotId.ToString());
		if (EntrySnapshot.CompiledChildRequestTemplate.IsValid())
		{
			Object->SetObjectField(
				TEXT("compiled_child_request_template"),
				ChildRequestTemplateSnapshotToJson(*EntrySnapshot.CompiledChildRequestTemplate));
		}
		return Object;
	}

	FLayoutRegionContentEntrySolveSnapshot JsonToContentSetEntrySnapshot(
		const TSharedPtr<FJsonObject>& Object,
		TArray<FString>& Issues,
		const FString& FieldPath)
	{
		FLayoutRegionContentEntrySolveSnapshot EntrySnapshot;
		if (!Object.IsValid())
		{
			Issues.Add(FString::Printf(TEXT("Invalid content-set entry snapshot object in %s."), *FieldPath));
			return EntrySnapshot;
		}

		EntrySnapshot.EntryId = FName(*Object->GetStringField(TEXT("entry_id")));
		EntrySnapshot.ContentKind = StringToEnum(Object->GetStringField(TEXT("content_kind")), ELayoutRegionContentKind::Module, Issues, TEXT("content_kind"));
		EntrySnapshot.Weight = static_cast<int32>(Object->GetIntegerField(TEXT("weight")));
		const TArray<TSharedPtr<FJsonValue>>* JsonProvidedZoneFeatures = nullptr;
		Object->TryGetArrayField(TEXT("provided_zone_features"), JsonProvidedZoneFeatures);
		EntrySnapshot.ProvidedZoneFeatures = JsonToTags(JsonProvidedZoneFeatures, Issues, FieldPath + TEXT(".provided_zone_features"));
		const TArray<TSharedPtr<FJsonValue>>* JsonClosureProviderIntents = nullptr;
		if (!Object->TryGetArrayField(TEXT("closure_provider_intents"), JsonClosureProviderIntents))
		{
			Object->TryGetArrayField(TEXT("boundary_provider_intents"), JsonClosureProviderIntents);
		}
		EntrySnapshot.ClosureProviderIntents = JsonToClosureProviderIntents(JsonClosureProviderIntents, Issues, FieldPath + TEXT(".closure_provider_intents"));
		const TArray<TSharedPtr<FJsonValue>>* JsonSeamProviderIntents = nullptr;
		Object->TryGetArrayField(TEXT("seam_provider_intents"), JsonSeamProviderIntents);
		EntrySnapshot.SeamProviderIntents = JsonToSeamProviderIntents(JsonSeamProviderIntents, Issues, FieldPath + TEXT(".seam_provider_intents"));
		EntrySnapshot.ModuleSnapshotIndex = static_cast<int32>(Object->GetIntegerField(TEXT("module_snapshot_index")));
		if (Object->HasField(TEXT("module_placement_zone")))
		{
			EntrySnapshot.ModulePlacementZone = StringToEnum(Object->GetStringField(TEXT("module_placement_zone")), ELayoutPlacementZone::Any, Issues, TEXT("module_placement_zone"));
		}
		if (Object->HasField(TEXT("module_level_placement_policy")))
		{
			EntrySnapshot.ModuleLevelPlacementPolicy = StringToEnum(Object->GetStringField(TEXT("module_level_placement_policy")), ELayoutLevelPlacementPolicy::AnyLevel, Issues, TEXT("module_level_placement_policy"));
		}
		if (Object->HasField(TEXT("module_specific_level")))
		{
			EntrySnapshot.ModuleSpecificLevel = static_cast<int32>(Object->GetIntegerField(TEXT("module_specific_level")));
		}
		Object->TryGetBoolField(TEXT("module_optional"), EntrySnapshot.bModuleOptional);
		EntrySnapshot.ChildProfileSnapshotId = FLayoutId(*Object->GetStringField(TEXT("child_profile_snapshot_id")));
		EntrySnapshot.ChildPlacementZone = StringToEnum(Object->GetStringField(TEXT("child_placement_zone")), ELayoutPlacementZone::Any, Issues, TEXT("child_placement_zone"));
		if (Object->HasField(TEXT("child_level_placement_policy")))
		{
			EntrySnapshot.ChildLevelPlacementPolicy = StringToEnum(Object->GetStringField(TEXT("child_level_placement_policy")), ELayoutLevelPlacementPolicy::AnyLevel, Issues, TEXT("child_level_placement_policy"));
		}
		if (Object->HasField(TEXT("child_specific_level")))
		{
			EntrySnapshot.ChildSpecificLevel = static_cast<int32>(Object->GetIntegerField(TEXT("child_specific_level")));
		}
		Object->TryGetBoolField(TEXT("child_optional"), EntrySnapshot.bChildOptional);
		Object->TryGetBoolField(TEXT("child_contributes_host_vertical_access"), EntrySnapshot.bChildContributesHostVerticalAccess);
		if (Object->HasField(TEXT("child_content_set_snapshot_id")))
		{
			EntrySnapshot.ChildContentSetSnapshotId = FLayoutId(*Object->GetStringField(TEXT("child_content_set_snapshot_id")));
		}
		const TSharedPtr<FJsonObject>* ChildTemplateObject = nullptr;
		if (Object->TryGetObjectField(TEXT("compiled_child_request_template"), ChildTemplateObject)
			&& ChildTemplateObject != nullptr
			&& (*ChildTemplateObject).IsValid())
		{
			EntrySnapshot.CompiledChildRequestTemplate =
				JsonToChildRequestTemplateSnapshot(*ChildTemplateObject, Issues, FieldPath + TEXT(".compiled_child_request_template"));
		}
		return EntrySnapshot;
	}

	TSharedPtr<FJsonObject> ContentSetSnapshotToJson(const FLayoutRegionContentSetSolveSnapshot& Snapshot)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("snapshot_id"), Snapshot.SnapshotId.ToString());
		Object->SetNumberField(TEXT("snapshot_schema_version"), Snapshot.SnapshotSchemaVersion);
		Object->SetStringField(TEXT("debug_name"), Snapshot.DebugName.ToString());
		Object->SetObjectField(TEXT("shared_cell_size_in_blocks"), VectorToJson(Snapshot.SharedCellSizeInBlocks));
		Object->SetArrayField(TEXT("validation_messages"), ValidationMessagesToJson(Snapshot.Validation.Messages));
		Object->SetArrayField(TEXT("proof_records"), ProofRecordsToJson(Snapshot.ProofRecords));
		Object->SetArrayField(TEXT("validation_assertions"), ValidationAssertionsToJson(Snapshot.ValidationAssertions));

		TArray<TSharedPtr<FJsonValue>> JsonEntries;
		for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : Snapshot.Entries)
		{
			JsonEntries.Add(MakeShared<FJsonValueObject>(ContentSetEntrySnapshotToJson(EntrySnapshot)));
		}
		Object->SetArrayField(TEXT("entries"), JsonEntries);
		return Object;
	}

	FLayoutRegionContentSetSolveSnapshot JsonToContentSetSnapshot(
		const TSharedPtr<FJsonObject>& Object,
		const TObjectPtr<ULayoutRegionContentSetAsset> SourceContentSet,
		TArray<FString>& Issues,
		const FString& FieldPath)
	{
		FLayoutRegionContentSetSolveSnapshot Snapshot;
		if (!Object.IsValid())
		{
			Issues.Add(FString::Printf(TEXT("Invalid content-set snapshot object in %s."), *FieldPath));
			return Snapshot;
		}

		Snapshot.SnapshotId = FLayoutId(*Object->GetStringField(TEXT("snapshot_id")));
		Snapshot.SnapshotSchemaVersion = static_cast<int32>(Object->GetIntegerField(TEXT("snapshot_schema_version")));
		Snapshot.SourceContentSet = SourceContentSet;
		Snapshot.DebugName = FName(*Object->GetStringField(TEXT("debug_name")));
		Snapshot.SharedCellSizeInBlocks = JsonToVector(Object->GetObjectField(TEXT("shared_cell_size_in_blocks")));
		const TArray<TSharedPtr<FJsonValue>>* JsonValidationMessages = nullptr;
		Object->TryGetArrayField(TEXT("validation_messages"), JsonValidationMessages);
		Snapshot.Validation.Messages = JsonToValidationMessages(JsonValidationMessages, Issues, FieldPath + TEXT(".validation_messages"));
		const TArray<TSharedPtr<FJsonValue>>* JsonProofRecords = nullptr;
		Object->TryGetArrayField(TEXT("proof_records"), JsonProofRecords);
		Snapshot.ProofRecords = JsonToProofRecords(JsonProofRecords, Issues, FieldPath + TEXT(".proof_records"));
		const TArray<TSharedPtr<FJsonValue>>* JsonAssertions = nullptr;
		Object->TryGetArrayField(TEXT("validation_assertions"), JsonAssertions);
		Snapshot.ValidationAssertions = JsonToValidationAssertions(JsonAssertions, Issues, FieldPath + TEXT(".validation_assertions"));
		const TArray<TSharedPtr<FJsonValue>>* JsonEntries = nullptr;
		Object->TryGetArrayField(TEXT("entries"), JsonEntries);
		if (JsonEntries != nullptr)
		{
			for (int32 EntryIndex = 0; EntryIndex < JsonEntries->Num(); ++EntryIndex)
			{
				Snapshot.Entries.Add(JsonToContentSetEntrySnapshot(
					(*JsonEntries)[EntryIndex].IsValid() ? (*JsonEntries)[EntryIndex]->AsObject() : nullptr,
					Issues,
					FString::Printf(TEXT("%s.entries[%d]"), *FieldPath, EntryIndex)));
			}
		}

			if (SourceContentSet != nullptr)
			{
				LayoutChildRequestTemplateCacheBuilder::FRecursiveChildTemplateBuildState ReboundChildTemplateState;
				for (FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : Snapshot.Entries)
				{
					const bool bChildTemplateNeedsRebind = EntrySnapshot.CompiledChildRequestTemplate.IsValid()
					&& EntrySnapshot.CompiledChildRequestTemplate->ModuleCatalog.Modules.ContainsByPredicate([](const FLayoutModuleSolveSnapshot& ModuleSnapshot)
					{
						return ModuleSnapshot.SourceModule == nullptr;
					});
				if (EntrySnapshot.ContentKind != ELayoutRegionContentKind::ChildRegion
					|| (EntrySnapshot.CompiledChildRequestTemplate.IsValid() && !bChildTemplateNeedsRebind))
				{
					continue;
				}

				const FLayoutRegionContentEntry* SourceEntry = SourceContentSet->Entries.FindByPredicate(
					[&EntrySnapshot](const FLayoutRegionContentEntry& Candidate)
					{
						return Candidate.EntryId == EntrySnapshot.EntryId;
					});
					if (SourceEntry == nullptr || SourceEntry->ChildRegionSettings.RegionProfile == nullptr)
					{
						continue;
					}

					const TSharedPtr<FLayoutChildRequestTemplateSnapshot> ChildTemplate =
						BuildImportedChildRequestTemplateSnapshot(
							SourceEntry->ChildRegionSettings.RegionProfile.Get(),
							Snapshot.SnapshotSchemaVersion,
							ReboundChildTemplateState);
					if (!ChildTemplate.IsValid())
					{
						Issues.Add(FString::Printf(
							TEXT("%s child entry '%s' could not rebind its compiled child request template from reconstructed profile '%s'."),
							*FieldPath,
							*EntrySnapshot.EntryId.ToString(),
							*SourceEntry->ChildRegionSettings.RegionProfile->GetName()));
						continue;
					}
					EntrySnapshot.ChildContentSetSnapshotId = ChildTemplate->ContentSetSnapshot.SnapshotId;
					EntrySnapshot.CompiledChildRequestTemplate = ChildTemplate;
				}
		}
		return Snapshot;
	}

	TSharedPtr<FJsonObject> SparsePlacementRuleSnapshotToJson(const FLayoutSparsePlacementRuleSolveSnapshot& Snapshot)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		const TCHAR* RuleType = TEXT("preserve_terrain");
		switch (Snapshot.RuleKind)
		{
		case ELayoutSparsePlacementRuleKind::Exact: RuleType = TEXT("exact"); break;
		case ELayoutSparsePlacementRuleKind::Range: RuleType = TEXT("range"); break;
		case ELayoutSparsePlacementRuleKind::FillAvailable: RuleType = TEXT("fill_available"); break;
		case ELayoutSparsePlacementRuleKind::PreserveTerrain: default: break;
		}
		Object->SetStringField(TEXT("rule_type"), RuleType);
		Object->SetStringField(TEXT("rule_id"), Snapshot.RuleId.ToString());
		Object->SetNumberField(TEXT("snapshot_schema_version"), Snapshot.SnapshotSchemaVersion);
		Object->SetStringField(TEXT("placement_zone"), EnumToString(Snapshot.PlacementZone));
		Object->SetStringField(TEXT("level_placement_policy"), EnumToString(Snapshot.LevelPlacementPolicy));
		Object->SetNumberField(TEXT("specific_level"), Snapshot.SpecificLevel);
		if (Snapshot.RuleKind != ELayoutSparsePlacementRuleKind::PreserveTerrain)
		{
			Object->SetStringField(TEXT("candidate_source"), EnumToString(Snapshot.CandidateSource));
			Object->SetObjectField(TEXT("content_set_snapshot"), ContentSetSnapshotToJson(Snapshot.ContentSetSnapshot));
			Object->SetObjectField(TEXT("module_set_snapshot"), ModuleCatalogToJson(Snapshot.ModuleCatalog));
			Object->SetNumberField(TEXT("min_spacing_cells"), Snapshot.MinSpacingCells);
		}
		if (Snapshot.RuleKind == ELayoutSparsePlacementRuleKind::Exact)
		{
			Object->SetNumberField(TEXT("count"), Snapshot.Count);
		}
		else if (Snapshot.RuleKind == ELayoutSparsePlacementRuleKind::Range)
		{
			Object->SetNumberField(TEXT("min_count"), Snapshot.MinCount);
			Object->SetNumberField(TEXT("max_count"), Snapshot.MaxCount);
		}
		Object->SetArrayField(TEXT("validation_messages"), ValidationMessagesToJson(Snapshot.Validation.Messages));
		Object->SetArrayField(TEXT("proof_records"), ProofRecordsToJson(Snapshot.ProofRecords));
		Object->SetArrayField(TEXT("validation_assertions"), ValidationAssertionsToJson(Snapshot.ValidationAssertions));
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> SparsePlacementRuleSnapshotsToJson(const TArray<FLayoutSparsePlacementRuleSolveSnapshot>& Snapshots)
	{
		TArray<TSharedPtr<FJsonValue>> JsonSnapshots;
		for (const FLayoutSparsePlacementRuleSolveSnapshot& Snapshot : Snapshots)
		{
			JsonSnapshots.Add(MakeShared<FJsonValueObject>(SparsePlacementRuleSnapshotToJson(Snapshot)));
		}
		return JsonSnapshots;
	}

	FLayoutSparsePlacementRuleSolveSnapshot JsonToSparsePlacementRuleSnapshot(
		const TSharedPtr<FJsonObject>& Object,
		TArray<FString>& Issues,
		const FString& FieldPath)
	{
		FLayoutSparsePlacementRuleSolveSnapshot Snapshot;
		if (!Object.IsValid())
		{
			Issues.Add(FString::Printf(TEXT("Invalid sparse placement rule snapshot object in %s."), *FieldPath));
			return Snapshot;
		}

		Snapshot.RuleId = FName(*Object->GetStringField(TEXT("rule_id")));
		Snapshot.SnapshotSchemaVersion = static_cast<int32>(Object->GetIntegerField(TEXT("snapshot_schema_version")));
		const FString RuleType = Object->GetStringField(TEXT("rule_type"));
		if (RuleType == TEXT("preserve_terrain")) Snapshot.RuleKind = ELayoutSparsePlacementRuleKind::PreserveTerrain;
		else if (RuleType == TEXT("exact")) Snapshot.RuleKind = ELayoutSparsePlacementRuleKind::Exact;
		else if (RuleType == TEXT("range")) Snapshot.RuleKind = ELayoutSparsePlacementRuleKind::Range;
		else if (RuleType == TEXT("fill_available")) Snapshot.RuleKind = ELayoutSparsePlacementRuleKind::FillAvailable;
		else Issues.Add(FString::Printf(TEXT("Unsupported sparse placement rule type '%s' in %s."), *RuleType, *FieldPath));
		Snapshot.PlacementZone = StringToEnum(Object->GetStringField(TEXT("placement_zone")), ELayoutPlacementZone::Interior, Issues, TEXT("placement_zone"));
		Snapshot.LevelPlacementPolicy = StringToEnum(Object->GetStringField(TEXT("level_placement_policy")), ELayoutLevelPlacementPolicy::AnyLevel, Issues, TEXT("level_placement_policy"));
		Snapshot.SpecificLevel = static_cast<int32>(Object->GetIntegerField(TEXT("specific_level")));
		if (Snapshot.RuleKind != ELayoutSparsePlacementRuleKind::PreserveTerrain)
		{
			Snapshot.CandidateSource = StringToEnum(Object->GetStringField(TEXT("candidate_source")), ELayoutSparseCandidateSource::PreserveSupportedTerrain, Issues, TEXT("candidate_source"));
			const TSharedPtr<FJsonObject>* ContentSetSnapshotObject = nullptr;
			if (Object->TryGetObjectField(TEXT("content_set_snapshot"), ContentSetSnapshotObject) && ContentSetSnapshotObject != nullptr && (*ContentSetSnapshotObject).IsValid())
			{
				Snapshot.ContentSetSnapshot = JsonToContentSetSnapshot(*ContentSetSnapshotObject, nullptr, Issues, FieldPath + TEXT(".content_set_snapshot"));
			}
			else
			{
				Issues.Add(FString::Printf(TEXT("Missing content_set_snapshot object in %s."), *FieldPath));
			}

			const TSharedPtr<FJsonObject>* ModuleCatalogObject = nullptr;
			if (Object->TryGetObjectField(TEXT("module_set_snapshot"), ModuleCatalogObject) && ModuleCatalogObject != nullptr && (*ModuleCatalogObject).IsValid())
			{
				Snapshot.ModuleCatalog.SnapshotId = FLayoutId(*(*ModuleCatalogObject)->GetStringField(TEXT("snapshot_id")));
				Snapshot.ModuleCatalog.SnapshotSchemaVersion = static_cast<int32>((*ModuleCatalogObject)->GetIntegerField(TEXT("snapshot_schema_version")));
				Snapshot.ModuleCatalog.DebugName = FName(*(*ModuleCatalogObject)->GetStringField(TEXT("debug_name")));
				Snapshot.ModuleCatalog.SharedCellSizeInBlocks = JsonToVector((*ModuleCatalogObject)->GetObjectField(TEXT("shared_cell_size_in_blocks")));
			}
			else
			{
				Issues.Add(FString::Printf(TEXT("Missing module_set_snapshot object in %s."), *FieldPath));
			}
			Snapshot.MinSpacingCells = static_cast<int32>(Object->GetIntegerField(TEXT("min_spacing_cells")));
		}
		if (Snapshot.RuleKind == ELayoutSparsePlacementRuleKind::Exact)
		{
			Snapshot.Count = static_cast<int32>(Object->GetIntegerField(TEXT("count")));
		}
		else if (Snapshot.RuleKind == ELayoutSparsePlacementRuleKind::Range)
		{
			Snapshot.MinCount = static_cast<int32>(Object->GetIntegerField(TEXT("min_count")));
			Snapshot.MaxCount = static_cast<int32>(Object->GetIntegerField(TEXT("max_count")));
		}
		const TArray<TSharedPtr<FJsonValue>>* JsonValidationMessages = nullptr;
		Object->TryGetArrayField(TEXT("validation_messages"), JsonValidationMessages);
		Snapshot.Validation.Messages = JsonToValidationMessages(JsonValidationMessages, Issues, FieldPath + TEXT(".validation_messages"));
		const TArray<TSharedPtr<FJsonValue>>* JsonProofRecords = nullptr;
		Object->TryGetArrayField(TEXT("proof_records"), JsonProofRecords);
		Snapshot.ProofRecords = JsonToProofRecords(JsonProofRecords, Issues, FieldPath + TEXT(".proof_records"));
		const TArray<TSharedPtr<FJsonValue>>* JsonAssertions = nullptr;
		Object->TryGetArrayField(TEXT("validation_assertions"), JsonAssertions);
		Snapshot.ValidationAssertions = JsonToValidationAssertions(JsonAssertions, Issues, FieldPath + TEXT(".validation_assertions"));
		return Snapshot;
	}

	TSharedPtr<FJsonObject> ProfileSnapshotToJson(const FLayoutProfileSolveSnapshot& Snapshot)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("snapshot_id"), Snapshot.SnapshotId.ToString());
		Object->SetNumberField(TEXT("snapshot_schema_version"), Snapshot.SnapshotSchemaVersion);
		Object->SetStringField(TEXT("debug_name"), Snapshot.DebugName.ToString());
		Object->SetObjectField(TEXT("minimum_footprint_in_cells"), PointToJson(Snapshot.MinimumFootprintInCells));
		Object->SetObjectField(TEXT("maximum_footprint_in_cells"), PointToJson(Snapshot.MaximumFootprintInCells));
		Object->SetNumberField(TEXT("level_count"), Snapshot.LevelCount);
		Object->SetStringField(TEXT("entry_count_mode"), EnumToString(Snapshot.EntryCountMode));
		Object->SetNumberField(TEXT("entry_count"), Snapshot.EntryCount);
		Object->SetNumberField(TEXT("min_entry_count"), Snapshot.MinEntryCount);
		Object->SetNumberField(TEXT("max_entry_count"), Snapshot.MaxEntryCount);
		Object->SetStringField(TEXT("vertical_access_count_mode"), EnumToString(Snapshot.VerticalAccessCountMode));
		Object->SetNumberField(TEXT("vertical_access_count"), Snapshot.VerticalAccessCount);
		Object->SetNumberField(TEXT("min_vertical_access_count"), Snapshot.MinVerticalAccessCount);
		Object->SetNumberField(TEXT("max_vertical_access_count"), Snapshot.MaxVerticalAccessCount);
		Object->SetBoolField(TEXT("restrict_vertical_access_modules_to_vertical_access_cells"), Snapshot.bRestrictVerticalAccessModulesToVerticalAccessCells);
		Object->SetArrayField(TEXT("closure_requirements"), ClosureRequirementsToJson(Snapshot.ClosureRequirements));
		Object->SetArrayField(TEXT("zone_feature_requirements"), ZoneFeatureRequirementsToJson(Snapshot.ZoneFeatureRequirements));
		Object->SetArrayField(TEXT("level_fill_rules"), LevelFillRulesToJson(Snapshot.LevelFillRules));
		Object->SetArrayField(TEXT("reserved_open_space_rules"), ReservedOpenSpaceRulesToJson(Snapshot.ReservedOpenSpaceRules));
		Object->SetBoolField(TEXT("require_all_traversal_channels_reachable"), Snapshot.bRequireAllTraversalChannelsReachable);
		Object->SetBoolField(TEXT("supports_stepped_terrain_solve"), Snapshot.bSupportsSteppedTerrainSolve);
		Object->SetBoolField(TEXT("enable_terrain_seams"), Snapshot.bEnableTerrainSeams);
		Object->SetArrayField(TEXT("sparse_placement_rules"), SparsePlacementRuleSnapshotsToJson(Snapshot.SparsePlacementRules));
		Object->SetArrayField(TEXT("validation_messages"), ValidationMessagesToJson(Snapshot.Validation.Messages));
		Object->SetArrayField(TEXT("proof_records"), ProofRecordsToJson(Snapshot.ProofRecords));
		Object->SetArrayField(TEXT("validation_assertions"), ValidationAssertionsToJson(Snapshot.ValidationAssertions));
		return Object;
	}

	FLayoutProfileSolveSnapshot JsonToProfileSnapshot(
		const TSharedPtr<FJsonObject>& Object,
		const TObjectPtr<ULayoutProfileAsset> SourceProfile,
		TArray<FString>& Issues,
		const FString& FieldPath)
	{
		FLayoutProfileSolveSnapshot Snapshot;
		if (!Object.IsValid())
		{
			Issues.Add(FString::Printf(TEXT("Invalid profile snapshot object in %s."), *FieldPath));
			return Snapshot;
		}

		Snapshot.SnapshotId = FLayoutId(*Object->GetStringField(TEXT("snapshot_id")));
		Snapshot.SnapshotSchemaVersion = static_cast<int32>(Object->GetIntegerField(TEXT("snapshot_schema_version")));
		Snapshot.SourceProfile = SourceProfile;
		Snapshot.DebugName = FName(*Object->GetStringField(TEXT("debug_name")));
		Snapshot.MinimumFootprintInCells = JsonToPoint(Object->GetObjectField(TEXT("minimum_footprint_in_cells")));
		Snapshot.MaximumFootprintInCells = JsonToPoint(Object->GetObjectField(TEXT("maximum_footprint_in_cells")));
		Snapshot.LevelCount = static_cast<int32>(Object->GetIntegerField(TEXT("level_count")));
		Snapshot.EntryCountMode = StringToEnum(Object->GetStringField(TEXT("entry_count_mode")), ELayoutCountConstraintMode::None, Issues, TEXT("entry_count_mode"));
		Snapshot.EntryCount = static_cast<int32>(Object->GetIntegerField(TEXT("entry_count")));
		Snapshot.MinEntryCount = static_cast<int32>(Object->GetIntegerField(TEXT("min_entry_count")));
		Snapshot.MaxEntryCount = static_cast<int32>(Object->GetIntegerField(TEXT("max_entry_count")));
		Snapshot.VerticalAccessCountMode = StringToEnum(Object->GetStringField(TEXT("vertical_access_count_mode")), ELayoutCountConstraintMode::None, Issues, TEXT("vertical_access_count_mode"));
		Snapshot.VerticalAccessCount = static_cast<int32>(Object->GetIntegerField(TEXT("vertical_access_count")));
		Snapshot.MinVerticalAccessCount = static_cast<int32>(Object->GetIntegerField(TEXT("min_vertical_access_count")));
		Snapshot.MaxVerticalAccessCount = static_cast<int32>(Object->GetIntegerField(TEXT("max_vertical_access_count")));
		Snapshot.bRestrictVerticalAccessModulesToVerticalAccessCells = Object->GetBoolField(TEXT("restrict_vertical_access_modules_to_vertical_access_cells"));
		const TArray<TSharedPtr<FJsonValue>>* JsonClosureRequirements = nullptr;
		Object->TryGetArrayField(TEXT("closure_requirements"), JsonClosureRequirements);
		Snapshot.ClosureRequirements = JsonToClosureRequirements(JsonClosureRequirements, Issues, FieldPath + TEXT(".closure_requirements"));
		const TArray<TSharedPtr<FJsonValue>>* JsonZoneFeatureRequirements = nullptr;
		Object->TryGetArrayField(TEXT("zone_feature_requirements"), JsonZoneFeatureRequirements);
		Snapshot.ZoneFeatureRequirements = JsonToZoneFeatureRequirements(JsonZoneFeatureRequirements, Issues, FieldPath + TEXT(".zone_feature_requirements"));
		const TArray<TSharedPtr<FJsonValue>>* JsonLevelFillRules = nullptr;
		Object->TryGetArrayField(TEXT("level_fill_rules"), JsonLevelFillRules);
		Snapshot.LevelFillRules = JsonToLevelFillRules(JsonLevelFillRules, Issues, FieldPath + TEXT(".level_fill_rules"));
		const TArray<TSharedPtr<FJsonValue>>* JsonReservedOpenSpaceRules = nullptr;
		Object->TryGetArrayField(TEXT("reserved_open_space_rules"), JsonReservedOpenSpaceRules);
		Snapshot.ReservedOpenSpaceRules = JsonToReservedOpenSpaceRules(JsonReservedOpenSpaceRules, Issues, FieldPath + TEXT(".reserved_open_space_rules"));
		Snapshot.bRequireAllTraversalChannelsReachable = Object->GetBoolField(TEXT("require_all_traversal_channels_reachable"));
		Object->TryGetBoolField(TEXT("supports_stepped_terrain_solve"), Snapshot.bSupportsSteppedTerrainSolve);
		Object->TryGetBoolField(TEXT("enable_terrain_seams"), Snapshot.bEnableTerrainSeams);
		const TArray<TSharedPtr<FJsonValue>>* JsonSparsePlacementRules = nullptr;
		Object->TryGetArrayField(TEXT("sparse_placement_rules"), JsonSparsePlacementRules);
		if (JsonSparsePlacementRules != nullptr)
		{
			for (int32 RuleIndex = 0; RuleIndex < JsonSparsePlacementRules->Num(); ++RuleIndex)
			{
				Snapshot.SparsePlacementRules.Add(JsonToSparsePlacementRuleSnapshot(
					(*JsonSparsePlacementRules)[RuleIndex].IsValid() ? (*JsonSparsePlacementRules)[RuleIndex]->AsObject() : nullptr,
					Issues,
					FString::Printf(TEXT("%s.sparse_placement_rules[%d]"), *FieldPath, RuleIndex)));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* JsonValidationMessages = nullptr;
		Object->TryGetArrayField(TEXT("validation_messages"), JsonValidationMessages);
		Snapshot.Validation.Messages = JsonToValidationMessages(JsonValidationMessages, Issues, FieldPath + TEXT(".validation_messages"));
		const TArray<TSharedPtr<FJsonValue>>* JsonProofRecords = nullptr;
		Object->TryGetArrayField(TEXT("proof_records"), JsonProofRecords);
		Snapshot.ProofRecords = JsonToProofRecords(JsonProofRecords, Issues, FieldPath + TEXT(".proof_records"));
		const TArray<TSharedPtr<FJsonValue>>* JsonAssertions = nullptr;
		Object->TryGetArrayField(TEXT("validation_assertions"), JsonAssertions);
		Snapshot.ValidationAssertions = JsonToValidationAssertions(JsonAssertions, Issues, FieldPath + TEXT(".validation_assertions"));
		return Snapshot;
	}

	TSharedPtr<FJsonObject> ChildRequestTemplateSnapshotToJson(const FLayoutChildRequestTemplateSnapshot& Snapshot)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("effective_snapshot_id"), Snapshot.EffectiveSnapshotId.ToString());
		Object->SetNumberField(TEXT("snapshot_schema_version"), Snapshot.SnapshotSchemaVersion);
		Object->SetNumberField(TEXT("seed"), Snapshot.Seed);
		Object->SetObjectField(TEXT("module_set_snapshot"), ModuleCatalogToJson(Snapshot.ModuleCatalog));
		Object->SetObjectField(TEXT("content_set_snapshot"), ContentSetSnapshotToJson(Snapshot.ContentSetSnapshot));
		Object->SetObjectField(TEXT("profile_snapshot"), ProfileSnapshotToJson(Snapshot.ProfileSnapshot));
		Object->SetArrayField(TEXT("proof_records"), ProofRecordsToJson(Snapshot.ProofRecords));
		Object->SetArrayField(TEXT("validation_assertions"), ValidationAssertionsToJson(Snapshot.ValidationAssertions));
		return Object;
	}

	TSharedPtr<FLayoutChildRequestTemplateSnapshot> JsonToChildRequestTemplateSnapshot(
		const TSharedPtr<FJsonObject>& Object,
		TArray<FString>& Issues,
		const FString& FieldPath)
	{
		if (!Object.IsValid())
		{
			Issues.Add(FString::Printf(TEXT("Invalid child request template snapshot object in %s."), *FieldPath));
			return nullptr;
		}

		TSharedPtr<FLayoutChildRequestTemplateSnapshot> Snapshot = MakeShared<FLayoutChildRequestTemplateSnapshot>();
		Snapshot->EffectiveSnapshotId = FLayoutId(*Object->GetStringField(TEXT("effective_snapshot_id")));
		Snapshot->SnapshotSchemaVersion = static_cast<int32>(Object->GetIntegerField(TEXT("snapshot_schema_version")));
		Snapshot->Seed = static_cast<int32>(Object->GetIntegerField(TEXT("seed")));

		const TSharedPtr<FJsonObject>* ModuleCatalogObject = nullptr;
		if (Object->TryGetObjectField(TEXT("module_set_snapshot"), ModuleCatalogObject) && ModuleCatalogObject != nullptr && (*ModuleCatalogObject).IsValid())
		{
			TMap<FName, TObjectPtr<ULayoutModuleAsset>> EmptyModulesBySnapshotId;
			TMap<FName, TObjectPtr<ULayoutCompositeModuleAsset>> EmptyCompositeModulesBySnapshotId;
			Snapshot->ModuleCatalog = JsonToModuleCatalog(
				*ModuleCatalogObject,
				EmptyModulesBySnapshotId,
				EmptyCompositeModulesBySnapshotId,
				Issues,
				FieldPath + TEXT(".module_set_snapshot"));
		}
		else
		{
			Issues.Add(FString::Printf(TEXT("Missing module_set_snapshot object in %s."), *FieldPath));
		}

		const TSharedPtr<FJsonObject>* ContentSetSnapshotObject = nullptr;
		if (Object->TryGetObjectField(TEXT("content_set_snapshot"), ContentSetSnapshotObject) && ContentSetSnapshotObject != nullptr && (*ContentSetSnapshotObject).IsValid())
		{
			Snapshot->ContentSetSnapshot = JsonToContentSetSnapshot(*ContentSetSnapshotObject, nullptr, Issues, FieldPath + TEXT(".content_set_snapshot"));
		}
		else
		{
			Issues.Add(FString::Printf(TEXT("Missing content_set_snapshot object in %s."), *FieldPath));
		}

		const TSharedPtr<FJsonObject>* ProfileSnapshotObject = nullptr;
		if (Object->TryGetObjectField(TEXT("profile_snapshot"), ProfileSnapshotObject) && ProfileSnapshotObject != nullptr && (*ProfileSnapshotObject).IsValid())
		{
			Snapshot->ProfileSnapshot = JsonToProfileSnapshot(*ProfileSnapshotObject, nullptr, Issues, FieldPath + TEXT(".profile_snapshot"));
		}
		else
		{
			Issues.Add(FString::Printf(TEXT("Missing profile_snapshot object in %s."), *FieldPath));
		}

		const TArray<TSharedPtr<FJsonValue>>* JsonProofRecords = nullptr;
		Object->TryGetArrayField(TEXT("proof_records"), JsonProofRecords);
		Snapshot->ProofRecords = JsonToProofRecords(JsonProofRecords, Issues, FieldPath + TEXT(".proof_records"));
		const TArray<TSharedPtr<FJsonValue>>* JsonAssertions = nullptr;
		Object->TryGetArrayField(TEXT("validation_assertions"), JsonAssertions);
		Snapshot->ValidationAssertions = JsonToValidationAssertions(JsonAssertions, Issues, FieldPath + TEXT(".validation_assertions"));
		return Snapshot;
	}

			ULayoutRegionContentSetAsset* RebuildContentSetFromSnapshot(
			UObject* AssetOuter,
			const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot,
			TArray<FString>& OutIssues)
		{
			if (ContentSetSnapshot.SnapshotId == NAME_None)
			{
				return nullptr;
			}

			ULayoutRegionContentSetAsset* ContentSet = NewObject<ULayoutRegionContentSetAsset>(
				AssetOuter,
				MakeUniqueObjectName(AssetOuter, ULayoutRegionContentSetAsset::StaticClass(), ContentSetSnapshot.DebugName.IsNone() ? FName(*ContentSetSnapshot.SnapshotId.ToString()) : ContentSetSnapshot.DebugName));

			TMap<FLayoutId, TObjectPtr<ULayoutProfileAsset>> ChildProfilesBySnapshotId;
			for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : ContentSetSnapshot.Entries)
			{
			FLayoutRegionContentEntry& Entry = ContentSet->Entries.AddDefaulted_GetRef();
			Entry.EntryId = EntrySnapshot.EntryId;
			Entry.ContentKind = EntrySnapshot.ContentKind;
			Entry.Weight = EntrySnapshot.Weight;
			Entry.ProvidedZoneFeatures = EntrySnapshot.ProvidedZoneFeatures;
			Entry.ClosureProviderIntents = EntrySnapshot.ClosureProviderIntents;
			Entry.SeamProviderIntents = EntrySnapshot.SeamProviderIntents;

			if (EntrySnapshot.ContentKind == ELayoutRegionContentKind::Module)
			{
				Entry.ModuleSettings.PlacementZone = EntrySnapshot.ModulePlacementZone;
				Entry.ModuleSettings.LevelPlacementPolicy = EntrySnapshot.ModuleLevelPlacementPolicy;
				Entry.ModuleSettings.SpecificLevel = FMath::Max(0, EntrySnapshot.ModuleSpecificLevel);
				Entry.ModuleSettings.bOptional = EntrySnapshot.bModuleOptional;
				{
					OutIssues.Add(FString::Printf(
						TEXT("Content-set snapshot entry '%s' references invalid module snapshot index %d."),
						*EntrySnapshot.EntryId.ToString(),
						EntrySnapshot.ModuleSnapshotIndex));
				}
			}
			else
			{
				TObjectPtr<ULayoutProfileAsset>* ExistingChildProfile = ChildProfilesBySnapshotId.Find(EntrySnapshot.ChildProfileSnapshotId);
				if (ExistingChildProfile == nullptr)
				{
					ULayoutProfileAsset* ChildProfile = NewObject<ULayoutProfileAsset>(
						AssetOuter,
						MakeUniqueObjectName(AssetOuter, ULayoutProfileAsset::StaticClass(), EntrySnapshot.ChildProfileSnapshotId.IsNone() ? FName(TEXT("ImportedChildProfile")) : FName(*EntrySnapshot.ChildProfileSnapshotId.ToString())));
					ChildProfilesBySnapshotId.Add(EntrySnapshot.ChildProfileSnapshotId, ChildProfile);
					ExistingChildProfile = ChildProfilesBySnapshotId.Find(EntrySnapshot.ChildProfileSnapshotId);
				}

				Entry.ChildRegionSettings.RegionProfile = ExistingChildProfile != nullptr ? ExistingChildProfile->Get() : nullptr;
				Entry.ChildRegionSettings.PlacementZone = EntrySnapshot.ChildPlacementZone;
				Entry.ChildRegionSettings.LevelPlacementPolicy = EntrySnapshot.ChildLevelPlacementPolicy;
				Entry.ChildRegionSettings.SpecificLevel = FMath::Max(0, EntrySnapshot.ChildSpecificLevel);
				Entry.ChildRegionSettings.bOptional = EntrySnapshot.bChildOptional;
			}
		}

		return ContentSet;
	}

	bool ImportRecursiveContentSetFixture(
		const TSharedPtr<FJsonObject>& Root,
		UObject* Outer,
		FLayoutProfileJsonFixtureAssets& OutAssets,
		TArray<FString>& OutIssues)
	{
		UObject* AssetOuter = Outer != nullptr ? Outer : GetTransientPackage();
		const TSharedPtr<FJsonObject>* RootObject = nullptr;
		if (!Root->TryGetObjectField(TEXT("root"), RootObject) || RootObject == nullptr || !(*RootObject).IsValid())
		{
			OutIssues.Add(TEXT("Recursive layout fixture JSON is missing a root object."));
			return false;
		}

		const FName RootProfileId(*(*RootObject)->GetStringField(TEXT("profile_id")));
		const FName RootContentSetId(*(*RootObject)->GetStringField(TEXT("content_set_id")));

		const TSharedPtr<FJsonObject>* LibrariesObject = nullptr;
		if (!Root->TryGetObjectField(TEXT("libraries"), LibrariesObject) || LibrariesObject == nullptr || !(*LibrariesObject).IsValid())
		{
			OutIssues.Add(TEXT("Recursive layout fixture JSON is missing libraries."));
			return false;
		}

		const TSharedPtr<FJsonObject>* CompiledSnapshotForTemplateSizeObject = nullptr;
		const TMap<FString, FIntVector> TemplateSizesByIdentity = Root->TryGetObjectField(TEXT("compiled_snapshot"), CompiledSnapshotForTemplateSizeObject)
			&& CompiledSnapshotForTemplateSizeObject != nullptr
			&& (*CompiledSnapshotForTemplateSizeObject).IsValid()
				? BuildFixtureTemplateSizesByModuleIdentityFromCompiledSnapshot(*CompiledSnapshotForTemplateSizeObject)
				: TMap<FString, FIntVector>();
		const TMap<FString, FIntVector> ContentSetSharedCellSizesByIdentity = Root->TryGetObjectField(TEXT("compiled_snapshot"), CompiledSnapshotForTemplateSizeObject)
			&& CompiledSnapshotForTemplateSizeObject != nullptr
			&& (*CompiledSnapshotForTemplateSizeObject).IsValid()
				? BuildFixtureSharedCellSizesByIdentityFromCompiledSnapshot(*CompiledSnapshotForTemplateSizeObject)
				: TMap<FString, FIntVector>();
		TMap<FString, FIntVector> ImportedContentSetSharedCellSizesByIdentity = ContentSetSharedCellSizesByIdentity;
		// Composite-only leaves may be absent from the root compiled module snapshot.
		// Recursive fixture content sets share one frozen cell size, so use it for those leaves.
		const FIntVector FallbackTemplateSize =
			FindFirstPositiveFixtureVector(ContentSetSharedCellSizesByIdentity);

		TMap<FName, TObjectPtr<ULayoutModuleAsset>> ModulesById;
		const TArray<TSharedPtr<FJsonValue>>* JsonModules = nullptr;
		(*LibrariesObject)->TryGetArrayField(TEXT("modules"), JsonModules);
		if (JsonModules != nullptr)
		{
			for (int32 ModuleIndex = 0; ModuleIndex < JsonModules->Num(); ++ModuleIndex)
			{
				const TSharedPtr<FJsonObject> ModuleObject = (*JsonModules)[ModuleIndex].IsValid() ? (*JsonModules)[ModuleIndex]->AsObject() : nullptr;
				if (!ModuleObject.IsValid())
				{
					OutIssues.Add(FString::Printf(TEXT("Invalid module authoring object at libraries.modules[%d]."), ModuleIndex));
					continue;
				}

				const FName ModuleId(*ModuleObject->GetStringField(TEXT("id")));
				ULayoutModuleAsset* Module = RebuildModuleFromAuthoringObject(
					ModuleObject,
					AssetOuter,
					TemplateSizesByIdentity,
					FallbackTemplateSize,
					OutIssues,
					FString::Printf(TEXT("libraries.modules[%d]"), ModuleIndex));
				if (Module != nullptr)
				{
					ModulesById.Add(ModuleId, Module);
					ModulesById.Add(Module->GetFName(), Module);
				}
			}
		}

		TMap<FName, TObjectPtr<ULayoutCompositeModuleAsset>> CompositeModulesById;
		const TArray<TSharedPtr<FJsonValue>>* JsonCompositeModules = nullptr;
		(*LibrariesObject)->TryGetArrayField(TEXT("composite_modules"), JsonCompositeModules);
		if (JsonCompositeModules != nullptr)
		{
			for (int32 CompositeIndex = 0; CompositeIndex < JsonCompositeModules->Num(); ++CompositeIndex)
			{
				const TSharedPtr<FJsonObject> CompositeObject = (*JsonCompositeModules)[CompositeIndex].IsValid() ? (*JsonCompositeModules)[CompositeIndex]->AsObject() : nullptr;
				if (!CompositeObject.IsValid())
				{
					OutIssues.Add(FString::Printf(TEXT("Invalid composite-module authoring object at libraries.composite_modules[%d]."), CompositeIndex));
					continue;
				}

				const FName CompositeId(*CompositeObject->GetStringField(TEXT("id")));
				ULayoutCompositeModuleAsset* CompositeModule = RebuildCompositeModuleFromAuthoringObject(
					CompositeObject,
					AssetOuter,
					ModulesById,
					OutIssues,
					FString::Printf(TEXT("libraries.composite_modules[%d]"), CompositeIndex));
				if (CompositeModule != nullptr)
				{
					CompositeModulesById.Add(CompositeId, CompositeModule);
					CompositeModulesById.Add(CompositeModule->GetFName(), CompositeModule);
				}
			}
		}

		TMap<FName, TObjectPtr<ULayoutRegionContentSetAsset>> ContentSetsById;
		TMap<FName, TSharedPtr<FJsonObject>> ContentSetObjectsById;
		const TArray<TSharedPtr<FJsonValue>>* JsonContentSets = nullptr;
		(*LibrariesObject)->TryGetArrayField(TEXT("content_sets"), JsonContentSets);
		if (JsonContentSets != nullptr)
		{
			for (int32 ContentSetIndex = 0; ContentSetIndex < JsonContentSets->Num(); ++ContentSetIndex)
			{
				const TSharedPtr<FJsonObject> ContentSetObject = (*JsonContentSets)[ContentSetIndex].IsValid() ? (*JsonContentSets)[ContentSetIndex]->AsObject() : nullptr;
				if (!ContentSetObject.IsValid())
				{
					OutIssues.Add(FString::Printf(TEXT("Invalid content-set authoring object at libraries.content_sets[%d]."), ContentSetIndex));
					continue;
				}

				const FName ContentSetId(*ContentSetObject->GetStringField(TEXT("id")));
				const FString ContentSetName = ContentSetObject->GetStringField(TEXT("name"));
				ULayoutRegionContentSetAsset* ContentSet = NewObject<ULayoutRegionContentSetAsset>(
					AssetOuter,
					MakeUniqueObjectName(AssetOuter, ULayoutRegionContentSetAsset::StaticClass(), FName(*(ContentSetName.IsEmpty() ? ContentSetId.ToString() : ContentSetName))));
				if (ContentSetObject->HasField(TEXT("shared_cell_size_in_blocks")))
				{
					const FIntVector ImportedSharedCellSize =
						JsonToVector(ContentSetObject->GetObjectField(TEXT("shared_cell_size_in_blocks")));
					if (HasPositiveFixtureSharedCellSize(ImportedSharedCellSize))
					{
						ImportedContentSetSharedCellSizesByIdentity.Add(ContentSetId.ToString(), ImportedSharedCellSize);
						if (!ContentSetName.IsEmpty())
						{
							ImportedContentSetSharedCellSizesByIdentity.Add(ContentSetName, ImportedSharedCellSize);
						}
					}
				}
				ContentSetsById.Add(ContentSetId, ContentSet);
				ContentSetObjectsById.Add(ContentSetId, ContentSetObject);
			}
		}

		TMap<FName, FImportedProfileAuthoring> ImportedProfilesById;
		const TArray<TSharedPtr<FJsonValue>>* JsonProfiles = nullptr;
		(*LibrariesObject)->TryGetArrayField(TEXT("profiles"), JsonProfiles);
		if (JsonProfiles != nullptr)
		{
			for (int32 ProfileIndex = 0; ProfileIndex < JsonProfiles->Num(); ++ProfileIndex)
			{
				const TSharedPtr<FJsonObject> ProfileObject = (*JsonProfiles)[ProfileIndex].IsValid() ? (*JsonProfiles)[ProfileIndex]->AsObject() : nullptr;
				if (!ProfileObject.IsValid())
				{
					OutIssues.Add(FString::Printf(TEXT("Invalid profile authoring object at libraries.profiles[%d]."), ProfileIndex));
					continue;
				}

				FImportedProfileAuthoring ImportedProfile;
				const FName ProfileId(*ProfileObject->GetStringField(TEXT("id")));
				const FString ProfileName = ProfileObject->GetStringField(TEXT("name"));
				ImportedProfile.Profile = NewObject<ULayoutProfileAsset>(
					AssetOuter,
					MakeUniqueObjectName(AssetOuter, ULayoutProfileAsset::StaticClass(), FName(*(ProfileName.IsEmpty() ? ProfileId.ToString() : ProfileName))));
				ImportedProfile.ContentSetId = FName(*ProfileObject->GetStringField(TEXT("content_set_id")));

				ULayoutProfileAsset* Profile = ImportedProfile.Profile;
				Profile->MinimumFootprintInCells = JsonToPoint(ProfileObject->GetObjectField(TEXT("minimum_footprint_in_cells")));
				Profile->MaximumFootprintInCells = JsonToPoint(ProfileObject->GetObjectField(TEXT("maximum_footprint_in_cells")));
				Profile->LevelCount = static_cast<int32>(ProfileObject->GetIntegerField(TEXT("level_count")));
				Profile->EntryCountMode = StringToEnum(ProfileObject->GetStringField(TEXT("entry_count_mode")), ELayoutCountConstraintMode::None, OutIssues, TEXT("entry_count_mode"));
				Profile->EntryCount = static_cast<int32>(ProfileObject->GetIntegerField(TEXT("entry_count")));
				Profile->MinEntryCount = static_cast<int32>(ProfileObject->GetIntegerField(TEXT("min_entry_count")));
				Profile->MaxEntryCount = static_cast<int32>(ProfileObject->GetIntegerField(TEXT("max_entry_count")));
				Profile->VerticalAccessCountMode = StringToEnum(ProfileObject->GetStringField(TEXT("vertical_access_count_mode")), ELayoutCountConstraintMode::None, OutIssues, TEXT("vertical_access_count_mode"));
				Profile->VerticalAccessCount = static_cast<int32>(ProfileObject->GetIntegerField(TEXT("vertical_access_count")));
				Profile->MinVerticalAccessCount = static_cast<int32>(ProfileObject->GetIntegerField(TEXT("min_vertical_access_count")));
				Profile->MaxVerticalAccessCount = static_cast<int32>(ProfileObject->GetIntegerField(TEXT("max_vertical_access_count")));
				Profile->bRestrictVerticalAccessModulesToVerticalAccessCells = ProfileObject->GetBoolField(TEXT("restrict_vertical_access_modules_to_vertical_access_cells"));
				const TArray<TSharedPtr<FJsonValue>>* JsonClosureRequirements = nullptr;
				ProfileObject->TryGetArrayField(TEXT("closure_requirements"), JsonClosureRequirements);
				Profile->ClosureRequirements = JsonToClosureRequirements(JsonClosureRequirements, OutIssues, FString::Printf(TEXT("libraries.profiles[%d].closure_requirements"), ProfileIndex));
				const TArray<TSharedPtr<FJsonValue>>* JsonZoneFeatureRequirements = nullptr;
				ProfileObject->TryGetArrayField(TEXT("zone_feature_requirements"), JsonZoneFeatureRequirements);
				Profile->ZoneFeatureRequirements = JsonToZoneFeatureRequirements(JsonZoneFeatureRequirements, OutIssues, FString::Printf(TEXT("libraries.profiles[%d].zone_feature_requirements"), ProfileIndex));
				const TArray<TSharedPtr<FJsonValue>>* JsonLevelFillRules = nullptr;
				ProfileObject->TryGetArrayField(TEXT("level_fill_rules"), JsonLevelFillRules);
				Profile->LevelFillRules = JsonToLevelFillRules(JsonLevelFillRules, OutIssues, FString::Printf(TEXT("libraries.profiles[%d].level_fill_rules"), ProfileIndex));
				const TArray<TSharedPtr<FJsonValue>>* JsonReservedOpenSpaceRules = nullptr;
				ProfileObject->TryGetArrayField(TEXT("reserved_open_space_rules"), JsonReservedOpenSpaceRules);
				Profile->ReservedOpenSpaceRules = JsonToReservedOpenSpaceRules(JsonReservedOpenSpaceRules, OutIssues, FString::Printf(TEXT("libraries.profiles[%d].reserved_open_space_rules"), ProfileIndex));
				if (!ProfileObject->TryGetBoolField(TEXT("require_all_traversal_channels_reachable"), Profile->bRequireAllTraversalChannelsReachable))
				{
					Profile->bRequireAllTraversalChannelsReachable = ProfileObject->GetBoolField(TEXT("require_all_walkable_areas_reachable"));
				}
				ProfileObject->TryGetBoolField(TEXT("supports_stepped_terrain_solve"), Profile->bSupportsSteppedTerrainSolve);
				ProfileObject->TryGetBoolField(TEXT("enable_terrain_seams"), Profile->bEnableTerrainSeams);

				const TArray<TSharedPtr<FJsonValue>>* JsonSparsePlacementRules = nullptr;
				ProfileObject->TryGetArrayField(TEXT("sparse_placement_rules"), JsonSparsePlacementRules);
				ImportedProfile.SparseRules = JsonToSparsePlacementRules(
					JsonSparsePlacementRules,
					OutIssues,
					FString::Printf(TEXT("libraries.profiles[%d].sparse_placement_rules"), ProfileIndex));

				ImportedProfilesById.Add(ProfileId, ImportedProfile);
			}
		}

		for (const TPair<FName, TSharedPtr<FJsonObject>>& Pair : ContentSetObjectsById)
		{
			ULayoutRegionContentSetAsset* const ContentSet = ContentSetsById.FindRef(Pair.Key);
			const TSharedPtr<FJsonObject>& ContentSetObject = Pair.Value;
			if (ContentSet == nullptr || !ContentSetObject.IsValid())
			{
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* JsonEntries = nullptr;
			ContentSetObject->TryGetArrayField(TEXT("entries"), JsonEntries);
			if (JsonEntries == nullptr)
			{
				continue;
			}

			for (int32 EntryIndex = 0; EntryIndex < JsonEntries->Num(); ++EntryIndex)
			{
				const TSharedPtr<FJsonObject> EntryObject = (*JsonEntries)[EntryIndex].IsValid() ? (*JsonEntries)[EntryIndex]->AsObject() : nullptr;
				if (!EntryObject.IsValid())
				{
					OutIssues.Add(FString::Printf(TEXT("Invalid content-set entry object at libraries.content_sets[%s].entries[%d]."), *Pair.Key.ToString(), EntryIndex));
					continue;
				}

				FLayoutRegionContentEntry& Entry = ContentSet->Entries.AddDefaulted_GetRef();
				Entry.EntryId = FName(*EntryObject->GetStringField(TEXT("entry_id")));
				Entry.ContentKind = StringToEnum(EntryObject->GetStringField(TEXT("content_kind")), ELayoutRegionContentKind::Module, OutIssues, TEXT("content_kind"));
				Entry.Weight = FMath::Max(1, static_cast<int32>(EntryObject->GetIntegerField(TEXT("weight"))));
				const TArray<TSharedPtr<FJsonValue>>* JsonProvidedZoneFeatures = nullptr;
				EntryObject->TryGetArrayField(TEXT("provided_zone_features"), JsonProvidedZoneFeatures);
				Entry.ProvidedZoneFeatures = JsonToTags(JsonProvidedZoneFeatures, OutIssues, FString::Printf(TEXT("libraries.content_sets[%s].entries[%d].provided_zone_features"), *Pair.Key.ToString(), EntryIndex));
				const TArray<TSharedPtr<FJsonValue>>* JsonClosureProviderIntents = nullptr;
				EntryObject->TryGetArrayField(TEXT("closure_provider_intents"), JsonClosureProviderIntents);
				Entry.ClosureProviderIntents = JsonToClosureProviderIntents(JsonClosureProviderIntents, OutIssues, FString::Printf(TEXT("libraries.content_sets[%s].entries[%d].closure_provider_intents"), *Pair.Key.ToString(), EntryIndex));
				const TArray<TSharedPtr<FJsonValue>>* JsonSeamProviderIntents = nullptr;
				EntryObject->TryGetArrayField(TEXT("seam_provider_intents"), JsonSeamProviderIntents);
				Entry.SeamProviderIntents = JsonToSeamProviderIntents(JsonSeamProviderIntents, OutIssues, FString::Printf(TEXT("libraries.content_sets[%s].entries[%d].seam_provider_intents"), *Pair.Key.ToString(), EntryIndex));

				if (Entry.ContentKind == ELayoutRegionContentKind::Module)
				{
					FString ModuleIdString;
					EntryObject->TryGetStringField(TEXT("module_id"), ModuleIdString);
					const FName ModuleId(*ModuleIdString);
					if (!ModuleId.IsNone())
					{
						if (const TObjectPtr<ULayoutModuleAsset>* Module = ModulesById.Find(ModuleId))
						{
							Entry.ModuleSettings.Module = *Module;
						}
						else
						{
							OutIssues.Add(FString::Printf(TEXT("Content-set '%s' references unknown module id '%s'."), *Pair.Key.ToString(), *ModuleId.ToString()));
						}
					}

					FString CompositeModuleIdString;
					EntryObject->TryGetStringField(TEXT("composite_module_id"), CompositeModuleIdString);
					const FName CompositeModuleId(*CompositeModuleIdString);
					if (!CompositeModuleId.IsNone())
					{
						if (const TObjectPtr<ULayoutCompositeModuleAsset>* CompositeModule = CompositeModulesById.Find(CompositeModuleId))
						{
							Entry.ModuleSettings.CompositeModule = *CompositeModule;
						}
						else
						{
							OutIssues.Add(FString::Printf(TEXT("Content-set '%s' references unknown composite module id '%s'."), *Pair.Key.ToString(), *CompositeModuleId.ToString()));
						}
					}

					if (EntryObject->HasField(TEXT("module_placement_zone")))
					{
						Entry.ModuleSettings.PlacementZone = StringToEnum(
							EntryObject->GetStringField(TEXT("module_placement_zone")),
							ELayoutPlacementZone::Any,
							OutIssues,
							TEXT("module_placement_zone"));
					}
					if (EntryObject->HasField(TEXT("module_level_placement_policy")))
					{
						Entry.ModuleSettings.LevelPlacementPolicy = StringToEnum(
							EntryObject->GetStringField(TEXT("module_level_placement_policy")),
							ELayoutLevelPlacementPolicy::AnyLevel,
							OutIssues,
							TEXT("module_level_placement_policy"));
					}
					if (EntryObject->HasField(TEXT("module_specific_level")))
					{
						Entry.ModuleSettings.SpecificLevel = static_cast<int32>(EntryObject->GetIntegerField(TEXT("module_specific_level")));
					}
					EntryObject->TryGetBoolField(TEXT("module_optional"), Entry.ModuleSettings.bOptional);
				}
				else
				{
					const FName ChildProfileId(*EntryObject->GetStringField(TEXT("child_profile_id")));
					if (const FImportedProfileAuthoring* ChildProfile = ImportedProfilesById.Find(ChildProfileId))
					{
						Entry.ChildRegionSettings.RegionProfile = ChildProfile->Profile;
					}
					else
					{
						OutIssues.Add(FString::Printf(TEXT("Content-set '%s' references unknown child profile id '%s'."), *Pair.Key.ToString(), *ChildProfileId.ToString()));
					}

					Entry.ChildRegionSettings.PlacementZone = StringToEnum(
						EntryObject->GetStringField(TEXT("child_placement_zone")),
						ELayoutPlacementZone::Any,
						OutIssues,
						TEXT("child_placement_zone"));
					if (EntryObject->HasField(TEXT("child_level_placement_policy")))
					{
						Entry.ChildRegionSettings.LevelPlacementPolicy = StringToEnum(
							EntryObject->GetStringField(TEXT("child_level_placement_policy")),
							ELayoutLevelPlacementPolicy::AnyLevel,
							OutIssues,
							TEXT("child_level_placement_policy"));
					}
					if (EntryObject->HasField(TEXT("child_specific_level")))
					{
						Entry.ChildRegionSettings.SpecificLevel = static_cast<int32>(EntryObject->GetIntegerField(TEXT("child_specific_level")));
					}
					EntryObject->TryGetBoolField(TEXT("child_optional"), Entry.ChildRegionSettings.bOptional);
					EntryObject->TryGetBoolField(TEXT("child_contributes_host_vertical_access"), Entry.ChildRegionSettings.bContributesHostVerticalAccess);
				}
			}
		}

		for (TPair<FName, FImportedProfileAuthoring>& Pair : ImportedProfilesById)
		{
			FImportedProfileAuthoring& ImportedProfile = Pair.Value;
			ULayoutProfileAsset* const Profile = ImportedProfile.Profile;
			if (Profile == nullptr)
			{
				continue;
			}

			if (const TObjectPtr<ULayoutRegionContentSetAsset>* ContentSet = ContentSetsById.Find(ImportedProfile.ContentSetId))
			{
				Profile->ContentSet = *ContentSet;
			}
			else if (!ImportedProfile.ContentSetId.IsNone())
			{
				OutIssues.Add(FString::Printf(TEXT("Profile '%s' references unknown content set id '%s'."), *Pair.Key.ToString(), *ImportedProfile.ContentSetId.ToString()));
			}

			Profile->SparsePlacementRules.Reset();
			for (const FImportedSparsePlacementRuleAuthoring& ImportedSparseRule : ImportedProfile.SparseRules)
			{
				FInstancedStruct& SparseRule = Profile->SparsePlacementRules.Add_GetRef(ImportedSparseRule.Rule);
				FLayoutSparseContentRuleBase* const ContentRule = SparseRule.GetMutablePtr<FLayoutSparseContentRuleBase>();
				const FLayoutSparsePlacementRuleBase* const BaseRule = SparseRule.GetPtr<FLayoutSparsePlacementRuleBase>();
				if (ContentRule == nullptr)
				{
					continue;
				}
				if (const TObjectPtr<ULayoutRegionContentSetAsset>* SparseContentSet = ContentSetsById.Find(ImportedSparseRule.ContentSetName))
				{
					ContentRule->ContentSet = *SparseContentSet;
				}
				else if (!ImportedSparseRule.ContentSetName.IsNone())
				{
					OutIssues.Add(FString::Printf(
						TEXT("Profile '%s' sparse placement rule '%s' references unknown content set id '%s'."),
						*Pair.Key.ToString(),
						BaseRule != nullptr ? *BaseRule->RuleId.ToString() : TEXT("<unsupported>"),
						*ImportedSparseRule.ContentSetName.ToString()));
				}
			}
		}

		NormalizeImportedRecursiveFixtureOneCellLeafContract(
			ContentSetsById,
			ImportedContentSetSharedCellSizesByIdentity,
			FindFirstPositiveFixtureVector(ImportedContentSetSharedCellSizesByIdentity));

		const FImportedProfileAuthoring* const RootProfile = ImportedProfilesById.Find(RootProfileId);
		const TObjectPtr<ULayoutRegionContentSetAsset>* const RootContentSet = ContentSetsById.Find(RootContentSetId);
		if (RootProfile == nullptr || RootProfile->Profile == nullptr)
		{
			OutIssues.Add(FString::Printf(TEXT("Recursive layout fixture root references unknown profile id '%s'."), *RootProfileId.ToString()));
			return false;
		}
		if (RootContentSet == nullptr || *RootContentSet == nullptr)
		{
			OutIssues.Add(FString::Printf(TEXT("Recursive layout fixture root references unknown content set id '%s'."), *RootContentSetId.ToString()));
			return false;
		}

		OutAssets.Profile = RootProfile->Profile;
		OutAssets.ContentSet = *RootContentSet;
		OutAssets.Profile->ContentSet = OutAssets.ContentSet;

		const TSharedPtr<FJsonObject>* CompiledSnapshotObject = nullptr;
		const bool bHasCompiledSnapshot = Root->TryGetObjectField(TEXT("compiled_snapshot"), CompiledSnapshotObject)
			&& CompiledSnapshotObject != nullptr
			&& (*CompiledSnapshotObject).IsValid();
		if (bHasCompiledSnapshot)
		{
			const TSharedPtr<FJsonObject>* ProfileSnapshotObject = nullptr;
			const TSharedPtr<FJsonObject>* ContentSetSnapshotObject = nullptr;
			const TSharedPtr<FJsonObject>* ModuleCatalogObject = nullptr;
			if ((*CompiledSnapshotObject)->TryGetObjectField(TEXT("profile_snapshot"), ProfileSnapshotObject) && ProfileSnapshotObject != nullptr && (*ProfileSnapshotObject).IsValid())
			{
				OutAssets.ProfileSnapshot = JsonToProfileSnapshot(*ProfileSnapshotObject, OutAssets.Profile, OutIssues, TEXT("compiled_snapshot.profile_snapshot"));
			}
			if ((*CompiledSnapshotObject)->TryGetObjectField(TEXT("content_set_snapshot"), ContentSetSnapshotObject) && ContentSetSnapshotObject != nullptr && (*ContentSetSnapshotObject).IsValid())
			{
				OutAssets.ContentSetSnapshot = JsonToContentSetSnapshot(*ContentSetSnapshotObject, OutAssets.ContentSet, OutIssues, TEXT("compiled_snapshot.content_set_snapshot"));
			}
			if ((*CompiledSnapshotObject)->TryGetObjectField(TEXT("module_set_snapshot"), ModuleCatalogObject) && ModuleCatalogObject != nullptr && (*ModuleCatalogObject).IsValid())
			{
				OutAssets.ModuleCatalog = JsonToModuleCatalog(*ModuleCatalogObject, ModulesById, CompositeModulesById, OutIssues, TEXT("compiled_snapshot.module_set_snapshot"));
			}
		}

		if (OutAssets.ProfileSnapshot.SnapshotId == NAME_None)
		{
			OutAssets.ProfileSnapshot = FLayoutProfileSolver::BuildProfileSnapshot(OutAssets.Profile);
		}
		if (OutAssets.ContentSetSnapshot.SnapshotId == NAME_None)
		{
			OutAssets.ContentSetSnapshot = FLayoutProfileSolver::BuildContentSetSnapshot(OutAssets.ContentSet);
		}
		if (OutAssets.ModuleCatalog.SnapshotId == NAME_None)
		{
			OutAssets.ModuleCatalog = FLayoutProfileSolver::BuildModuleCatalog(OutAssets.ContentSet);
		}
		OutAssets.ContentSetSnapshot.SourceContentSet = OutAssets.ContentSet;
		return true;
	}
}


bool FLayoutProfileJsonFixture::ExportToString(
	const ULayoutProfileAsset* Profile,
	const ULayoutRegionContentSetAsset* ContentSet,
	FString& OutJson,
	FString& OutError)
{
	OutJson.Reset();
	OutError.Reset();
	if (Profile == nullptr || ContentSet == nullptr)
	{
		OutError = TEXT("Layout fixture export requires both a profile and a content set.");
		return false;
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), LayoutRecursiveFixtureSchemaVersion);

	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("profile_id"), Profile->GetFName().ToString());
	RootObject->SetStringField(TEXT("content_set_id"), ContentSet->GetFName().ToString());
	Root->SetObjectField(TEXT("root"), RootObject);

	TMap<FName, const ULayoutProfileAsset*> ProfilesById;
	TMap<FName, const ULayoutRegionContentSetAsset*> ContentSetsById;
	TMap<FName, const ULayoutModuleAsset*> ModulesById;
	TMap<FName, const ULayoutCompositeModuleAsset*> CompositeModulesById;
	CollectRecursiveFixtureAssets(Profile, ContentSet, ProfilesById, ContentSetsById, ModulesById, CompositeModulesById);

	TSharedPtr<FJsonObject> LibrariesObject = MakeShared<FJsonObject>();
	TArray<FName> ProfileIds;
	ProfilesById.GetKeys(ProfileIds);
	ProfileIds.Sort([](const FName& Left, const FName& Right)
	{
		return Left.ToString() < Right.ToString();
	});
	TArray<TSharedPtr<FJsonValue>> JsonProfiles;
	for (const FName& ProfileId : ProfileIds)
	{
		JsonProfiles.Add(MakeShared<FJsonValueObject>(ProfileAuthoringToJson(ProfilesById[ProfileId])));
	}
	LibrariesObject->SetArrayField(TEXT("profiles"), JsonProfiles);

	TArray<FName> ContentSetIds;
	ContentSetsById.GetKeys(ContentSetIds);
	ContentSetIds.Sort([](const FName& Left, const FName& Right)
	{
		return Left.ToString() < Right.ToString();
	});
	TArray<TSharedPtr<FJsonValue>> JsonContentSets;
	for (const FName& ContentSetId : ContentSetIds)
	{
		JsonContentSets.Add(MakeShared<FJsonValueObject>(ContentSetAuthoringToJson(ContentSetsById[ContentSetId])));
	}
	LibrariesObject->SetArrayField(TEXT("content_sets"), JsonContentSets);

	TArray<FName> ModuleIds;
	ModulesById.GetKeys(ModuleIds);
	ModuleIds.Sort([](const FName& Left, const FName& Right)
	{
		return Left.ToString() < Right.ToString();
	});
	TArray<TSharedPtr<FJsonValue>> JsonModules;
	for (const FName& ModuleId : ModuleIds)
	{
		JsonModules.Add(MakeShared<FJsonValueObject>(ModuleAuthoringToJson(ModulesById[ModuleId])));
	}
	LibrariesObject->SetArrayField(TEXT("modules"), JsonModules);

	TArray<FName> CompositeModuleIds;
	CompositeModulesById.GetKeys(CompositeModuleIds);
	CompositeModuleIds.Sort([](const FName& Left, const FName& Right)
	{
		return Left.ToString() < Right.ToString();
	});
	TArray<TSharedPtr<FJsonValue>> JsonCompositeModules;
	for (const FName& CompositeModuleId : CompositeModuleIds)
	{
		JsonCompositeModules.Add(MakeShared<FJsonValueObject>(CompositeModuleAuthoringToJson(CompositeModulesById[CompositeModuleId])));
	}
	LibrariesObject->SetArrayField(TEXT("composite_modules"), JsonCompositeModules);
	Root->SetObjectField(TEXT("libraries"), LibrariesObject);

	FIntVector FixtureSharedCellSizeOverride = FIntVector::ZeroValue;
	const FIntVector* FixtureSharedCellSizeOverridePtr =
		TryResolveRecursiveFixtureContentSetSharedCellSizeInBlocks(
			ContentSet,
			FixtureSharedCellSizeOverride)
			? &FixtureSharedCellSizeOverride
			: nullptr;
	const FLayoutRegionSolveRequest FixtureExportRequest =
		FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ContentSet,
			Profile,
			0,
			TEXT("FixtureExport"),
			FLayoutSolverExecutionSettings(),
			NAME_None,
			NAME_None,
			0,
			NAME_None,
			FixtureSharedCellSizeOverridePtr);
	TSharedPtr<FJsonObject> CompiledSnapshotObject = MakeShared<FJsonObject>();
	CompiledSnapshotObject->SetObjectField(TEXT("content_set_snapshot"), ContentSetSnapshotToJson(FixtureExportRequest.ContentSetSnapshot));
	CompiledSnapshotObject->SetObjectField(TEXT("module_set_snapshot"), ModuleCatalogToJson(FixtureExportRequest.ModuleCatalog));
	CompiledSnapshotObject->SetObjectField(TEXT("profile_snapshot"), ProfileSnapshotToJson(FixtureExportRequest.ProfileSnapshot));
	Root->SetObjectField(TEXT("compiled_snapshot"), CompiledSnapshotObject);

	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutJson);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		OutError = TEXT("Failed to serialize layout fixture JSON.");
		return false;
	}

	return true;
}

bool FLayoutProfileJsonFixture::ImportFromString(
	const FString& Json,
	UObject* Outer,
	FLayoutProfileJsonFixtureAssets& OutAssets,
	TArray<FString>& OutIssues)
{
	OutAssets = FLayoutProfileJsonFixtureAssets();
	OutIssues.Reset();

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutIssues.Add(TEXT("Failed to parse layout fixture JSON."));
		return false;
	}

	const int32 SchemaVersion = static_cast<int32>(Root->GetIntegerField(TEXT("schema_version")));
	if (SchemaVersion == LayoutRecursiveFixtureSchemaVersion)
	{
		return ImportRecursiveContentSetFixture(Root, Outer, OutAssets, OutIssues);
	}

	OutIssues.Add(FString::Printf(
		TEXT("Unsupported layout fixture schema v%d. Re-export using destructive sparse-placement schema v%d."),
		SchemaVersion,
		LayoutRecursiveFixtureSchemaVersion));
	return false;
}


bool FLayoutProfileJsonFixture::BuildImportedRootRequest(
	const FLayoutProfileJsonFixtureAssets& Assets,
	const int32 Seed,
	const FString& RegionDebugPath,
	FLayoutRegionSolveRequest& OutRequest,
	const FLayoutRootSolveBudgetSettings& SolveBudget,
	const FName RootPlacementPolicyId,
	const FName RootCandidateId,
	const int32 TemplatePlacementZOffsetBlocks,
	FString* OutError,
	const FLayoutWorldBindingPlacementPolicy* const PlacementPolicyOverride,
	const FLayoutImportedRootSteppedTerrainSupportContext* const SteppedTerrainSupportContext)
{
	OutRequest = FLayoutRegionSolveRequest();
	if (OutError != nullptr)
	{
		OutError->Reset();
	}

	if (Assets.Profile == nullptr)
	{
		if (OutError != nullptr)
		{
			*OutError = TEXT("Imported fixture root request requires a reconstructed profile.");
		}
		return false;
	}

		const FName ResolvedRootPlacementPolicyId = RootPlacementPolicyId != NAME_None
			? RootPlacementPolicyId
			: FixtureReplayPlacementPolicyId;
		const FLayoutId ResolvedRootCandidateId = RootCandidateId != NAME_None
			? RootCandidateId
			: (!RegionDebugPath.IsEmpty() ? FLayoutId(*RegionDebugPath) : NAME_None);
		const FLayoutId ResolvedRootSolveId =
			!RegionDebugPath.IsEmpty() ? FLayoutId(*RegionDebugPath) : NAME_None;
		const FLayoutWorldBindingPlacementPolicy FixtureReplayPlacementPolicy =
			PlacementPolicyOverride != nullptr
				? *PlacementPolicyOverride
				: BuildFixtureReplayPlacementPolicy();
		FLayoutWorldBindingRuntimeView RuntimeView =
			LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
				Assets.Profile.Get(),
				Assets.ContentSet.Get(),
				
				SolveBudget,
				FixtureReplayPlacementPolicy);
		if (!HasPositiveFixtureSharedCellSize(RuntimeView.SharedCellSizeInBlocks))
		{
			if (HasPositiveFixtureSharedCellSize(Assets.ContentSetSnapshot.SharedCellSizeInBlocks))
			{
				RuntimeView.SharedCellSizeInBlocks = Assets.ContentSetSnapshot.SharedCellSizeInBlocks;
			}
			else if (HasPositiveFixtureSharedCellSize(Assets.ModuleCatalog.SharedCellSizeInBlocks))
			{
				RuntimeView.SharedCellSizeInBlocks = Assets.ModuleCatalog.SharedCellSizeInBlocks;
			}
		}
		RuntimeView.TemplatePlacementZOffsetBlocks = TemplatePlacementZOffsetBlocks;

	auto TryBuildImportedRequest = [&]() -> bool
	{
		FString FailureReason;
		if (!LayoutWorldBindingSolveRequestBuilder::TryBuildStandaloneSolveRequest(
			RuntimeView,
			Seed,
			RegionDebugPath,
			ResolvedRootPlacementPolicyId,
			ResolvedRootCandidateId,
			ResolvedRootSolveId,
			OutRequest,
			FailureReason))
		{
			if (OutError != nullptr)
			{
				*OutError = FailureReason;
			}
			return false;
		}

		if (!ShouldDeriveFixtureReplaySteppedTerrainSupport(RuntimeView, SteppedTerrainSupportContext))
		{
			return true;
		}

		// The prewarm adapter handles cell derivation; preserve fixture-supplied cells here.
		if (!LayoutWorldBindingRuntimeHelpers::TryPopulateExplicitRuntimeSteppedTerrainSupportOnRequest(
			RuntimeView,
			SteppedTerrainSupportContext->EligibleBiomeRowName,
			SteppedTerrainSupportContext->SiteCenterBlockWorldPos,
			OutRequest.FootprintSize,
			OutRequest.PlannedCells,
			SteppedTerrainSupportContext->CoordinateSettings,
			*SteppedTerrainSupportContext->ActiveBiomeSampler,
			OutRequest,
			FailureReason))
		{
			if (OutError != nullptr)
			{
				*OutError = FailureReason.IsEmpty()
					? TEXT("Imported fixture replay could not materialize derived stepped terrain support onto the standalone request.")
					: FailureReason;
			}
			return false;
		}

		return true;
	};

	if (Assets.ContentSet != nullptr)
	{
		return TryBuildImportedRequest();
	}

	if (nullptr != nullptr)
	{
		return TryBuildImportedRequest();
	}

	if (OutError != nullptr)
	{
		*OutError = TEXT("Imported fixture root request requires a reconstructed content set.");
	}
	return false;
}

bool FLayoutProfileJsonFixture::ExportToFile(
	const ULayoutProfileAsset* Profile,
	const ULayoutRegionContentSetAsset* ContentSet,
	const FString& AbsoluteFilePath,
	FString& OutError)
{
	FString Json;
	if (!ExportToString(Profile, ContentSet, Json, OutError))
	{
		return false;
	}

	const FString Directory = FPaths::GetPath(AbsoluteFilePath);
	if (!Directory.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*Directory, true);
	}

	if (!FFileHelper::SaveStringToFile(Json, *AbsoluteFilePath))
	{
		OutError = FString::Printf(TEXT("Failed to save layout fixture JSON to '%s'."), *AbsoluteFilePath);
		return false;
	}

	return true;
}

bool FLayoutProfileJsonFixture::ImportFromFile(
	const FString& AbsoluteFilePath,
	UObject* Outer,
	FLayoutProfileJsonFixtureAssets& OutAssets,
	TArray<FString>& OutIssues)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *AbsoluteFilePath))
	{
		OutIssues.Reset();
		OutIssues.Add(FString::Printf(TEXT("Failed to load layout fixture JSON from '%s'."), *AbsoluteFilePath));
		return false;
	}

	return ImportFromString(Json, Outer, OutAssets, OutIssues);
}
