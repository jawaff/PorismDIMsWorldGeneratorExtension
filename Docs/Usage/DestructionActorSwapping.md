# Destruction Actor Swapping

## Purpose
Use destruction actor swapping when block destruction should spawn a dedicated presentation actor instead of only removing the represented voxel.

## How To Use It
1. Use a damage-capable block definition family for blocks that can be destroyed.
2. Configure `DestructionActorClass` in the relevant block definitions.
3. Implement `UChunkWorldDestructionActorInterface` on the destruction actor when it needs block-context-aware startup behavior.
4. Destroy the block through the shared damage flow or other authoritative destruction path.

## Runtime Behavior
- once destruction is confirmed, the chunk-world extension resolves the configured destruction actor
- the destruction presentation actor is spawned with the block context needed for playback
- the actor handles its own effects, animation, and cleanup lifetime
- network delivery follows the presentation mode configured by the destruction flow

## Recommended Rule
- keep destruction actors focused on presentation
- let gameplay state changes happen before the destruction actor spawns
- avoid putting long-lived authoritative gameplay state inside the destruction actor

## Related Docs
- [ServerAuthoritativeBlockDamage.md](./ServerAuthoritativeBlockDamage.md)
- [../Design/PorismChaosDestructionPresentation.md](../Design/PorismChaosDestructionPresentation.md)
- [../Design/ChunkWorldDamage.md](../Design/ChunkWorldDamage.md)
