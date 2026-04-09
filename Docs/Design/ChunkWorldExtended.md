# ChunkWorldExtended

## Purpose
`AChunkWorldExtended` is the thin reusable host actor for the plugin's block lookup flow.

It provides:
- a default place to attach the reusable block type schema component
- a single actor-level schema registry property that feeds that component
- a default place to host shared authoritative block custom-data update notifications
- a default place to host the shared server-side swap scanner
- a default place to host shared replicated block swap transitions
- a clean subclass of `AChunkWorld` for future project-specific chunk-world behavior
- a simple getter for the schema component

## Current Behavior
- Owns a `UBlockTypeSchemaComponent` by default
- Owns a single editable `BlockTypeSchemaRegistry` property and syncs it into the runtime schema component
- Owns a `UChunkWorldBlockFeedbackComponent` by default
- Owns a `UChunkWorldBlockSwapScannerComponent` by default
- Owns a `UChunkWorldBlockSwapComponent` by default
- Rebuilds the component's lookup maps during `StartGen()` after the base chunk world has populated its runtime indexes
- Emits `OnBlockCustomDataChanged` after authoritative custom-data writes have settled for a block
- Coalesces slot-level replicated writes into one block-level notification on the next tick so listeners refresh against a settled block view
- Uses committed health writes to drive server-authoritative lethal block cleanup
- Routes authoritative destroy feedback through `UChunkWorldBlockFeedbackComponent`
- Resolves schema-authored swap actor classes and distances through `UBlockTypeSchemaComponent` instead of requiring a separate integer swap-id table
- Exposes `GetBlockSwapScannerComponent()` so callers can reach the shared scanner when they need diagnostics or direct bindings
- Exposes `GetBlockTypeSchemaComponent()` so callers can reach the reusable schema component
- Exposes `GetBlockFeedbackComponent()` so callers can reuse the shared block hit and destroy feedback host
- Exposes `GetBlockSwapComponent()` so callers can reuse the shared replicated swap host

## Damage And Reconciliation Contract
`AChunkWorldExtended` is the authority-side mutation host, not the prediction owner.

That means:
- client prediction does not write real chunk-world custom data
- authority-owned writes commit through the chunk world and then notify observers with `OnBlockCustomDataChanged`
- `UPorismPredictedBlockStateComponent` listens to that event to retire stale prediction and refresh focused UI

When authoritative health writes leave a shared damage-family block at zero or lower, the chunk world destroys the block through its authoritative destroy path instead of leaving lethal-state cleanup to callers.

## Intended Use
Use this class when you want a chunk world that already carries the project-specific block-definition lookup component but do not want the lookup state baked directly into the actor itself.
