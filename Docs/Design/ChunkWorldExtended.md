# ChunkWorldExtended

## Purpose
`AChunkWorldExtended` is the thin reusable host actor for the plugin's block lookup flow.

It provides:
- a default place to attach the reusable block type schema component
- a single actor-level schema registry property that feeds that component
- a default place to host shared authoritative block custom-data update notifications
- a default place to host shared replicated block swap transitions
- a clean subclass of `AChunkWorld` for future project-specific chunk-world behavior
- a simple getter for the schema component

## Current Behavior
- Owns a `UBlockTypeSchemaComponent` by default
- Owns a single editable `BlockTypeSchemaRegistry` property and syncs it into the runtime schema component
- Owns a `UChunkWorldBlockFeedbackComponent` by default
- Owns a `UChunkWorldBlockSwapComponent` by default
- Rebuilds the component's lookup maps during `StartGen()` after the base chunk world has populated its runtime indexes
- Emits `OnAuthoritativeBlockCustomDataUpdated` after authoritative replicated custom-data writes are applied
- Retires matching predicted block state through `UPorismPredictedBlockStateComponent`
- Resolves schema-authored swap actor classes and distances through `UBlockTypeSchemaComponent` instead of requiring a separate integer swap-id table
- Exposes `GetBlockTypeSchemaComponent()` so callers can reach the reusable schema component

## Intended Use
Use this class when you want a chunk world that already carries the project-specific block-definition lookup component but do not want the lookup state baked directly into the actor itself.
