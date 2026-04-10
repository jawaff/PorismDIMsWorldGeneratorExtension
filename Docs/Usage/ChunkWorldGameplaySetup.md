# Chunk World Gameplay Setup

## Purpose
Use this guide to choose the correct Porism extension setup for your project.

There are two intended routes:
- a damage-capable route for games that want voxel health and damage
- a non-damage route for games that only need interaction and generic block hit helpers

I'd recommend building off of these two routes as a baseline of functionality. If you want to do your own damage system, then start with the non-damage 
route and extend to your heart's desire.

## Shared Base
Both routes use:
- `AChunkWorldExtended` as the chunk-world host
- `UBlockTypeSchemaComponent` on that chunk world through the actor's schema setup
- `UChunkWorldBlockSwapScannerComponent` on that chunk world when schema-authored block swap actors should be materialized
- `UChunkWorldSwapRegistrationSubsystem` to connect active proximity sources to active chunk-world swap scanners
- `UChunkWorldHitBlueprintLibrary` for generic chunk-world hit resolution and non-damage helper functions

For simple block removal in either route, call `DestroyBlock()` on `AChunkWorldExtended`.
For proximity-driven mesh/actor presentation swap, add `UChunkWorldProximityComponent` to the relevant actor, usually the player character or another actor that defines swap relevance.
For the full swap feature design and ownership boundary, see `Docs/Design/ChunkWorldBlockSwap.md`.

### Swap Setup Notes
- `UChunkWorldProximityComponent` now drives polling queries, not overlap-event tracking.
- Configure `SwapScanInterval` on `UChunkWorldBlockSwapScannerComponent` and `SwapInDistance` / `SwapOutDistance` on `UChunkWorldProximityComponent`.
- `UChunkWorldBlockSwapScannerComponent` now preloads and pools swap actors. Configure warm counts and pooled parking conservatively if many block types can swap into different actor classes.
- Use a collision channel for proximity queries that resolves the same represented chunk-world block data path you expect interaction and damage systems to hit.
- Configure swapped actor collision so player interaction traces do not pass through the actor and accidentally hit the chunk world behind it.
- Set the hidden parking area before swapping begins. The plugin stays generic, while project code can supply a centralized policy from a chunk-world subclass.
- Treat `UChunkWorldPooledSwapActorInterface` as the expected contract for project-owned pooled swap actors. If a swap actor keeps timers, effects, child actors, cached block data, or any other meaningful runtime state, implement `PrepareForSwapUse(...)` and `ResetForSwapPool()` so pooled reuse is safe.

## Route 1: Damage-Capable Voxels
Use this route when blocks should have health, destructibility, predicted local state, or shared damage-oriented feedback.

Starter character class:
- `AChunkWorldDamagePlayerCharacter`
  - includes `UPorismDamageTraceInteractionComponent`
  - includes `UPorismPredictedBlockStateComponent`
  - includes `UChunkWorldProximityComponent`

### Required Setup
- use damage-oriented schema families in the registry
  - definition family derived from `FBlockDamageDefinition`
  - custom-data family derived from `FBlockDamageCustomData`
- add `UPorismPredictedBlockStateComponent` to player-controlled characters that need to damage voxels or read shared health state for focused UI
- when damage-capable blocks also use actor swap, keep the damage interaction and predicted-state components on the same character that owns the proximity component so focused block UI and swap relevance stay aligned

### Interaction Support
Only add `UPorismDamageTraceInteractionComponent` when that character also needs interaction support for focused damage-capable blocks.

Do not treat the trace interaction component as the main damage interface. The main reusable damage interface is `UPorismPredictedBlockStateComponent`.

Use the split like this:
- `UPorismPredictedBlockStateComponent` for shared health reads, damage requests, prediction, and reconciliation
- `UPorismDamageTraceInteractionComponent` for focused damage-aware UI and simple "damage the currently focused block" input flows
- `UPorismTraceInteractionComponent` only when you need generic interaction without health semantics

### Main Utilities
Use `UPorismPredictedBlockStateComponent` as the primary runtime API for:
- shared damage requests
- shared health-state reads
- prediction and reconciliation

Use `UChunkWorldBlockDamageBlueprintLibrary` for lower-level schema and authoritative damage helpers that back the shared component route.

Use `UChunkWorldHitBlueprintLibrary` alongside it when you first need to resolve a hit into a represented block.

### Summary
Choose this route when your project needs:
- voxel health
- destructibility rules tied to damage schemas
- client-side predicted block health
- shared damage and destroy feedback

## Route 2: Non-Damage Voxels
Use this route when blocks do not need health or damage support.

Starter character class:
- `AChunkWorldPlayerCharacter`
  - includes `UPorismTraceInteractionComponent`
  - includes `UChunkWorldProximityComponent`

### Required Setup
- use the base struct families in the registry
  - definition family derived from `FBlockDefinitionBase`
  - custom-data family derived from `FBlockCustomDataBase`

### Interaction Support
If characters need interaction support, use `UPorismTraceInteractionComponent`.

This route does not require `UPorismPredictedBlockStateComponent` or `UPorismDamageTraceInteractionComponent`.

### Main Utilities
Use `UChunkWorldHitBlueprintLibrary` for non-damage oriented helper functions such as:
- resolving `FHitResult` into a represented block
- reading block identity and schema-backed context
- other generic chunk-world hit helpers

If you need to remove a block without a health system, call `DestroyBlock()` on `AChunkWorldExtended`.

### Summary
Choose this route when your project needs:
- chunk-world block interaction
- generic hit resolution
- schema-backed block definitions or custom data
- direct block removal without a damage model

## Recommended Rule
Pick one route per gameplay need:
- if the project needs voxel damage, use the damage families and the predicted block state component
- if the project does not need voxel damage, stay on the base families and the base trace interaction component

That keeps the extension setup simple and makes the intended public interfaces clear.

For projects with a world-specific parking rule:
- keep that rule outside the plugin
- push it into `UChunkWorldBlockSwapComponent` from a project chunk-world subclass during startup
- validate that the chosen parking Z stays outside any meaningful generated space for the world definition

## Starter Character Classes
The extension plugin now includes two minimal player-facing character bases you can subclass in Blueprint or extend in C++:
- `AChunkWorldPlayerCharacter`
- `AChunkWorldDamagePlayerCharacter`

These classes are intentionally minimal. They establish the component composition for each setup, but they do not include project-specific movement, camera, UI, input, or equipment logic.
