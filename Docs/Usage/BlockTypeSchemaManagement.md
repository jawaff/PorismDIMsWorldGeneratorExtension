# Block Type Schema Management

## Purpose
Use the block type schema system when your chunk-world blocks need structured authored definition data and structured runtime custom data instead of raw `int32` slot management.

## How To Use It
1. Use `AChunkWorldExtended` as the chunk-world actor.
2. Assign a `UBlockTypeSchemaRegistry` data asset on the chunk world. (You need to create your own data asset!)
3. Add block type entries to that registry for the block types used by your world.
4. Choose or author:
   - a definition struct family for authored constant data
   - a custom-data struct family for runtime replicated data
5. Configure each block type entry with the correct definition and custom-data payloads.
6. Start generation normally. The extension plugin sizes the required custom-data storage and rebuilds lookup maps during startup.

`AChunkWorldExtended` already includes the schema component that reads this registry, so normal usage only requires configuring the registry and block entries.

## Runtime Usage
- Use the block type schema component on `AChunkWorldExtended` to resolve block type information.
- Use schema-backed helpers and libraries when you need to translate between represented blocks and their authored/runtime schema data.
- Prefer writing gameplay against your struct types rather than directly against Porism custom-data arrays.

## When To Use Different Struct Families
- Use one shared base family when all blocks follow the same gameplay contract.
- Use extended families when some block types need extra fields beyond the shared baseline.
- Keep common fields in the shared base so systems like interaction, damage, or destruction can work consistently.

## Recommended Rule
- Treat the schema registry as the source of truth for authored block gameplay data.
- Keep raw Porism array access as a low-level implementation detail, not the main gameplay interface.

## Related Docs
- [ChunkWorldGameplaySetup.md](./ChunkWorldGameplaySetup.md)
- [../Design/BlockTypeSchema.md](../Design/BlockTypeSchema.md)
- [../Design/BlockTypeSchemaComponent.md](../Design/BlockTypeSchemaComponent.md)
