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
- `UChunkWorldHitBlueprintLibrary` for generic chunk-world hit resolution and non-damage helper functions

For simple block removal in either route, call `DestroyBlock()` on `AChunkWorldExtended`.

## Route 1: Damage-Capable Voxels
Use this route when blocks should have health, destructibility, predicted local state, or shared damage-oriented feedback.

### Required Setup
- use damage-oriented schema families in the registry
  - definition family derived from `FBlockDamageDefinition`
  - custom-data family derived from `FBlockDamageCustomData`
- add `UPorismPredictedBlockStateComponent` to player-controlled characters that need to damage voxels

### Interaction Support
Only add `UPorismDamageTraceInteractionComponent` when that character also needs interaction support for focused damage-capable blocks.

Do not treat the trace interaction component as the main damage interface. The main reusable damage interface is `UPorismPredictedBlockStateComponent`.

### Main Utilities
Use `UChunkWorldBlockDamageBlueprintLibrary` for damage-oriented helper functions such as:
- authoritative block damage application
- health and invincibility reads
- shared hit feedback helpers

Use `UChunkWorldHitBlueprintLibrary` alongside it when you first need to resolve a hit into a represented block.

### Summary
Choose this route when your project needs:
- voxel health
- destructibility rules tied to damage schemas
- client-side predicted block health
- shared damage and destroy feedback

## Route 2: Non-Damage Voxels
Use this route when blocks do not need health or damage support.

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
