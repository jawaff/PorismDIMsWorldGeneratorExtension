# ChunkWorldDamage

## Purpose
`UChunkWorldBlockDamageBlueprintLibrary`, `UPorismPredictedBlockStateComponent`, and `UChunkWorldBlockFeedbackComponent` provide the shared chunk-world block damage route for projects using the extension plugin.

They provide:
- authoritative block damage application from `FChunkWorldResolvedBlockHit`
- explicit predicted and authoritative damage entry points on `UPorismPredictedBlockStateComponent`
- local-only predicted health results without mutating client chunk-world custom data
- schema-aligned health and invincibility reads through `FBlockDamageDefinition` and `FBlockDamageCustomData`
- a damage-aware interaction component for health-capable blocks
- authoritative hit and destroy feedback with optional immediate local initiator feedback
- event-driven retirement of predicted values when authoritative health-related custom-data replication arrives

## Main Types
- `UChunkWorldBlockDamageBlueprintLibrary`
- `UPorismDamageTraceInteractionComponent`
- `UPorismPredictedBlockStateComponent`
- `UChunkWorldBlockFeedbackComponent`
- `UChunkWorldDestructionActorInterface`
- `FChunkWorldBlockDamageResult`
- `FChunkWorldBlockDestructionRequest`
- `FChunkWorldResolvedBlockHit`
- `FChunkWorldBlockHitAuthorityPayload`
- `FBlockDamageDefinition`
- `FBlockDamageCustomData`

## Required Schema
The shared damage path assumes the resolved block uses the plugin damage schema family.

Required definition family:
- `FBlockDamageDefinition`
  - `DestructionActorClass`
  - `MaxHealth`
  - `HitSound`

Required custom-data family:
- `FBlockDamageCustomData`
  - `Health`
  - inherited `bInvincible`

Optional destroy feedback comes from `FBlockDefinitionBase`:
- `DestroyedSound`
- `DestroyedEffect`

Optional destruction presentation comes from `FBlockDamageDefinition`:
- `DestructionActorClass`

When `DestructionActorClass` is authored, that actor class is expected to implement `UChunkWorldDestructionActorInterface`.

If the resolved block does not use those schema families, the shared damage helpers return `false` and do nothing.

## Current Behavior
- Reads the current block health view through `UPorismPredictedBlockStateComponent::TryGetHealthState(...)`.
- Applies local-only prediction through `UPorismPredictedBlockStateComponent::ApplyPredictedDamageRequest(...)`.
- Applies authoritative mutation on the server through `UPorismPredictedBlockStateComponent::ApplyAuthoritativeDamageRequest(...)`.
- Falls back to authored max health before runtime custom data is initialized.
- Initializes block custom data on demand for authoritative writes only.
- Honors `bInvincible` from `FBlockCustomDataBase`.
- Applies real block mutation only on the server.
- Removes the block through `AChunkWorldExtended::DestroyBlock(...)` when health reaches zero or lower after an authoritative write.
- Spawns and triggers `DestructionActorClass` through `UChunkWorldDestructionActorInterface` when that class is authored on the damage definition.
- Reports `BlockTypeName` in `FChunkWorldBlockDamageResult`.
- Plays non-lethal hit feedback through the shared feedback component on authoritative non-lethal writes.
- Plays destroy feedback through the shared feedback component on authoritative destroy paths.
- Plays immediate local hit feedback for locally predicted requests.
- Retires predictions when authoritative health-related custom-data replication is applied for the same `ChunkWorld + BlockWorldPos`.
- Falls back to timeout cleanup for stale predictions.

The shared path intentionally does not own:
- collision routing
- melee dedupe
- combat damage calculation
- team filtering
- project-specific UI payloads

## How To Use
1. Resolve a block hit into `FChunkWorldResolvedBlockHit` with `UChunkWorldHitBlueprintLibrary`.
2. Compute final gameplay damage outside the plugin.
3. Submit the request through `UPorismPredictedBlockStateComponent`.
4. Read current block health through `UPorismPredictedBlockStateComponent`.

If your interaction flow needs focused UI or input helpers, add `UPorismDamageTraceInteractionComponent` on the same owner.

`UPorismDamageTraceInteractionComponent`:
- exposes `FChunkWorldDamageBlockInteractionResult`
- reads prediction through `UPorismPredictedBlockStateComponent` when present on the owner
- stays inactive until the currently focused block can produce a valid health view
- emits one-shot initialization for the currently focused block through `OnDamageBlockCustomDataInitialized`

Typical authoritative flow:
1. Resolve `FChunkWorldResolvedBlockHit`.
2. Compute project-specific damage outside the plugin.
3. Build `FChunkWorldBlockDamageRequest`.
4. Call `ApplyAuthoritativeDamageRequest(...)` on an authority-owned `UPorismPredictedBlockStateComponent`.
5. Inspect `FChunkWorldBlockDamageRequestResult` and `FChunkWorldBlockDamageResult` for:
   - `bAccepted`
   - `bAuthoritativeDamageApplied`
   - `bAppliedDamage`
   - `bWasInvincible`
   - `bDestroyed`
   - `PreviousHealth`
   - `NewHealth`
   - `BlockTypeName`

Typical predicted flow:
1. Resolve `FChunkWorldResolvedBlockHit`.
2. Compute project-specific damage outside the plugin.
3. Build `FChunkWorldBlockDamageRequest`.
4. Call `ApplyPredictedDamageRequest(...)` on a locally controlled client-owned `UPorismPredictedBlockStateComponent`.
5. Inspect `FChunkWorldBlockDamageRequestResult` for:
   - `bPredictionWritten`
   - `bPlayedImmediateLocalFeedback`

## Shared Request Model
`UPorismPredictedBlockStateComponent` is the main reusable block-damage boundary.

Use it for:
- focused trace damage
- melee or weapon collision damage
- projectiles
- spells
- scripted world damage

The request model is intentionally source-agnostic:
- `ResolvedHit`
- `DamageAmount`
- optional `RequestContextTag`

For client-to-server handoff, do not send `FChunkWorldResolvedBlockHit` as authoritative truth. Convert it into `FChunkWorldBlockHitAuthorityPayload`, send that payload, and let the server re-resolve a fresh `FChunkWorldResolvedBlockHit` before it computes and applies damage.

## Feedback Replication
`UChunkWorldBlockFeedbackComponent` is the shared replicated feedback host for chunk-world block damage.

Current behavior:
- multicasts authoritative feedback from the chunk world
- applies a local distance filter before spawning sound or Niagara
- supports:
  - hit sound only for non-lethal damage
  - destroyed sound and destroyed Niagara for block removal
  - optional immediate local initiator feedback for predicted requests

Chunk worlds that want shared feedback should own a `UChunkWorldBlockFeedbackComponent`.

`AChunkWorldExtended` creates one by default.

## Prediction Model
`UPorismPredictedBlockStateComponent` is the local-only prediction cache.

Use it when:
- a locally controlled pawn needs predicted block health for UI or gameplay

Current behavior:
- stores predicted results by `ChunkWorld + BlockWorldPos`
- steps predicted health down from the latest stored predicted result for the same block
- prefers predicted values over authoritative reads until reconciliation
- never writes authoritative values into the prediction cache
- retires predictions on:
  - authoritative health-related replicated custom-data update for the same block
  - timeout fallback

The server should not use prediction state.

## Reconciliation
The shared reconciliation path is event-driven.

Current route:
1. authoritative custom-data replication is applied by the chunk world
2. `AChunkWorldExtended` broadcasts `OnBlockCustomDataChanged`
3. `UPorismPredictedBlockStateComponent` clears matching prediction and pending queued damage when appropriate
4. `UPorismPredictedBlockStateComponent` broadcasts `OnTrackedBlockStateChanged`

This avoids chunk scans and keeps the prediction cache bounded.

## Intended Use
Use this shared damage route when your project:
- already uses the plugin schema system
- wants block damage keyed by `BlockTypeName`
- wants shared hit and destroy feedback replication
- wants local predicted health without client-side custom-data mutation

Keep project-specific behavior outside the plugin, for example:
- collision-manager routing
- damage shaping from stats, tools, spells, or weapon rules
- project-specific UI presentation
- project-specific actor interaction and swap behavior

The plugin does own the stable destruction trigger contract:
- author `DestructionActorClass` on the block damage definition
- implement `TriggerBlockDestruction(...)` through `UChunkWorldDestructionActorInterface`
- keep the destruction actor's Chaos or one-shot presentation logic inside that actor
