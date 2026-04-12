// Copyright 2026 Spotted Loaf Studio

#include "Block/BlockCustomDataLayout.h"
#include "Block/BlockTypeSchemaBlueprintLibrary.h"
#include "Block/BlockTypeSchemaRegistry.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "StructUtils/InstancedStruct.h"

#include "PorismExtensionTestTypes.h"

namespace
{
	FInstancedStruct MakeExactDamageDefinitionPayload(int32 MaxHealth, bool bInvincible)
	{
		FBlockHealthDefinition Definition;
		Definition.MaxHealth = MaxHealth;
		Definition.bInvincible = bInvincible;

		FInstancedStruct Payload;
		Payload.InitializeAs<FBlockHealthDefinition>(Definition);
		return Payload;
	}

	FInstancedStruct MakeExactDamageCustomDataPayload(int32 Health)
	{
		FBlockHealthCustomData CustomData;
		CustomData.Health = Health;

		FInstancedStruct Payload;
		Payload.InitializeAs<FBlockHealthCustomData>(CustomData);
		return Payload;
	}
}

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_PorismExtension_TestBlock, "DropType");

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionCustomDataLayoutRoundTripTest,
	"PorismExtension.Schema.CustomDataLayout.RoundTripDerivedDamagePayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionCustomDataLayoutRoundTripTest::RunTest(const FString& Parameters)
{
	FBlockCustomDataLayout Layout;
	TestTrue(TEXT("Derived damage custom-data layout builds"), Layout.Build(FPorismExtensionDerivedDamageCustomData::StaticStruct()));
	TestEqual(TEXT("Derived damage payload uses four authored slots"), Layout.GetValueSlotCount(), 4);

	FPorismExtensionDerivedDamageCustomData Source;
	Source.Health = 37;
	Source.BonusHealth = 9;
	Source.Nested.bHasDust = true;
	Source.Nested.DustAmount = 4;

	TArray<int32> PackedValues;
	TestTrue(TEXT("Derived damage payload packs"), Layout.Pack(&Source, PackedValues));
	TestEqual(TEXT("Packed slot count matches layout"), PackedValues.Num(), 4);
	TestEqual(TEXT("Health stays in slot zero"), PackedValues[0], 37);
	TestEqual(TEXT("Bonus health follows the shared health field"), PackedValues[1], 9);
	TestEqual(TEXT("Nested bool is packed as one"), PackedValues[2], 1);
	TestEqual(TEXT("Nested scalar is packed after nested bool"), PackedValues[3], 4);

	FPorismExtensionDerivedDamageCustomData Unpacked;
	TestTrue(TEXT("Derived damage payload unpacks"), Layout.Unpack(PackedValues, &Unpacked));
	TestEqual(TEXT("Unpacked health matches source"), Unpacked.Health, 37);
	TestEqual(TEXT("Unpacked bonus health matches source"), Unpacked.BonusHealth, 9);
	TestTrue(TEXT("Unpacked nested bool matches source"), Unpacked.Nested.bHasDust);
	TestEqual(TEXT("Unpacked nested amount matches source"), Unpacked.Nested.DustAmount, 4);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionCustomDataLayoutRejectsUnsupportedPropertyTest,
	"PorismExtension.Schema.CustomDataLayout.RejectsUnsupportedProperty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionCustomDataLayoutRejectsUnsupportedPropertyTest::RunTest(const FString& Parameters)
{
	FBlockCustomDataLayout Layout;
	TestFalse(TEXT("Unsupported custom-data layout fails to build"), Layout.Build(FPorismExtensionUnsupportedCustomData::StaticStruct()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionCustomDataLayoutExactDamagePayloadTest,
	"PorismExtension.Schema.CustomDataLayout.RoundTripExactDamagePayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionCustomDataLayoutExactDamagePayloadTest::RunTest(const FString& Parameters)
{
	FBlockCustomDataLayout Layout;
	TestTrue(TEXT("Exact damage custom-data layout builds"), Layout.Build(FBlockHealthCustomData::StaticStruct()));
	TestEqual(TEXT("Exact damage payload uses one authored slot"), Layout.GetValueSlotCount(), 1);

	FBlockHealthCustomData Source;
	Source.Health = 14;

	TArray<int32> PackedValues;
	TestTrue(TEXT("Exact damage payload packs"), Layout.Pack(&Source, PackedValues));
	TestEqual(TEXT("Exact damage payload writes one slot"), PackedValues.Num(), 1);
	TestEqual(TEXT("Health is stored in slot zero"), PackedValues[0], 14);

	FBlockHealthCustomData Unpacked;
	TestTrue(TEXT("Exact damage payload unpacks"), Layout.Unpack(PackedValues, &Unpacked));
	TestEqual(TEXT("Exact damage payload round-trips health"), Unpacked.Health, 14);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionCustomDataLayoutRejectsShortPackedArrayTest,
	"PorismExtension.Schema.CustomDataLayout.RejectsShortPackedArray",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionCustomDataLayoutRejectsShortPackedArrayTest::RunTest(const FString& Parameters)
{
	FBlockCustomDataLayout Layout;
	TestTrue(TEXT("Derived damage custom-data layout builds"), Layout.Build(FPorismExtensionDerivedDamageCustomData::StaticStruct()));

	FPorismExtensionDerivedDamageCustomData Unpacked;
	TArray<int32> ShortPackedValues;
	ShortPackedValues.Add(11);
	ShortPackedValues.Add(3);

	TestFalse(TEXT("Unpack fails when the packed array is shorter than the authored slot count"), Layout.Unpack(ShortPackedValues, &Unpacked));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionSchemaBlueprintLibraryDerivedPayloadTest,
	"PorismExtension.Schema.BlueprintLibrary.AcceptsDerivedDamagePayloads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionSchemaBlueprintLibraryDerivedPayloadTest::RunTest(const FString& Parameters)
{
	FPorismExtensionDerivedDamageCustomData DerivedCustomData;
	DerivedCustomData.Health = 21;
	DerivedCustomData.BonusHealth = 6;

	FInstancedStruct CustomDataPayload;
	CustomDataPayload.InitializeAs<FPorismExtensionDerivedDamageCustomData>(DerivedCustomData);

	TestTrue(TEXT("Derived custom-data payload counts as block custom data"), UBlockTypeSchemaBlueprintLibrary::IsBlockCustomDataPayload(CustomDataPayload));
	TestTrue(TEXT("Derived custom-data payload counts as block health custom data"), UBlockTypeSchemaBlueprintLibrary::IsBlockHealthCustomDataPayload(CustomDataPayload));
	TestTrue(TEXT("Derived custom-data payload counts as block damage custom data"), UBlockTypeSchemaBlueprintLibrary::IsBlockDamageCustomDataPayload(CustomDataPayload));

	FBlockHealthCustomData HealthCustomData;
	TestTrue(TEXT("Derived custom-data payload can be read as shared health custom data"), UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthCustomData(CustomDataPayload, HealthCustomData));
	TestEqual(TEXT("Shared health custom-data view keeps the derived health value"), HealthCustomData.Health, 21);

	FBlockDamageCustomData DamageCustomData;
	TestTrue(TEXT("Derived custom-data payload can be read as shared damage custom data"), UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageCustomData(CustomDataPayload, DamageCustomData));
	TestEqual(TEXT("Shared damage custom-data view keeps the derived health value"), DamageCustomData.Health, 21);

	FPorismExtensionDerivedDamageDefinition DerivedDefinition;
	DerivedDefinition.MaxHealth = 80;
	DerivedDefinition.Hardness = 5;
	DerivedDefinition.Quality = EPorismExtensionTestQuality::High;

	FInstancedStruct DefinitionPayload;
	DefinitionPayload.InitializeAs<FPorismExtensionDerivedDamageDefinition>(DerivedDefinition);

	TestTrue(TEXT("Derived definition payload counts as block definition"), UBlockTypeSchemaBlueprintLibrary::IsBlockDefinitionPayload(DefinitionPayload));
	TestTrue(TEXT("Derived definition payload counts as block health definition"), UBlockTypeSchemaBlueprintLibrary::IsBlockHealthDefinitionPayload(DefinitionPayload));
	TestTrue(TEXT("Derived definition payload counts as block damage definition"), UBlockTypeSchemaBlueprintLibrary::IsBlockDamageDefinitionPayload(DefinitionPayload));

	FBlockHealthDefinition HealthDefinition;
	TestTrue(TEXT("Derived definition payload can be read as shared health definition"), UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthDefinition(DefinitionPayload, HealthDefinition));
	TestEqual(TEXT("Shared health definition view keeps the derived max health"), HealthDefinition.MaxHealth, 80);

	FBlockDamageDefinition DamageDefinition;
	TestTrue(TEXT("Derived definition payload can be read as shared damage definition"), UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageDefinition(DefinitionPayload, DamageDefinition));
	TestEqual(TEXT("Shared damage definition view keeps the derived max health"), DamageDefinition.MaxHealth, 80);

	FInstancedStruct IncompatiblePayload;
	IncompatiblePayload.InitializeAs<FVector>(FVector(1.0, 2.0, 3.0));
	TestFalse(TEXT("Unrelated payload is not treated as block custom data"), UBlockTypeSchemaBlueprintLibrary::IsBlockCustomDataPayload(IncompatiblePayload));
	TestFalse(TEXT("Unrelated payload does not convert to shared damage custom data"), UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageCustomData(IncompatiblePayload, DamageCustomData));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionSchemaRegistryLookupTest,
	"PorismExtension.Schema.Registry.ResolvesDefinitionAndCustomData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionSchemaRegistryLookupTest::RunTest(const FString& Parameters)
{
	UBlockTypeSchemaRegistry* Registry = NewObject<UBlockTypeSchemaRegistry>();
	TestNotNull(TEXT("Registry object is created"), Registry);

	FBlockTypeSchema Row;
	Row.BlockTypeName = TAG_PorismExtension_TestBlock;
	Row.Definition = MakeExactDamageDefinitionPayload(65, false);
	Row.CustomData = MakeExactDamageCustomDataPayload(42);

	TArray<FBlockTypeSchema>& MutableRows = const_cast<TArray<FBlockTypeSchema>&>(Registry->GetBlockTypeDefinitions());
	MutableRows.Add(Row);

	FBlockTypeSchema FoundRow;
	TestTrue(TEXT("Registry finds authored row by gameplay tag"), Registry->FindBlockTypeDefinition(TAG_PorismExtension_TestBlock, FoundRow));
	TestEqual(TEXT("Found row keeps the expected gameplay tag"), FoundRow.BlockTypeName, TAG_PorismExtension_TestBlock.GetTag());

	FInstancedStruct DefinitionPayload;
	TestTrue(TEXT("Registry resolves definition payload"), Registry->TryGetBlockDefinition(TAG_PorismExtension_TestBlock, DefinitionPayload));

	FBlockHealthDefinition HealthDefinition;
	TestTrue(TEXT("Definition payload converts to shared health definition"), UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthDefinition(DefinitionPayload, HealthDefinition));
	TestEqual(TEXT("Resolved max health matches authored definition"), HealthDefinition.MaxHealth, 65);

	FInstancedStruct CustomDataPayload;
	TestTrue(TEXT("Registry resolves custom-data payload"), Registry->TryGetBlockCustomData(TAG_PorismExtension_TestBlock, CustomDataPayload));

	FBlockHealthCustomData HealthCustomData;
	TestTrue(TEXT("Custom-data payload converts to shared health custom data"), UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthCustomData(CustomDataPayload, HealthCustomData));
	TestEqual(TEXT("Resolved custom-data health matches authored value"), HealthCustomData.Health, 42);

	TestFalse(TEXT("Unknown tag does not resolve a definition"), Registry->TryGetBlockDefinition(FGameplayTag(), DefinitionPayload));
	TestFalse(TEXT("Unknown tag does not resolve custom data"), Registry->TryGetBlockCustomData(FGameplayTag(), CustomDataPayload));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionSchemaComponentRequiredChannelCountTest,
	"PorismExtension.Schema.Component.RequiredChannelCountTracksLargestLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionSchemaComponentRequiredChannelCountTest::RunTest(const FString& Parameters)
{
	UBlockTypeSchemaComponent* SchemaComponent = NewObject<UBlockTypeSchemaComponent>();
	TestNotNull(TEXT("Schema component is created"), SchemaComponent);
	TestEqual(TEXT("Missing registry reports zero required channels"), SchemaComponent->GetRequiredCustomDataChannelCount(), 0);

	UBlockTypeSchemaRegistry* Registry = NewObject<UBlockTypeSchemaRegistry>();
	TestNotNull(TEXT("Registry object is created"), Registry);

	FBlockTypeSchema Row;
	Row.BlockTypeName = TAG_PorismExtension_TestBlock;
	Row.CustomData.InitializeAs<FPorismExtensionDerivedDamageCustomData>();

	TArray<FBlockTypeSchema>& MutableRows = const_cast<TArray<FBlockTypeSchema>&>(Registry->GetBlockTypeDefinitions());
	MutableRows.Add(Row);

	SchemaComponent->SetBlockTypeSchemaRegistry(Registry);

	TestEqual(TEXT("Derived payload requires authored slots plus the initialization marker"), SchemaComponent->GetRequiredCustomDataChannelCount(), 5);
	TestEqual(TEXT("Shared damage health slot stays at index zero"), SchemaComponent->GetBlockDamageHealthCustomDataIndex(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionSchemaComponentLookupResetTest,
	"PorismExtension.Schema.Component.LookupMapsResetCleanly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionSchemaComponentLookupResetTest::RunTest(const FString& Parameters)
{
	UBlockTypeSchemaComponent* SchemaComponent = NewObject<UBlockTypeSchemaComponent>();
	TestNotNull(TEXT("Schema component is created"), SchemaComponent);
	TestFalse(TEXT("Lookup maps start unbuilt"), SchemaComponent->IsBlockDefinitionLookupReady());

	FInstancedStruct DefinitionPayload;
	TestFalse(TEXT("Material lookup fails before lookup maps are built"), SchemaComponent->GetBlockDefinitionForMaterialIndex(1, DefinitionPayload));
	TestFalse(TEXT("Mesh lookup fails before lookup maps are built"), SchemaComponent->GetBlockDefinitionForMeshIndex(1, DefinitionPayload));

	SchemaComponent->ClearBlockDefinitionLookupMaps();
	TestFalse(TEXT("Clearing unbuilt lookup maps keeps the component in an unbuilt state"), SchemaComponent->IsBlockDefinitionLookupReady());
	TestFalse(TEXT("Material lookup still fails after clearing"), SchemaComponent->GetBlockDefinitionForMaterialIndex(1, DefinitionPayload));
	TestFalse(TEXT("Mesh lookup still fails after clearing"), SchemaComponent->GetBlockDefinitionForMeshIndex(1, DefinitionPayload));

	return true;
}
