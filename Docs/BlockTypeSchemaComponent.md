# BlockTypeSchemaComponent

## Purpose
`UBlockTypeSchemaComponent` is the reusable runtime component that builds block-definition lookup maps once the host chunk world has finished setting up its runtime indexes.

It provides:
- a required `UBlockTypeSchemaRegistry` reference named `BlockTypeSchemaRegistry` for schema resolution
- a material-index lookup map for resolved block definitions
- a mesh-index lookup map for resolved block definitions
- a block-position utility that reconstructs authored definition data and returns the block type tag
- a block-position utility that materializes authored custom data on the first write
- a block-position utility that reconstructs authored custom data and returns the block type tag
- one-time map building from the owning chunk world's runtime material and mesh tables
- a reusable host-independent implementation that can be attached to any chunk-world class

## Current Behavior
- Stores a required `UBlockTypeSchemaRegistry` in `BlockTypeSchemaRegistry`
- Builds lookup maps from the owning chunk world's resolved material and mesh indexes after the chunk world starts generation
- Exposes `GetBlockDefinitionForMaterialIndex()` and `GetBlockDefinitionForMeshIndex()`
- Exposes `GetBlockDefinitionForBlockWorldPos()` so gameplay or tooling can reconstruct the authored definition struct and block type tag from the resolved schema row
- Exposes `InitializeBlockCustomData()` so block gameplay can lazily seed the runtime custom-data slots for a `BlockWorldPos`
- Exposes `GetBlockCustomDataForBlockWorldPos()` so gameplay or tooling can reconstruct the authored struct and block type tag from the packed runtime slots
- Also exposes templated C++ overloads for the index/world-position helpers when the caller already knows the concrete struct type
- Uses `FInstancedStruct` for the generic payload path and typed helpers for the convenience path
- Clears the lookup maps when the owning actor ends play
- Caches one `FBlockCustomDataLayout` per `UScriptStruct` so the same custom-data family always packs and unpacks with the same slot order

For tag-based lookup, use `UBlockTypeSchemaRegistry::TryGetBlockDefinition(...)` or `UBlockTypeSchemaRegistry::TryGetBlockCustomData(...)` instead of the component.

For Blueprint base-type checks and canonical struct references, use `UBlockTypeSchemaBlueprintLibrary`.
For generic `FInstancedStruct` payload handling, use the registry helpers plus the Blueprint library rather than trying to inspect the struct through the component alone. The library is intentionally small: it gives Blueprint code the base struct references, family checks, and concrete `TryGet*` helpers needed to work with `FInstancedStruct` payloads safely.

## Intended Use
Attach this component to any chunk world that should use the plugin's block-type schema flow. `AChunkWorldExtended` creates one by default, but other chunk-world classes can add the same component without inheriting from the extension actor.
