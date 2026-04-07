# Block Type Schema

## Purpose
`UBlockTypeSchemaRegistry` is the plugin-owned asset that resolves static, lookup-only block type data.

It separates the authored block model into two families:
- `Definition` for static, type-level lookup data
- `CustomData` for runtime materialized per-block data

Both families are constrained to derive from the plugin base structs:
- `FBlockDefinitionBase`
- `FBlockCustomDataBase`

## Base Types
- `FBlockDefinitionBase`
- `FBlockCustomDataBase`

These are the plugin-owned base `USTRUCT`s that project-specific derived structs should inherit from.

## Default Damage-Oriented Structs
The plugin also provides a concrete first-pass pair for damageable blocks:
- `FBlockDamageDefinition`
- `FBlockDamageCustomData`

Use these when you want a block family to expose:
- `MaxHealth` and `HitSound` in the definition payload
- `Health` in the runtime custom-data payload

They extend the plugin base families, so they still fit into `FBlockTypeSchema` rows and the registry validation path.

## Authoring Model
One `FBlockTypeSchema` row describes one semantic block type.

Each row contains:
- `BlockTypeName`
- `Definition`
- `CustomData`

Within `Definition`, `MaterialAsset` and `MeshAsset` are the primary association fields used to match the semantic block type back to Porism runtime blocks. They are intentionally surfaced first in the definition UI so authored rows read as:
- what Porism block this schema row matches
- then what static gameplay data that block type carries

`BlockTypeName` should also stay grouped by the same authored identity so the tag tree remains readable. Try something like:
- `BlockType.Material.Dirt`
- `BlockType.Mesh.Grass.MainGrass`
- `BlockType.Mesh.Tree.LargeFirTree`

## Expected Project Extension Pattern
Future project-specific structs should extend the base families, for example:
- `FMyGameBlockDefinition : public FBlockDamageDefinition`
- `FMyGameBlockCustomData : public FBlockDamageCustomData`

That gives the schema a stable base contract while still allowing each voxel family to carry its own static definition payload and runtime custom data payload.

## Validation Rule
The schema validates that:
- the base definition and custom-data families are set
- `BaseDefinitionStruct` derives from `FBlockDefinitionBase`
- `BaseCustomDataStruct` derives from `FBlockCustomDataBase`
- each block type row uses payload structs compatible with those bases

This keeps the lookup layer predictable before any gameplay code consumes the resolved data.
Invalid authored rows are also reported when the registry asset loads, not just when it is edited.

## Startup Lookup
`UBlockTypeSchemaComponent` reads this registry after the chunk world starts generation and builds two transient lookup maps:
- material index -> resolved block definition
- mesh index -> resolved block definition

That lets chunk-world code query block definitions without rebuilding the lookup every time it needs an index.

The same component can also reconstruct the resolved definition payload for a specific block world position once the runtime material and mesh indexes are available.
For C++ callers that already know the concrete struct family, the component also exposes templated helpers that copy directly into a typed struct after the gameplay tag or runtime index has been resolved.

`UBlockTypeSchemaRegistry` also exposes tag-based typed helpers:
- `TryGetBlockDefinition<T>()`
- `TryGetBlockCustomData<T>()`
- `TryGetBlockDefinition(FGameplayTag, FInstancedStruct&)`
- `TryGetBlockCustomData(FGameplayTag, FInstancedStruct&)`

Use those when you already know the `FGameplayTag` for the block type and want a typed struct back without manually unpacking the `FInstancedStruct`.

Blueprints can use `UBlockTypeSchemaBlueprintLibrary` to fetch the canonical base structs and stay on the generic `FInstancedStruct` path without manually unpacking the struct by hand.
The library also provides base-family and damage-family `TryGet*` helpers so Blueprint code can inspect a payload, confirm which shared family it belongs to, and copy the concrete struct when needed.

## Using `FInstancedStruct`
`FInstancedStruct` is the generic container used by the registry and component when the concrete struct type may vary. Use it when you want to keep the payload polymorphic and decide later how to interpret it.

### Blueprint usage
In Blueprints, use `UBlockTypeSchemaBlueprintLibrary` when you need to stay on the generic path.
Typical usage is:
1. Fetch the canonical base struct references with `GetBlockDefinitionBaseStruct()` or `GetBlockCustomDataBaseStruct()` when you need to compare or display the expected family.
2. Use `IsBlockDefinitionPayload(...)`, `IsBlockCustomDataPayload(...)`, `IsBlockDamageDefinitionPayload(...)`, or `IsBlockDamageCustomDataPayload(...)` to classify an `FInstancedStruct`.
3. Call `TryGetBlockDefinitionBase(...)`, `TryGetBlockCustomDataBase(...)`, `TryGetBlockDamageDefinition(...)`, or `TryGetBlockDamageCustomData(...)` to copy the concrete struct into a Blueprint-visible output pin.

That keeps Blueprint code on the same polymorphic payload path as C++, while still making it easy to branch on the shared base families before reading the data.

### C++ usage
When you only have an `FInstancedStruct`, inspect the stored struct type first:

```cpp
const UScriptStruct* StructType = CustomData.GetScriptStruct();
if (StructType != nullptr && StructType->IsChildOf(FBlockDamageCustomData::StaticStruct()))
{
	const FBlockDamageCustomData* DamageData = CustomData.GetPtr<FBlockDamageCustomData>();
	if (DamageData != nullptr)
	{
		int32 Health = DamageData->Health;
	}
}
```

That pattern works for any block-family base:
- check the stored type with `GetScriptStruct()`
- confirm it is derived from the base family you expect, such as `FBlockDamageCustomData` or `FBlockDamageDefinition`
- use `GetPtr<T>()` to access the concrete struct in a generic path

If you already know the exact derived type, prefer the templated registry helpers:

```cpp
FBlockDamageCustomData DamageData;
if (Registry->TryGetBlockCustomData(BlockTypeTag, DamageData))
{
	int32 Health = DamageData.Health;
}
```

That keeps the plugin contract consistent while still allowing Blueprint-friendly generic storage and C++ typed access.

## Custom-Data Packing
`UBlockTypeSchemaComponent` also provides a block-position helper that can seed custom data for a specific block world position.

The packer works in inheritance order and is cached per custom-data struct family:
- parent struct fields are written first
- child struct fields append after their parents
- a reserved runtime marker slot is appended last so the component can detect whether that block has already been materialized

That makes the layout stable for user-defined structs that extend `FBlockDamageCustomData` or any other `FBlockCustomDataBase` descendant.

The same cached layout is used for unpacking, so the component can reconstruct the authored `FInstancedStruct` for a block world position without relying on a separate side cache.

## Editor Workflow
1. Create a new `UBlockTypeSchemaRegistry` data asset in the Content Browser.
2. Author the `FBlockTypeSchema` rows on that asset.
3. Assign the asset to `AChunkWorldExtended.BlockTypeSchemaRegistry`.
4. The chunk world syncs that actor-level property into its runtime `UBlockTypeSchemaComponent` automatically.
5. Use `GetBlockDefinitionForMaterialIndex()` or `GetBlockDefinitionForMeshIndex()` on the component when you need the resolved definition for a runtime index.
6. Call `GetBlockDefinitionForBlockWorldPos()` when you need the resolved definition payload and block type tag for a specific block world position.
7. Call `InitializeBlockCustomData()` with a `BlockWorldPos` when a block should lazily seed its custom data on first use.
8. Call `GetBlockCustomDataForBlockWorldPos()` when you need to reconstruct the authored custom-data struct and block type tag from the packed runtime slots.
