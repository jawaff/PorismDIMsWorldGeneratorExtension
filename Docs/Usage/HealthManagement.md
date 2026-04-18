# Health Management

## Purpose
Use the health-management feature set when blocks need replicated health, authoritative damage/healing, and optional client-side prediction.

## How To Use It
1. Use damage-capable schema families for the relevant block types.
2. Use a player-controlled actor that exposes `UPorismPredictedBlockStateComponent` when that actor should read or modify block health.
3. Use `UPorismHealthInteractionComponent` when the same actor should also expose focused health information for the currently looked-at block.
4. Route block damage through the predicted block state component or the authoritative damage helpers instead of writing custom-data slots directly.
5. Route block healing through the same component/library surface instead of writing custom-data slots directly.

If you are using `AChunkWorldDamagePlayerCharacter`, the predicted block state and health-oriented interaction components are already part of the starter character setup.

## Runtime Responsibilities
- `UPorismPredictedBlockStateComponent`
  - sends health modification requests
  - tracks predicted local health state
  - reconciles back to authoritative custom-data replication
- `UPorismHealthInteractionComponent`
  - exposes focused health information for the currently targeted block
- schema-backed damage definitions
  - define block health and related destruction behavior

## Recommended Rule
- treat health as schema-owned gameplay state
- use the predicted block state component as the main runtime API
- keep direct custom-data writes as low-level implementation details
- keep damage and healing as separate calls when project behavior differs, such as damage-only hit or destroy feedback

## Related Docs
- [ChunkWorldGameplaySetup.md](./ChunkWorldGameplaySetup.md)
- [ServerAuthoritativeBlockDamage.md](./ServerAuthoritativeBlockDamage.md)
- [../Design/ChunkWorldDamage.md](../Design/ChunkWorldDamage.md)
