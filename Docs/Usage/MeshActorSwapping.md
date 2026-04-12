# Mesh Actor Swapping

## Purpose
Use mesh/actor swapping when represented voxel blocks should become full actors with richer behavior once a nearby gameplay actor makes them relevant.

## How To Use It
1. Use `AChunkWorldExtended` as the chunk-world actor.
2. Configure block definitions that identify which represented blocks are allowed to swap into actors.
3. Use an actor with `UChunkWorldProximityComponent` to drive swap relevance.
4. Ensure that actor is active in the world so the swap registration subsystem can discover its proximity component.

`AChunkWorldExtended` already includes the chunk-world swap scanner and swap host components, so they do not need to be added again during normal setup.

## Runtime Behavior
- the proximity component defines where swap relevance is evaluated
- the built-in swap scanner on `AChunkWorldExtended` looks for represented blocks that should swap
- the built-in swap host on `AChunkWorldExtended` hides the represented block view and materializes the configured actor presentation
- replication keeps the swap state consistent across connected processes

## Setup Notes
- choose proximity distances conservatively so actor swaps do not churn excessively
- make sure swap actors have collision and interaction settings that fit your gameplay expectations
- if pooled swap actors keep runtime state, implement the pooling contract described in the design docs

## Related Docs
- [ChunkWorldGameplaySetup.md](./ChunkWorldGameplaySetup.md)
- [../Design/ChunkWorldBlockSwap.md](../Design/ChunkWorldBlockSwap.md)
